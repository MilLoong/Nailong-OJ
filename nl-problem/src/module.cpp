#include "nloj/problem/module.h"
#include "nloj/common/mysql.h"
#include "nloj/common/redis.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace nloj::problem {
namespace {

constexpr const char* kNilCache = "__nil__";
constexpr int kCacheTtlSec = 1800;       // 30min，再按 id 抖动防雪崩
constexpr int kNilTtlSec = 300;          // 穿透：空值短 TTL
constexpr int kLockTtlSec = 5;
constexpr int kL1TtlSec = 60;
constexpr std::size_t kL1Cap = 64;

struct L1Entry {
    ProblemDetail detail;
    std::chrono::steady_clock::time_point expire;
};

std::mutex g_l1_mu;
std::unordered_map<std::int64_t, L1Entry> g_l1;
std::atomic<std::int64_t> g_stat_total{0};
std::atomic<std::int64_t> g_stat_l1{0};
std::atomic<std::int64_t> g_stat_redis{0};
std::atomic<std::int64_t> g_stat_mysql{0};

std::string problem_key(std::int64_t id) {
    return "nloj:problem:" + std::to_string(id);
}

std::string problem_lock_key(std::int64_t id) {
    return "nloj:problem:lock:" + std::to_string(id);
}

int cache_ttl_sec(std::int64_t id) {
    return kCacheTtlSec + static_cast<int>(id % 601);
}

int l1_get(std::int64_t id, ProblemDetail& out) {
    std::lock_guard<std::mutex> lock(g_l1_mu);
    const auto it = g_l1.find(id);
    if (it == g_l1.end()) {
        return 0;
    }
    if (std::chrono::steady_clock::now() >= it -> second.expire) {
        g_l1.erase(it);
        return 0;
    }
    out = it -> second.detail;
    return 1;
}

void l1_put(std::int64_t id, const ProblemDetail& detail) {
    std::lock_guard<std::mutex> lock(g_l1_mu);
    if (g_l1.size() >= kL1Cap) {
        g_l1.erase(g_l1.begin());
    }
    L1Entry entry;
    entry.detail = detail;
    entry.expire = std::chrono::steady_clock::now() + std::chrono::seconds(kL1TtlSec);
    g_l1[id] = entry;
}

void l1_erase(std::int64_t id) {
    std::lock_guard<std::mutex> lock(g_l1_mu);
    g_l1.erase(id);
}

std::string encode_problem(const ProblemDetail& p) {
    nlohmann::json j;
    j["id"] = p.id;
    j["title"] = p.title;
    j["difficulty"] = p.difficulty;
    j["description"] = p.description;
    j["timeLimit"] = p.time_limit;
    j["memoryLimit"] = p.memory_limit;
    j["visible"] = p.visible;
    j["createTime"] = p.create_time;
    j["problemType"] = p.problem_type.empty() ? "STANDARD" : p.problem_type;
    j["judgeMode"] = p.judge_mode.empty() ? "EXACT" : p.judge_mode;
    nlohmann::json samples = nlohmann::json::array();
    for (const auto& s : p.samples) {
        nlohmann::json one;
        one["id"] = s.id;
        one["input"] = s.input;
        one["output"] = s.output;
        samples.push_back(one);
    }
    j["samples"] = samples;
    return j.dump();
}

int decode_problem(const std::string& raw, ProblemDetail& out) {
    const nlohmann::json j = nlohmann::json::parse(raw, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("id")) {
        return 0;
    }
    ProblemDetail p;
    p.id = j["id"].is_number_integer() ? j["id"].get<std::int64_t>() : 0;
    p.title = j.value("title", "");
    p.difficulty = j.value("difficulty", "");
    p.description = j.value("description", "");
    p.time_limit = j.value("timeLimit", 0);
    p.memory_limit = j.value("memoryLimit", 0);
    p.visible = j.value("visible", 0);
    p.create_time = j.value("createTime", "");
    p.problem_type = j.value("problemType", "STANDARD");
    p.judge_mode = j.value("judgeMode", "EXACT");
    if (j.contains("samples") && j["samples"].is_array()) {
        for (const auto& one : j["samples"]) {
            ProblemSample s;
            s.id = one.value("id", static_cast<std::int64_t>(0));
            s.input = one.value("input", "");
            s.output = one.value("output", "");
            p.samples.push_back(s);
        }
    }
    out = p;
    return 1;
}

void write_problem_cache(std::int64_t id, const ProblemDetail& detail) {
    if (!nloj::common::redis_using()) {
        return;
    }
    if (detail.id <= 0) {
        nloj::common::redis_set_ex(problem_key(id), kNilCache, kNilTtlSec);
        return;
    }
    nloj::common::redis_set_ex(problem_key(id), encode_problem(detail), cache_ttl_sec(id));
}

void invalidate_problem_cache(std::int64_t id) {
    l1_erase(id);
    nloj::common::redis_del(problem_key(id));
}

int column_exists(MYSQL* mysql, const char* table, const char* column) {
    const std::string sql = "SELECT COUNT(*) FROM information_schema.COLUMNS "
                            "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='"
                           + std::string(table)
                           + "' AND COLUMN_NAME='"
                           + std::string(column) + "'";
    MYSQL_RES* result = nloj::common::query_select(mysql, sql);
    if (result == nullptr) {
        return 0;
    }
    MYSQL_ROW row = mysql_fetch_row(result);
    const int ok = (row != nullptr && row[0] != nullptr && std::stoll(row[0]) > 0) ? 1 : 0;
    mysql_free_result(result);
    return ok;
}

void ensure_problem_schema(MYSQL* mysql) {
    // 已有库补题型 / SPJ 列；新库 schema.sql 已带这些列
    if (mysql == nullptr) {
        return;
    }
    if (!column_exists(mysql, "problem", "problem_type")) {
        nloj::common::query_exec(
            mysql,
            "ALTER TABLE problem ADD COLUMN problem_type VARCHAR(16) NOT NULL DEFAULT 'STANDARD'"
        );
    }
    if (!column_exists(mysql, "problem", "judge_mode")) {
        nloj::common::query_exec(
            mysql,
            "ALTER TABLE problem ADD COLUMN judge_mode VARCHAR(16) NOT NULL DEFAULT 'EXACT'"
        );
    }
    if (!column_exists(mysql, "problem", "extra_code")) {
        nloj::common::query_exec(
            mysql,
            "ALTER TABLE problem ADD COLUMN extra_code MEDIUMTEXT NULL"
        );
    }
}

int valid_problem_type(const std::string& type) {
    return type == "STANDARD" || type == "INTERACTIVE" || type == "COMMUNICATION" ? 1 : 0;
}

int valid_judge_mode(const std::string& mode) {
    return mode == "EXACT" || mode == "SPJ" ? 1 : 0;
}

int extra_code_required(const std::string& type, const std::string& mode) {
    if (type == "INTERACTIVE" || type == "COMMUNICATION") {
        return 1;
    }
    if (mode == "SPJ") {
        return 1;
    }
    return 0;
}

int normalize_judge_fields(CreateProblemRequest& req) {
    if (req.problem_type.empty()) {
        req.problem_type = "STANDARD";
    }
    if (req.judge_mode.empty()) {
        req.judge_mode = "EXACT";
    }
    if (!valid_problem_type(req.problem_type) || !valid_judge_mode(req.judge_mode)) {
        return 0;
    }
    if (extra_code_required(req.problem_type, req.judge_mode) && req.extra_code.empty()) {
        return 0;
    }
    if (req.extra_code.size() > 64 * 1024) {
        return 0;
    }
    return 1;
}

}  // namespace

const char* module_name() {
    return "nl-problem";
}

ProblemCacheStats problem_cache_stats() {
    ProblemCacheStats st;
    st.total = g_stat_total.load(std::memory_order_relaxed);
    st.l1_hit = g_stat_l1.load(std::memory_order_relaxed);
    st.redis_hit = g_stat_redis.load(std::memory_order_relaxed);
    st.mysql_load = g_stat_mysql.load(std::memory_order_relaxed);
    return st;
}

ProblemPage list_problems(std::int64_t page_num,
                          std::int64_t page_size,
                          const std::string& difficulty,
                          const std::string& keyword,
                          std::int64_t last_id) {
    // 拼过滤条件 -> COUNT total -> 分页 SELECT（last_id>0 走 keyset）-> 填 ProblemPage

    // 校验分页（页码从 1 起，每页最多 100）
    if (page_num < 1 || page_size < 1 || page_size > 100 || last_id < 0) {
        return {};
    }

    // mysql 初始化、连接
    nloj::common::MysqlConn conn;
    if (!conn.ok()) {
        return {};
    }
    ensure_problem_schema(conn.get());

    // 拼过滤条件（列表只看未删且可见；difficulty / keyword 可选）
    std::string where_sql = " WHERE deleted=0 AND visible=1";
    if (!difficulty.empty()) {
        const std::string escaped_difficulty = nloj::common::escape_sql(conn.get(), difficulty);
        where_sql += " AND difficulty='" + escaped_difficulty + "'";
    }
    if (!keyword.empty()) {
        const std::string escaped_keyword = nloj::common::escape_sql(conn.get(), keyword);
        where_sql += " AND title LIKE '%" + escaped_keyword + "%'";
    }

    // COUNT total
    const std::string count_sql = "SELECT COUNT(*) FROM problem"
                                 + where_sql;
    MYSQL_RES* count_result = nloj::common::query_select(conn.get(), count_sql);
    if (count_result == nullptr) {
        return {};
    }
    MYSQL_ROW count_row = mysql_fetch_row(count_result);
    if (count_row == nullptr || count_row[0] == nullptr) {
        mysql_free_result(count_result);
        return {};
    }
    const std::int64_t total = std::stoll(count_row[0]);
    mysql_free_result(count_result);

    // SELECT 分页列表（不含 description 题面）
    // last_id>0 用 keyset：只取 id 比游标小的最新 page_size 条，深翻页不再扫描 OFFSET 行
    std::string list_where = where_sql;
    std::string list_tail;
    if (last_id > 0) {
        list_where += " AND id < " + std::to_string(last_id);
        list_tail = " ORDER BY id DESC LIMIT "
                  + std::to_string(page_size);
    } else {
        const std::int64_t offset = (page_num - 1) * page_size;
        list_tail = " ORDER BY id DESC LIMIT "
                  + std::to_string(page_size)
                  + " OFFSET "
                  + std::to_string(offset);
    }
    const std::string list_sql = "SELECT id, title, difficulty, time_limit, memory_limit, visible, "
                                "create_time, problem_type, judge_mode FROM problem"
                                + list_where
                                + list_tail;
    MYSQL_RES* list_result = nloj::common::query_select(conn.get(), list_sql);
    if (list_result == nullptr) {
        return {};
    }

    // 填 ProblemPage
    ProblemPage page;
    page.page_num = page_num;
    page.page_size = page_size;
    page.total = total;
    while (MYSQL_ROW row = mysql_fetch_row(list_result)) {
        if (row[0] == nullptr || row[1] == nullptr) {
            continue;
        }
        ProblemSummary item;
        item.id = std::stoll(row[0]);
        item.title = row[1];
        item.difficulty = row[2] != nullptr ? row[2] : "";
        item.time_limit = row[3] != nullptr ? std::stoi(row[3]) : 0;
        item.memory_limit = row[4] != nullptr ? std::stoi(row[4]) : 0;
        item.visible = row[5] != nullptr ? std::stoi(row[5]) : 0;
        item.create_time = row[6] != nullptr ? row[6] : "";
        item.problem_type = row[7] != nullptr ? row[7] : "STANDARD";
        item.judge_mode = row[8] != nullptr ? row[8] : "EXACT";
        page.records.push_back(item);
    }
    mysql_free_result(list_result);
    return page;
}


ProblemDetail load_problem_from_db(std::int64_t id) {
    // 按 id 查题目 -> 查 is_sample=1 的用例 -> 填 ProblemDetail

    // mysql 初始化、连接
    nloj::common::MysqlConn conn;
    if (!conn.ok()) {
        return {};
    }
    ensure_problem_schema(conn.get());

    // 按 id 查题目（未删除且可见）
    const std::string select_sql = "SELECT id, title, difficulty, description, "
                                   "time_limit, memory_limit, visible, create_time, "
                                   "problem_type, judge_mode "
                                   "FROM problem WHERE deleted=0 AND visible=1 AND id="
                                  + std::to_string(id)
                                  + " LIMIT 1";
    MYSQL_RES* problem_result = nloj::common::query_select(conn.get(), select_sql);
    if (problem_result == nullptr) {
        return {};
    }
    MYSQL_ROW row = mysql_fetch_row(problem_result);
    if (row == nullptr || row[0] == nullptr) {
        mysql_free_result(problem_result);
        return {};  // 不存在或不可见
    }

    // row[0]=id … row[7]=create_time，row[8]/[9] 题型与比对
    ProblemDetail problem;
    problem.id = std::stoll(row[0]);
    problem.title = row[1] != nullptr ? row[1] : "";
    problem.difficulty = row[2] != nullptr ? row[2] : "";
    problem.description = row[3] != nullptr ? row[3] : "";
    problem.time_limit = row[4] != nullptr ? std::stoi(row[4]) : 0;
    problem.memory_limit = row[5] != nullptr ? std::stoi(row[5]) : 0;
    problem.visible = row[6] != nullptr ? std::stoi(row[6]) : 0;
    problem.create_time = row[7] != nullptr ? row[7] : "";
    problem.problem_type = row[8] != nullptr ? row[8] : "STANDARD";
    problem.judge_mode = row[9] != nullptr ? row[9] : "EXACT";
    mysql_free_result(problem_result);

    // 查样例用例（仅 is_sample=1，隐藏用例不给前端）
    const std::string case_sql = "SELECT id, input, output FROM problem_case WHERE problem_id="
                                + std::to_string(id)
                                + " AND is_sample=1 AND deleted=0 ORDER BY sort_order ASC, id ASC";
    MYSQL_RES* case_result = nloj::common::query_select(conn.get(), case_sql);
    if (case_result == nullptr) {
        return {};
    }
    while (MYSQL_ROW case_row = mysql_fetch_row(case_result)) {
        if (case_row[0] == nullptr) {
            continue;
        }
        ProblemSample sample;
        sample.id = std::stoll(case_row[0]);
        sample.input = case_row[1] != nullptr ? case_row[1] : "";
        sample.output = case_row[2] != nullptr ? case_row[2] : "";
        problem.samples.push_back(sample);
    }
    mysql_free_result(case_result);
    return problem;
}

ProblemDetail get_problem(std::int64_t id, int skip_cache) {
    // L1 -> Redis String -> 互斥锁重建 -> MySQL -> 回填（空值短 TTL）

    if (id <= 0) {
        return {};
    }

    g_stat_total.fetch_add(1, std::memory_order_relaxed);
    const int bypass = skip_cache ? 1 : 0;

    ProblemDetail cached;
    std::string raw;
    if (!bypass) {
        if (l1_get(id, cached)) {
            g_stat_l1.fetch_add(1, std::memory_order_relaxed);
            return cached;
        }

        if (nloj::common::redis_get(problem_key(id), raw)) {
            if (raw == kNilCache) {
                g_stat_redis.fetch_add(1, std::memory_order_relaxed);
                l1_put(id, {});
                return {};
            }
            ProblemDetail from_redis;
            if (decode_problem(raw, from_redis)) {
                g_stat_redis.fetch_add(1, std::memory_order_relaxed);
                l1_put(id, from_redis);
                return from_redis;
            }
        }

        const int locked = nloj::common::redis_set_nx_ex(problem_lock_key(id), "1", kLockTtlSec);
        if (!locked) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if (nloj::common::redis_get(problem_key(id), raw)) {
                if (raw == kNilCache) {
                    g_stat_redis.fetch_add(1, std::memory_order_relaxed);
                    return {};
                }
                ProblemDetail from_redis;
                if (decode_problem(raw, from_redis)) {
                    g_stat_redis.fetch_add(1, std::memory_order_relaxed);
                    l1_put(id, from_redis);
                    return from_redis;
                }
            }
        }

        g_stat_mysql.fetch_add(1, std::memory_order_relaxed);
        const ProblemDetail detail = load_problem_from_db(id);
        write_problem_cache(id, detail);
        if (locked) {
            nloj::common::redis_del(problem_lock_key(id));
        }
        l1_put(id, detail);
        return detail;
    }

    g_stat_mysql.fetch_add(1, std::memory_order_relaxed);
    return load_problem_from_db(id);
}

std::int64_t create_problem(const CreateProblemRequest& req, nloj::common::AppError* err) {
    // 校验字段 -> INSERT problem -> 返回 insert_id

    CreateProblemRequest norm = req;
    if (norm.title.empty() || norm.title.size() > 256 || norm.description.empty()
     || (norm.difficulty != "EASY" && norm.difficulty != "MEDIUM" && norm.difficulty != "HARD")
     || norm.time_limit <= 0 || norm.memory_limit <= 0
     || (norm.visible != 0 && norm.visible != 1)
     || !normalize_judge_fields(norm)) {
        nloj::common::set_error(err, nloj::common::AppError::InvalidArgument);
        return -1;
    }

    nloj::common::MysqlConn conn;
    if (!conn.ok()) {
        nloj::common::set_error(err, nloj::common::AppError::Database);
        return -1;
    }
    ensure_problem_schema(conn.get());

    // INSERT problem（用例可后续再插 problem_case）
    const std::string escaped_title = nloj::common::escape_sql(conn.get(), norm.title);
    const std::string escaped_difficulty = nloj::common::escape_sql(conn.get(), norm.difficulty);
    const std::string escaped_description = nloj::common::escape_sql(conn.get(), norm.description);
    const std::string escaped_type = nloj::common::escape_sql(conn.get(), norm.problem_type);
    const std::string escaped_mode = nloj::common::escape_sql(conn.get(), norm.judge_mode);
    const std::string escaped_extra = nloj::common::escape_sql(conn.get(), norm.extra_code);
    const std::string insert_sql = "INSERT INTO problem (title, difficulty, description, "
                                   "time_limit, memory_limit, visible, problem_type, judge_mode, extra_code) VALUES ('"
                                  + escaped_title + "', '"
                                  + escaped_difficulty + "', '"
                                  + escaped_description + "', "
                                  + std::to_string(norm.time_limit) + ", "
                                  + std::to_string(norm.memory_limit) + ", "
                                  + std::to_string(norm.visible) + ", '"
                                  + escaped_type + "', '"
                                  + escaped_mode + "', '"
                                  + escaped_extra + "')";
    if (!nloj::common::query_exec(conn.get(), insert_sql)) {
        nloj::common::set_error(err, nloj::common::AppError::Database);
        return -1;
    }

    const std::int64_t problem_id = static_cast<std::int64_t>(mysql_insert_id(conn.get()));
    nloj::common::set_error(err, nloj::common::AppError::Ok);
    return problem_id;
}

int update_problem(std::int64_t id, const CreateProblemRequest& req, nloj::common::AppError* err) {
    // 校验字段 -> 按 id UPDATE -> 返回是否影响到行

    // 校验字段
    CreateProblemRequest norm = req;
    if (id <= 0
     || norm.title.empty() || norm.title.size() > 256 || norm.description.empty()
     || (norm.difficulty != "EASY" && norm.difficulty != "MEDIUM" && norm.difficulty != "HARD")
     || norm.time_limit <= 0 || norm.memory_limit <= 0
     || (norm.visible != 0 && norm.visible != 1)
     || !normalize_judge_fields(norm)) {
        nloj::common::set_error(err, nloj::common::AppError::InvalidArgument);
        return 0;
    }

    nloj::common::MysqlConn conn;
    if (!conn.ok()) {
        nloj::common::set_error(err, nloj::common::AppError::Database);
        return 0;
    }
    ensure_problem_schema(conn.get());

    // 按 id UPDATE
    const std::string escaped_title = nloj::common::escape_sql(conn.get(), norm.title);
    const std::string escaped_difficulty = nloj::common::escape_sql(conn.get(), norm.difficulty);
    const std::string escaped_description = nloj::common::escape_sql(conn.get(), norm.description);
    const std::string escaped_type = nloj::common::escape_sql(conn.get(), norm.problem_type);
    const std::string escaped_mode = nloj::common::escape_sql(conn.get(), norm.judge_mode);
    const std::string escaped_extra = nloj::common::escape_sql(conn.get(), norm.extra_code);
    const std::string update_sql = "UPDATE problem SET title='"
                                  + escaped_title
                                  + "', difficulty='"
                                  + escaped_difficulty
                                  + "', description='"
                                  + escaped_description
                                  + "', time_limit="
                                  + std::to_string(norm.time_limit)
                                  + ", memory_limit="
                                  + std::to_string(norm.memory_limit)
                                  + ", visible="
                                  + std::to_string(norm.visible)
                                  + ", problem_type='"
                                  + escaped_type
                                  + "', judge_mode='"
                                  + escaped_mode
                                  + "', extra_code='"
                                  + escaped_extra
                                  + "' WHERE id="
                                  + std::to_string(id)
                                  + " AND deleted=0";
    if (!nloj::common::query_exec(conn.get(), update_sql)) {
        nloj::common::set_error(err, nloj::common::AppError::Database);
        return 0;
    }

    const int ok = mysql_affected_rows(conn.get()) > 0 ? 1 : 0;  // 0 行：id 不存在或已删
    if (!ok) {
        nloj::common::set_error(err, nloj::common::AppError::NotFound);
    } else {
        nloj::common::set_error(err, nloj::common::AppError::Ok);
    }
    if (ok) {
        invalidate_problem_cache(id);
    }
    return ok;
}

ProblemJudgeConfig get_problem_judge_config(std::int64_t id) {
    // 判题读配置（不看 visible；含 extra_code）
    if (id <= 0) {
        return {};
    }
    nloj::common::MysqlConn conn;
    if (!conn.ok()) {
        return {};
    }
    ensure_problem_schema(conn.get());
    const std::string select_sql = "SELECT id, time_limit, memory_limit, problem_type, "
                                   "judge_mode, extra_code FROM problem WHERE id="
                                  + std::to_string(id)
                                  + " AND deleted=0 LIMIT 1";
    MYSQL_RES* result = nloj::common::query_select(conn.get(), select_sql);
    if (result == nullptr) {
        return {};
    }
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row == nullptr || row[0] == nullptr) {
        mysql_free_result(result);
        return {};
    }
    ProblemJudgeConfig cfg;
    cfg.id = std::stoll(row[0]);
    cfg.time_limit = row[1] != nullptr ? std::stoi(row[1]) : 0;
    cfg.memory_limit = row[2] != nullptr ? std::stoi(row[2]) : 0;
    cfg.problem_type = row[3] != nullptr ? row[3] : "STANDARD";
    cfg.judge_mode = row[4] != nullptr ? row[4] : "EXACT";
    cfg.extra_code = row[5] != nullptr ? row[5] : "";
    mysql_free_result(result);
    return cfg;
}

}  // namespace nloj::problem
