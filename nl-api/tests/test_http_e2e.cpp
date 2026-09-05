// nl-api HTTP 全链路（需要本机 MySQL；判题需要 Docker 或 PATH 里的 g++）。
// 进程内起路由 + Worker，不占用 8080，也不拖垮 nloj_api_http_test。
#include "nloj/common/mysql.h"
#include "server.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

int g_failed = 0;
int g_name_seq = 0;
int g_port = 18080;

constexpr const char* kJsonMime = "application/json";
constexpr const char* kPassword = "secret123";
constexpr const char* kAcCode =
    "#include <iostream>\n"
    "int main() { int a,b; std::cin>>a>>b; std::cout<<a+b<<std::endl; return 0; }\n";

void expect_true(const char* name, int ok) {
    if (ok) {
        std::cout << "[PASS] " << name << '\n';
    } else {
        std::cout << "[FAIL] " << name << '\n';
        g_failed = 1;
    }
}

std::string unique_name(const char* prefix) {
    ++g_name_seq;
    return std::string(prefix) + std::to_string(std::time(nullptr)) + "_"
           + std::to_string(g_name_seq);
}

httplib::Headers auth_headers(const std::string& token) {
    httplib::Headers headers;
    headers.emplace("Authorization", "Bearer " + token);
    return headers;
}

int parse_res(const httplib::Result& res, nlohmann::json& out) {
    if (!res) {
        return 0;
    }
    out = nlohmann::json::parse(res -> body, nullptr, false);
    if (out.is_discarded() || !out.is_object()) {
        return 0;
    }
    return 1;
}

int json_code(const nlohmann::json& j) {
    if (!j.contains("code") || !j["code"].is_number_integer()) {
        return -1;
    }
    return j["code"].get<int>();
}

int read_data_i64(const nlohmann::json& j, std::int64_t& out) {
    if (!j.contains("data") || !j["data"].is_number_integer()) {
        return 0;
    }
    out = j["data"].get<std::int64_t>();
    return 1;
}

int read_data_string(const nlohmann::json& j, const char* key, std::string& out) {
    if (!j.contains("data") || !j["data"].is_object()) {
        return 0;
    }
    if (!j["data"].contains(key) || !j["data"][key].is_string()) {
        return 0;
    }
    out = j["data"][key].get<std::string>();
    return 1;
}

int read_data_obj_i64(const nlohmann::json& j, const char* key, std::int64_t& out) {
    if (!j.contains("data") || !j["data"].is_object()) {
        return 0;
    }
    if (!j["data"].contains(key) || !j["data"][key].is_number_integer()) {
        return 0;
    }
    out = j["data"][key].get<std::int64_t>();
    return 1;
}

httplib::Result post_json(httplib::Client& cli,
                          const char* path,
                          const nlohmann::json& body,
                          const std::string& token) {
    const std::string raw = body.dump();
    if (token.empty()) {
        return cli.Post(path, raw, kJsonMime);
    }
    return cli.Post(path, auth_headers(token), raw, kJsonMime);
}

int promote_admin(std::int64_t user_id) {
    MYSQL mysql;
    if (!nloj::common::start_mysql(mysql)) {
        return 0;
    }
    const std::string sql = "UPDATE `user` SET role='admin' WHERE id="
                           + std::to_string(user_id);
    const int ok = nloj::common::query_exec(&mysql, sql) ? 1 : 0;
    mysql_close(&mysql);
    return ok;
}

// create_problem 不插用例，e2e 自己补，否则很难稳定断言 AC。
int insert_case(std::int64_t problem_id,
                const std::string& input,
                const std::string& output,
                int sort_order) {
    MYSQL mysql;
    if (!nloj::common::start_mysql(mysql)) {
        return 0;
    }
    const std::string escaped_in = nloj::common::escape_sql(&mysql, input);
    const std::string escaped_out = nloj::common::escape_sql(&mysql, output);
    const std::string sql = "INSERT INTO problem_case (problem_id, input, output, is_sample, sort_order) VALUES ("
                           + std::to_string(problem_id) + ", '"
                           + escaped_in + "', '"
                           + escaped_out + "', 1, "
                           + std::to_string(sort_order) + ")";
    const int ok = nloj::common::query_exec(&mysql, sql) ? 1 : 0;
    mysql_close(&mysql);
    return ok;
}

int is_terminal_status(const std::string& status) {
    if (status == "AC"
     || status == "WA"
     || status == "CE"
     || status == "TLE"
     || status == "MLE"
     || status == "RE"
     || status == "SYSTEM_ERROR") {
        return 1;
    }
    return 0;
}

int wait_health(httplib::Client& cli) {
    for (int i = 0; i < 50; ++i) {
        const httplib::Result res = cli.Get("/api/v1/health");
        if (res && res -> status == 200) {
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return 0;
}

int login_token(httplib::Client& cli,
                const std::string& username,
                const std::string& password,
                std::string& token,
                std::int64_t& user_id) {
    nlohmann::json body;
    body["username"] = username;
    body["password"] = password;
    const httplib::Result res = post_json(cli, "/api/v1/auth/login", body, "");
    nlohmann::json j;
    if (!parse_res(res, j) || json_code(j) != 0) {
        return 0;
    }
    if (!read_data_string(j, "token", token)) {
        return 0;
    }
    if (!read_data_obj_i64(j, "userId", user_id)) {
        return 0;
    }
    return 1;
}

int register_user_http(httplib::Client& cli,
                       const std::string& username,
                       std::int64_t& user_id) {
    nlohmann::json body;
    body["username"] = username;
    body["password"] = kPassword;
    const httplib::Result res = post_json(cli, "/api/v1/auth/register", body, "");
    nlohmann::json j;
    if (!parse_res(res, j) || json_code(j) != 0) {
        return 0;
    }
    return read_data_i64(j, user_id);
}

int records_contain_id(const nlohmann::json& j, std::int64_t id) {
    if (!j.contains("data") || !j["data"].is_object()) {
        return 0;
    }
    if (!j["data"].contains("records") || !j["data"]["records"].is_array()) {
        return 0;
    }
    for (const auto& item : j["data"]["records"]) {
        if (item.contains("id") && item["id"].is_number_integer()
            && item["id"].get<std::int64_t>() == id) {
            return 1;
        }
    }
    return 0;
}

void test_negative(httplib::Client& cli, const std::string& user_token) {
    nlohmann::json empty;
    nlohmann::json j;

    const httplib::Result bad_reg = post_json(cli, "/api/v1/auth/register", empty, "");
    expect_true("register missing fields 40000",
                parse_res(bad_reg, j) && json_code(j) == 40000);

    nlohmann::json bad_login;
    bad_login["username"] = unique_name("e2e_miss_");
    bad_login["password"] = "wrong-pass";
    const httplib::Result login_miss = post_json(cli, "/api/v1/auth/login", bad_login, "");
    expect_true("login unknown user 50001",
                parse_res(login_miss, j) && json_code(j) == 50001);

    const httplib::Result no_auth = cli.Get("/api/v1/users/me");
    expect_true("me without bearer 40100",
                parse_res(no_auth, j) && json_code(j) == 40100);

    const httplib::Result fake = cli.Get("/api/v1/users/me", auth_headers("not.a.jwt"));
    expect_true("me fake bearer 40100",
                parse_res(fake, j) && json_code(j) == 40100);

    nlohmann::json problem;
    problem["title"] = "nope";
    problem["difficulty"] = "EASY";
    problem["description"] = "x";
    problem["timeLimit"] = 1000;
    problem["memoryLimit"] = 262144;
    const httplib::Result user_create = post_json(
        cli, "/api/v1/problems", problem, user_token
    );
    expect_true("user create problem 40101",
                parse_res(user_create, j) && json_code(j) == 40101);
}

void test_happy_path(httplib::Client& cli) {
    // health → 注册登录 → admin 建题+用例 → 提交 A+B → 轮询终态 → list
    const httplib::Result health = cli.Get("/api/v1/health");
    nlohmann::json j;
    expect_true("health http 200", health && health -> status == 200);
    expect_true("health code 0", parse_res(health, j) && json_code(j) == 0);
    std::string mysql_status;
    expect_true("health mysql UP",
                read_data_string(j, "mysql", mysql_status) && mysql_status == "UP");

    const std::string username = unique_name("e2e_u_");
    std::int64_t user_id = 0;
    expect_true("register user", register_user_http(cli, username, user_id));

    std::string user_token;
    std::int64_t login_id = 0;
    expect_true("login user", login_token(cli, username, kPassword, user_token, login_id));
    expect_true("login id match", login_id == user_id);

    const httplib::Result me = cli.Get("/api/v1/users/me", auth_headers(user_token));
    expect_true("me code 0", parse_res(me, j) && json_code(j) == 0);
    std::int64_t me_id = 0;
    expect_true("me id match", read_data_obj_i64(j, "id", me_id) && me_id == user_id);

    nlohmann::json wrong;
    wrong["username"] = username;
    wrong["password"] = "wrong-pass";
    const httplib::Result bad_pw = post_json(cli, "/api/v1/auth/login", wrong, "");
    expect_true("wrong password 50001",
                parse_res(bad_pw, j) && json_code(j) == 50001);

    test_negative(cli, user_token);

    expect_true("promote admin", promote_admin(user_id));
    std::string admin_token;
    expect_true("relogin admin",
                login_token(cli, username, kPassword, admin_token, login_id));

    nlohmann::json problem;
    problem["title"] = unique_name("e2e_prob_");
    problem["difficulty"] = "EASY";
    problem["description"] = "A + B";
    problem["timeLimit"] = 2000;
    problem["memoryLimit"] = 262144;
    problem["visible"] = 1;
    const httplib::Result created = post_json(
        cli, "/api/v1/problems", problem, admin_token
    );
    std::int64_t problem_id = 0;
    expect_true("admin create problem",
                parse_res(created, j)
                && json_code(j) == 0
                && read_data_i64(j, problem_id)
                && problem_id > 0);
    expect_true("insert case 1 2 / 3", insert_case(problem_id, "1 2", "3", 1));

    const httplib::Result listed = cli.Get("/api/v1/problems?keyword=" + problem["title"].get<std::string>());
    expect_true("list contains problem",
                parse_res(listed, j) && json_code(j) == 0
                && records_contain_id(j, problem_id));

    const std::string get_path = "/api/v1/problems/" + std::to_string(problem_id);
    const httplib::Result got = cli.Get(get_path);
    expect_true("get problem code 0", parse_res(got, j) && json_code(j) == 0);
    int sample_ok = 0;
    if (j.contains("data") && j["data"].is_object()
        && j["data"].contains("samples") && j["data"]["samples"].is_array()
        && !j["data"]["samples"].empty()) {
        sample_ok = 1;
    }
    expect_true("get problem has sample", sample_ok);

    const std::string other_name = unique_name("e2e_o_");
    std::int64_t other_id = 0;
    expect_true("register other user", register_user_http(cli, other_name, other_id));
    std::string other_token;
    expect_true("login other user",
                login_token(cli, other_name, kPassword, other_token, other_id));

    nlohmann::json submit;
    submit["problemId"] = problem_id;
    submit["language"] = "CPP";
    submit["code"] = kAcCode;
    const httplib::Result submitted = post_json(
        cli, "/api/v1/submissions", submit, user_token
    );
    std::int64_t submission_id = 0;
    expect_true("submit A+B",
                parse_res(submitted, j)
                && json_code(j) == 0
                && read_data_obj_i64(j, "submissionId", submission_id)
                && submission_id > 0);

    const std::string peek_path = "/api/v1/submissions/" + std::to_string(submission_id);
    const httplib::Result peek = cli.Get(peek_path, auth_headers(other_token));
    expect_true("other user cannot see submission 40400",
                parse_res(peek, j) && json_code(j) == 40400);

    std::string status;
    int reached = 0;
    // 覆盖本机 g++ / Docker 编译超时（sandbox 编译上限 120s）
    for (int i = 0; i < 300; ++i) {
        const httplib::Result one = cli.Get(peek_path, auth_headers(user_token));
        if (parse_res(one, j) && json_code(j) == 0
            && read_data_string(j, "status", status)
            && is_terminal_status(status)) {
            reached = 1;
            break;
        }
        if (i > 0 && i % 10 == 0) {
            std::cout << "[...] submission status=" << status << " t=" << (i * 500) << "ms\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    expect_true("submission reached terminal", reached);
    expect_true("submission AC", status == "AC");

    const httplib::Result mine = cli.Get("/api/v1/submissions", auth_headers(user_token));
    expect_true("list my submissions",
                parse_res(mine, j) && json_code(j) == 0
                && records_contain_id(j, submission_id));
}

}  // namespace

int main() {
    MYSQL mysql;
    if (!nloj::common::start_mysql(mysql)) {
        std::cerr << "nl-api e2e failed: MySQL 未就绪（nloj / nloj123456 / nloj_db）\n";
        return EXIT_FAILURE;
    }
    mysql_close(&mysql);

    httplib::Server svr;
    nloj::api::register_http_routes(svr);

    int bound = 0;
    for (int port = 18080; port < 18090; ++port) {
        if (svr.bind_to_port("127.0.0.1", port)) {
            g_port = port;
            bound = 1;
            break;
        }
    }
    if (!bound) {
        std::cerr << "nl-api e2e failed: 18080-18089 均无法 bind\n";
        return EXIT_FAILURE;
    }

    std::thread listen_th([&svr] {
        svr.listen_after_bind();
    });
    std::thread worker(nloj::api::judge_worker_loop);
    worker.detach();

    httplib::Client cli("127.0.0.1", g_port);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(10, 0);
    if (!wait_health(cli)) {
        svr.stop();
        if (listen_th.joinable()) {
            listen_th.join();
        }
        std::cerr << "nl-api e2e failed: HTTP 服务未起来\n";
        return EXIT_FAILURE;
    }

    test_happy_path(cli);

    svr.stop();
    if (listen_th.joinable()) {
        listen_th.join();
    }

    if (g_failed) {
        std::cerr << "nl-api e2e tests failed（检查 MySQL / Docker 或 g++）\n";
        return EXIT_FAILURE;
    }
    std::cout << "nl-api e2e tests passed\n";
    return EXIT_SUCCESS;
}
