#include "server.h"

#include "http_json.h"
#include "nloj/common/mq.h"
#include "nloj/common/mysql.h"
#include "nloj/common/redis.h"
#include "nloj/judge/module.h"
#include "nloj/judge/node.h"
#include "nloj/problem/module.h"
#include "nloj/submit/module.h"
#include "nloj/user/module.h"

#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace {

constexpr const char* kJsonMime = "application/json; charset=utf-8";
constexpr const char* kHtmlMime = "text/html; charset=utf-8";
constexpr const char* kYamlMime = "application/yaml; charset=utf-8";

// Swagger UI（C++ 没有 Knife4j，同一份 openapi.yaml）。CDN 静态资源。
constexpr const char* kSwaggerHtml =
    "<!DOCTYPE html>\n"
    "<html lang=\"zh-CN\">\n"
    "<head>\n"
    "  <meta charset=\"utf-8\"/>\n"
    "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"/>\n"
    "  <title>NLOJ API Docs</title>\n"
    "  <link rel=\"stylesheet\" href=\"https://unpkg.com/swagger-ui-dist@5.17.14/swagger-ui.css\"/>\n"
    "  <style>\n"
    "    body { margin: 0; background: #fafafa; }\n"
    "    .nloj-bar { background: #1d1e2c; color: #fff; padding: 14px 24px;\n"
    "                font: 16px/1.4 sans-serif; }\n"
    "    .nloj-bar small { color: #9aa3b2; margin-left: 12px; }\n"
    "  </style>\n"
    "</head>\n"
    "<body>\n"
    "  <div class=\"nloj-bar\">NLOJ API"
    "<small>需要登录的接口：在 Parameters 里填 Authorization，写成 Bearer 空格 token</small></div>\n"
    "  <div id=\"swagger-ui\"></div>\n"
    "  <script src=\"https://unpkg.com/swagger-ui-dist@5.17.14/swagger-ui-bundle.js\"></script>\n"
    "  <script>\n"
    "    window.ui = SwaggerUIBundle({\n"
    "      url: '/api/docs/openapi.yaml',\n"
    "      dom_id: '#swagger-ui',\n"
    "      persistAuthorization: true,\n"
    "      tryItOutEnabled: true,\n"
    "      filter: true\n"
    "    });\n"
    "  </script>\n"
    "</body>\n"
    "</html>\n";

int file_exists(const std::string& path) {
    std::ifstream in(path);
    return in.good() ? 1 : 0;
}

int read_all_text(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return 0;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return out.empty() ? 0 : 1;
}

std::string exe_dir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return "";
    }
    const std::string p(buf, n);
    const auto slash = p.find_last_of("\\/");
    if (slash == std::string::npos) {
        return "";
    }
    return p.substr(0, slash);
#else
    return ".";
#endif
}

// 环境变量 → exe 旁副本 → 编译期仓库路径。
std::string find_openapi_path() {
    const char* env = std::getenv("NLOJ_OPENAPI");
    if (env != nullptr && file_exists(env)) {
        return env;
    }
    const std::string beside = exe_dir() + "/openapi.yaml";
    if (file_exists(beside)) {
        return beside;
    }
#ifdef NLOJ_OPENAPI_PATH
    if (file_exists(NLOJ_OPENAPI_PATH)) {
        return NLOJ_OPENAPI_PATH;
    }
#endif
    return "";
}

void write_body(httplib::Response& res, const std::string& body) {
    res.set_content(body, kJsonMime);
}

// Swagger 页在 127.0.0.1、契约写 localhost 时浏览器会拦跨域。
void add_cors(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, Accept");
}

int parse_json_object(const std::string& raw, nlohmann::json& out) {
    out = nlohmann::json::parse(raw, nullptr, false);
    if (out.is_discarded() || !out.is_object()) {
        return 0;
    }
    return 1;
}

int read_string_field(const nlohmann::json& j, const char* key, std::string& out) {
    if (!j.contains(key) || !j[key].is_string()) {
        return 0;
    }
    out = j[key].get<std::string>();
    return 1;
}

int read_i64_field(const nlohmann::json& j, const char* key, std::int64_t& out) {
    if (!j.contains(key) || !j[key].is_number_integer()) {
        return 0;
    }
    out = j[key].get<std::int64_t>();
    return 1;
}

int read_int_field(const nlohmann::json& j, const char* key, int& out) {
    std::int64_t v = 0;
    if (!read_i64_field(j, key, v)) {
        return 0;
    }
    out = static_cast<int>(v);
    return 1;
}

// Header Bearer → verify_token。失败已写入 40100。
int require_login(const httplib::Request& req,
                  httplib::Response& res,
                  nloj::user::AuthUser& out) {
    const std::string token = nloj::api::extract_bearer(
        req.get_header_value("Authorization")
    );
    if (token.empty()) {
        write_body(res, nloj::api::json_err(40100, "未登录"));
        return 0;
    }
    out = nloj::user::verify_token(token);
    if (out.id <= 0) {
        write_body(res, nloj::api::json_err(40100, "未登录"));
        return 0;
    }
    return 1;
}

int require_admin(const nloj::user::AuthUser& user, httplib::Response& res) {
    if (user.role != "admin") {
        write_body(res, nloj::api::json_err(40101, "无权限"));
        return 0;
    }
    return 1;
}

int read_page(const httplib::Request& req,
              httplib::Response& res,
              std::int64_t& page_num,
              std::int64_t& page_size) {
    const std::string num_raw = req.has_param("pageNum") ? req.get_param_value("pageNum") : "";
    const std::string size_raw = req.has_param("pageSize") ? req.get_param_value("pageSize") : "";
    if (!nloj::api::parse_page_query(num_raw, size_raw, page_num, page_size)) {
        write_body(res, nloj::api::json_err(40000, "请求参数错误"));
        return 0;
    }
    return 1;
}

int read_path_id(const httplib::Request& req, const char* name, std::int64_t& out) {
    const auto it = req.path_params.find(name);
    if (it == req.path_params.end()) {
        return 0;
    }
    if (!nloj::api::parse_i64(it -> second, out)) {
        return 0;
    }
    if (out <= 0) {
        return 0;
    }
    return 1;
}

int read_problem_body(const nlohmann::json& j, nloj::problem::CreateProblemRequest& req) {
    if (!read_string_field(j, "title", req.title)) {
        return 0;
    }
    if (!read_string_field(j, "difficulty", req.difficulty)) {
        return 0;
    }
    if (!read_string_field(j, "description", req.description)) {
        return 0;
    }
    if (!read_int_field(j, "timeLimit", req.time_limit)) {
        return 0;
    }
    if (!read_int_field(j, "memoryLimit", req.memory_limit)) {
        return 0;
    }
    if (j.contains("visible")) {
        if (!read_int_field(j, "visible", req.visible)) {
            return 0;
        }
    } else {
        req.visible = 1;
    }
    return 1;
}

nlohmann::json json_auth_user(const nloj::user::AuthUser& user) {
    nlohmann::json j;
    j["id"] = user.id;
    j["username"] = user.username;
    j["role"] = user.role;
    j["createTime"] = user.create_time;
    return j;
}

nlohmann::json json_problem_summary(const nloj::problem::ProblemSummary& p) {
    nlohmann::json j;
    j["id"] = p.id;
    j["title"] = p.title;
    j["difficulty"] = p.difficulty;
    j["timeLimit"] = p.time_limit;
    j["memoryLimit"] = p.memory_limit;
    j["visible"] = p.visible;
    j["createTime"] = p.create_time;
    return j;
}

nlohmann::json json_problem_detail(const nloj::problem::ProblemDetail& p) {
    nlohmann::json j = json_problem_summary(
        nloj::problem::ProblemSummary{
            p.id, p.title, p.difficulty, p.time_limit, p.memory_limit, p.visible,
            p.create_time
        }
    );
    j["description"] = p.description;
    nlohmann::json samples = nlohmann::json::array();
    for (const auto& s : p.samples) {
        nlohmann::json one;
        one["id"] = s.id;
        one["input"] = s.input;
        one["output"] = s.output;
        samples.push_back(one);
    }
    j["samples"] = samples;
    return j;
}

nlohmann::json json_submission(const nloj::submit::SubmissionDetail& s, int with_code) {
    nlohmann::json j;
    j["id"] = s.id;
    j["userId"] = s.user_id;
    j["problemId"] = s.problem_id;
    j["language"] = s.language;
    if (with_code) {
        j["code"] = s.code;
    }
    j["status"] = s.status;
    j["timeUsed"] = nloj::api::int_or_null(s.time_used);
    j["memoryUsed"] = nloj::api::int_or_null(s.memory_used);
    if (s.judge_info.empty()) {
        j["judgeInfo"] = nullptr;
    } else {
        j["judgeInfo"] = s.judge_info;
    }
    j["createTime"] = s.create_time;
    return j;
}

}  // namespace

namespace nloj::api {

void register_http_routes(httplib::Server& svr) {
    svr.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        add_cors(res);
        if (req.method == "OPTIONS") {
            res.status = 204;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // GET / → 文档页（不单独做前端）
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_redirect("/api/docs");
    });

    // GET /api/docs
    svr.Get("/api/docs", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(kSwaggerHtml, kHtmlMime);
    });

    // GET /api/docs/openapi.yaml
    svr.Get("/api/docs/openapi.yaml", [](const httplib::Request&, httplib::Response& res) {
        const std::string path = find_openapi_path();
        std::string yaml;
        if (path.empty() || !read_all_text(path, yaml)) {
            write_body(res, nloj::api::json_err(40400, "openapi.yaml 未找到"));
            return;
        }
        res.set_content(yaml, kYamlMime);
    });

    // GET /api/v1/health
    svr.Get("/api/v1/health", [](const httplib::Request&, httplib::Response& res) {
        MYSQL mysql;
        const int mysql_ok = nloj::common::start_mysql(mysql) ? 1 : 0;
        if (mysql_ok) {
            mysql_close(&mysql);
        }
        nlohmann::json data;
        data["mysql"] = mysql_ok ? "UP" : "DOWN";
        data["redis"] = nloj::common::redis_using() ? "UP" : "DOWN";
        data["rabbitmq"] = nloj::common::mq_using_rabbit() ? "UP" : "DOWN";
        data["status"] = mysql_ok ? "UP" : "DOWN";
        const nloj::problem::ProblemCacheStats st = nloj::problem::problem_cache_stats();
        nlohmann::json cache;
        cache["total"] = st.total;
        cache["l1Hit"] = st.l1_hit;
        cache["redisHit"] = st.redis_hit;
        cache["mysqlLoad"] = st.mysql_load;
        if (st.total > 0) {
            cache["hitRate"] = static_cast<double>(st.l1_hit + st.redis_hit)
                              / static_cast<double>(st.total);
        } else {
            cache["hitRate"] = 0.0;
        }
        data["cache"] = cache;
        data["embedWorker"] = embed_judge_worker_enabled();
        nlohmann::json nodes = nlohmann::json::array();
        for (const auto& id : nloj::judge::list_judge_nodes()) {
            nodes.push_back(id);
        }
        data["judgeNodes"] = nodes;
        write_body(res, nloj::api::json_ok(data));
    });

    // POST /api/v1/auth/register
    svr.Post("/api/v1/auth/register", [](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json body;
        if (!parse_json_object(req.body, body)) {
            write_body(res, nloj::api::json_err(40000, "请求参数错误"));
            return;
        }
        std::string username;
        std::string password;
        if (!read_string_field(body, "username", username)
         || !read_string_field(body, "password", password)) {
            write_body(res, nloj::api::json_err(40000, "请求参数错误"));
            return;
        }
        const std::int64_t id = nloj::user::register_user(username, password);
        if (id <= 0) {
            write_body(res, nloj::api::json_err(50001, "操作失败"));
            return;
        }
        write_body(res, nloj::api::json_ok(id));
    });

    // POST /api/v1/auth/login
    svr.Post("/api/v1/auth/login", [](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json body;
        if (!parse_json_object(req.body, body)) {
            write_body(res, nloj::api::json_err(40000, "请求参数错误"));
            return;
        }
        std::string username;
        std::string password;
        if (!read_string_field(body, "username", username)
         || !read_string_field(body, "password", password)) {
            write_body(res, nloj::api::json_err(40000, "请求参数错误"));
            return;
        }
        const nloj::user::LoginResult login = nloj::user::login_user(username, password);
        if (login.token.empty()) {
            write_body(res, nloj::api::json_err(50001, "用户名或密码错误"));
            return;
        }
        nlohmann::json data;
        data["token"] = login.token;
        data["userId"] = login.user.id;
        data["username"] = login.user.username;
        data["role"] = login.user.role;
        write_body(res, nloj::api::json_ok(data));
    });

    // GET /api/v1/users/me
    svr.Get("/api/v1/users/me", [](const httplib::Request& req, httplib::Response& res) {
        nloj::user::AuthUser token_user;
        if (!require_login(req, res, token_user)) {
            return;
        }
        const nloj::user::AuthUser user = nloj::user::get_current_user(
            std::to_string(token_user.id)
        );
        if (user.id <= 0) {
            write_body(res, nloj::api::json_err(40400, "资源不存在"));
            return;
        }
        write_body(res, nloj::api::json_ok(json_auth_user(user)));
    });

    // GET /api/v1/problems
    svr.Get("/api/v1/problems", [](const httplib::Request& req, httplib::Response& res) {
        std::int64_t page_num = 1;
        std::int64_t page_size = 20;
        if (!read_page(req, res, page_num, page_size)) {
            return;
        }
        const std::string difficulty = req.has_param("difficulty") ? req.get_param_value("difficulty") : "";
        const std::string keyword = req.has_param("keyword") ? req.get_param_value("keyword") : "";
        const nloj::problem::ProblemPage page = nloj::problem::list_problems(
            page_num, page_size, difficulty, keyword
        );
        nlohmann::json records = nlohmann::json::array();
        for (const auto& item : page.records) {
            records.push_back(json_problem_summary(item));
        }
        nlohmann::json data;
        data["pageNum"] = page.page_num;
        data["pageSize"] = page.page_size;
        data["total"] = page.total;
        data["records"] = records;
        write_body(res, nloj::api::json_ok(data));
    });

    // GET /api/v1/problems/:id
    svr.Get("/api/v1/problems/:id", [](const httplib::Request& req, httplib::Response& res) {
        std::int64_t id = 0;
        if (!read_path_id(req, "id", id)) {
            write_body(res, nloj::api::json_err(40000, "请求参数错误"));
            return;
        }
        int skip_cache = 0;
        if (req.has_header("X-NLOJ-Skip-Cache")
         && req.get_header_value("X-NLOJ-Skip-Cache") == "1") {
            skip_cache = 1;
        }
        const nloj::problem::ProblemDetail detail = nloj::problem::get_problem(id, skip_cache);
        if (detail.id <= 0) {
            write_body(res, nloj::api::json_err(40400, "资源不存在"));
            return;
        }
        write_body(res, nloj::api::json_ok(json_problem_detail(detail)));
    });

    // POST /api/v1/problems
    svr.Post("/api/v1/problems", [](const httplib::Request& req, httplib::Response& res) {
        nloj::user::AuthUser user;
        if (!require_login(req, res, user) || !require_admin(user, res)) {
            return;
        }
        nlohmann::json body;
        if (!parse_json_object(req.body, body)) {
            write_body(res, nloj::api::json_err(40000, "请求参数错误"));
            return;
        }
        nloj::problem::CreateProblemRequest create_req;
        if (!read_problem_body(body, create_req)) {
            write_body(res, nloj::api::json_err(40000, "请求参数错误"));
            return;
        }
        const std::int64_t id = nloj::problem::create_problem(create_req);
        if (id <= 0) {
            write_body(res, nloj::api::json_err(50001, "操作失败"));
            return;
        }
        write_body(res, nloj::api::json_ok(id));
    });

    // PUT /api/v1/problems/:id
    svr.Put("/api/v1/problems/:id", [](const httplib::Request& req, httplib::Response& res) {
        nloj::user::AuthUser user;
        if (!require_login(req, res, user) || !require_admin(user, res)) {
            return;
        }
        std::int64_t id = 0;
        if (!read_path_id(req, "id", id)) {
            write_body(res, nloj::api::json_err(40000, "请求参数错误"));
            return;
        }
        nlohmann::json body;
        if (!parse_json_object(req.body, body)) {
            write_body(res, nloj::api::json_err(40000, "请求参数错误"));
            return;
        }
        nloj::problem::CreateProblemRequest update_req;
        if (!read_problem_body(body, update_req)) {
            write_body(res, nloj::api::json_err(40000, "请求参数错误"));
            return;
        }
        if (!nloj::problem::update_problem(id, update_req)) {
            write_body(res, nloj::api::json_err(50001, "操作失败"));
            return;
        }
        write_body(res, nloj::api::json_ok(nullptr));
    });

    // POST /api/v1/submissions
    svr.Post("/api/v1/submissions", [](const httplib::Request& req, httplib::Response& res) {
        nloj::user::AuthUser user;
        if (!require_login(req, res, user)) {
            return;
        }
        nlohmann::json body;
        if (!parse_json_object(req.body, body)) {
            write_body(res, nloj::api::json_err(40000, "请求参数错误"));
            return;
        }
        std::int64_t problem_id = 0;
        std::string language;
        std::string code;
        if (!read_i64_field(body, "problemId", problem_id)
         || !read_string_field(body, "language", language)
         || !read_string_field(body, "code", code)) {
            write_body(res, nloj::api::json_err(40000, "请求参数错误"));
            return;
        }
        const std::int64_t id = nloj::submit::create_submission(
            user.id, problem_id, language, code
        );
        if (id <= 0) {
            write_body(res, nloj::api::json_err(50001, "操作失败"));
            return;
        }
        nlohmann::json data;
        data["submissionId"] = id;
        write_body(res, nloj::api::json_ok(data));
    });

    // GET /api/v1/submissions
    svr.Get("/api/v1/submissions", [](const httplib::Request& req, httplib::Response& res) {
        nloj::user::AuthUser user;
        if (!require_login(req, res, user)) {
            return;
        }
        std::int64_t page_num = 1;
        std::int64_t page_size = 20;
        if (!read_page(req, res, page_num, page_size)) {
            return;
        }
        std::int64_t problem_id = 0;
        if (req.has_param("problemId")) {
            if (!nloj::api::parse_i64(req.get_param_value("problemId"), problem_id)) {
                write_body(res, nloj::api::json_err(40000, "请求参数错误"));
                return;
            }
        }
        const std::string status = req.has_param("status") ? req.get_param_value("status") : "";
        const nloj::submit::SubmissionPage page = nloj::submit::list_my_submissions(
            user.id, page_num, page_size, problem_id, status
        );
        nlohmann::json records = nlohmann::json::array();
        for (const auto& item : page.records) {
            records.push_back(json_submission(item, 0));
        }
        nlohmann::json data;
        data["pageNum"] = page.page_num;
        data["pageSize"] = page.page_size;
        data["total"] = page.total;
        data["records"] = records;
        write_body(res, nloj::api::json_ok(data));
    });

    // GET /api/v1/submissions/:id
    svr.Get("/api/v1/submissions/:id", [](const httplib::Request& req, httplib::Response& res) {
        nloj::user::AuthUser user;
        if (!require_login(req, res, user)) {
            return;
        }
        std::int64_t id = 0;
        if (!read_path_id(req, "id", id)) {
            write_body(res, nloj::api::json_err(40000, "请求参数错误"));
            return;
        }
        const nloj::submit::SubmissionDetail detail = nloj::submit::get_submission(
            id, user.id, user.role
        );
        if (detail.id <= 0) {
            write_body(res, nloj::api::json_err(40400, "资源不存在"));
            return;
        }
        write_body(res, nloj::api::json_ok(json_submission(detail, 1)));
    });
}

int embed_judge_worker_enabled() {
#ifdef _WIN32
    char buf[8];
    const DWORD n = GetEnvironmentVariableA("NLOJ_EMBED_WORKER", buf, 8);
    if (n == 1 && buf[0] == '0') {
        return 0;
    }
    return 1;
#else
    const char* raw = std::getenv("NLOJ_EMBED_WORKER");
    if (raw != nullptr && raw[0] == '0') {
        return 0;
    }
    return 1;
#endif
}

void judge_worker_loop() {
    for (;;) {
        nloj::common::JudgeTaskMessage task;
        nloj::common::wait_pop_judge_task(task);
        nloj::judge::run_judge_task(task.submission_id);
        nloj::common::ack_judge_task(task);
    }
}

}  // namespace nloj::api
