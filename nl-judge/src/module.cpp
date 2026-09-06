#include "nloj/judge/module.h"
#include "nloj/judge/sandbox.h"
#include "nloj/problem/module.h"
#include "nloj/common/mq.h"
#include "nloj/common/mysql.h"

#include <memory>
#include <string>
#include <vector>

namespace nloj::judge {
namespace {

// 判题用的一组用例（含隐藏点，不只 is_sample）。
struct JudgeCase {
    std::int64_t id;
    std::string input;
    std::string output;
};

}  // namespace

const char* module_name() {
    return "nl-judge";
}

int run_judge_task(std::int64_t submission_id) {
    // 读 submission 与题目用例 → 置 JUDGING → 沙箱单容器跑全部用例 → 比对输出 → UPDATE 终态

    if (submission_id <= 0) {
        return 0;
    }

    // mysql 初始化、连接
    nloj::common::MysqlConn conn;
    if (!conn.ok()) {
        return 0;
    }

    // 读 submission
    const std::string select_sql = "SELECT id, problem_id, language, code, status "
                                   "FROM submission WHERE id="
                                  + std::to_string(submission_id)
                                  + " LIMIT 1";
    MYSQL_RES* sub_result = nloj::common::query_select(conn.get(), select_sql);
    if (sub_result == nullptr) {
        return 0;
    }
    MYSQL_ROW sub_row = mysql_fetch_row(sub_result);
    if (sub_row == nullptr || sub_row[0] == nullptr || sub_row[1] == nullptr) {
        mysql_free_result(sub_result);
        return 0;  // 提交不存在
    }
    const std::int64_t problem_id = std::stoll(sub_row[1]);
    const std::string language = sub_row[2] != nullptr ? sub_row[2] : "";
    const std::string code = sub_row[3] != nullptr ? sub_row[3] : "";
    const std::string cur_status = sub_row[4] != nullptr ? sub_row[4] : "";
    mysql_free_result(sub_result);
    if (cur_status == "AC" || cur_status == "WA" || cur_status == "TLE" || cur_status == "MLE"
     || cur_status == "RE" || cur_status == "CE" || cur_status == "SYSTEM_ERROR") {
        return 1;  // 已出终态，重投递幂等
    }
    if (language.empty() || code.empty()) {
        return 0;
    }

    // 读题目时限 / 题型 / SPJ（判题不看 visible，已提交的隐藏题也要评）
    const nloj::problem::ProblemJudgeConfig cfg =
        nloj::problem::get_problem_judge_config(problem_id);
    if (cfg.id <= 0) {
        return 0;  // 题目不存在或已删
    }
    const int time_limit = cfg.time_limit;
    const int memory_limit = cfg.memory_limit;

    // 读全部用例（含隐藏点，不要只查 is_sample=1）
    const std::string case_sql = "SELECT id, input, output FROM problem_case WHERE problem_id="
                                + std::to_string(problem_id)
                                + " AND deleted=0 ORDER BY sort_order ASC, id ASC";
    MYSQL_RES* case_result = nloj::common::query_select(conn.get(), case_sql);
    if (case_result == nullptr) {
        return 0;
    }
    std::vector<JudgeCase> cases;
    while (MYSQL_ROW case_row = mysql_fetch_row(case_result)) {
        if (case_row[0] == nullptr) {
            continue;
        }
        JudgeCase item;
        item.id = std::stoll(case_row[0]);
        item.input = case_row[1] != nullptr ? case_row[1] : "";
        item.output = case_row[2] != nullptr ? case_row[2] : "";
        cases.push_back(item);
    }
    mysql_free_result(case_result);
    if (cases.empty()) {
        return 0;  // 没有用例无法判
    }

    // 置 JUDGING
    const std::string judging_sql = "UPDATE submission SET status='JUDGING' WHERE id="
                                   + std::to_string(submission_id);
    if (!nloj::common::query_exec(conn.get(), judging_sql)) {
        return 0;
    }

    // 沙箱跑完后写终态：time_used / memory_used 取本次运行实测值
    auto write_verdict = [&](const std::string& status,
                             int time_used,
                             int memory_used,
                             const std::string& info) {
        std::string clipped = info;
        if (clipped.size() > 500) {
            clipped.resize(500);
        }
        const std::string escaped_status = nloj::common::escape_sql(conn.get(), status);
        const std::string escaped_info = nloj::common::escape_sql(conn.get(), clipped);
        const std::string time_sql = time_used < 0 ? "NULL" : std::to_string(time_used);
        const std::string mem_sql = memory_used < 0 ? "NULL" : std::to_string(memory_used);
        const std::string update_sql = "UPDATE submission SET status='"
                                      + escaped_status
                                      + "', time_used="
                                      + time_sql
                                      + ", memory_used="
                                      + mem_sql
                                      + ", judge_info='"
                                      + escaped_info
                                      + "' WHERE id="
                                      + std::to_string(submission_id);
        return nloj::common::query_exec(conn.get(), update_sql) ? 1 : 0;
    };

    // 沙箱单容器跑完全部用例：容器内逐用例计时，wait4 记录真实内存峰值
    std::unique_ptr<JudgeSandbox> box = make_sandbox_for(language);
    SandboxJudgeRequest req;
    req.language = language;
    req.code = code;
    req.time_limit_ms = time_limit;
    req.memory_limit_kb = memory_limit;
    req.problem_type = cfg.problem_type.empty() ? "STANDARD" : cfg.problem_type;
    req.judge_mode = cfg.judge_mode.empty() ? "EXACT" : cfg.judge_mode;
    req.extra_code = cfg.extra_code;
    for (const auto& item : cases) {
        req.inputs.push_back(item.input);
        req.expecteds.push_back(item.output);
    }
    const SandboxJudgeResult result = box -> judge(req);
    if (result.status == "CE") {
        const int ok = write_verdict("CE", -1, -1, result.error_text);
        return ok;
    }
    if (result.status != "OK") {
        const std::string info = result.error_text.empty() ? "sandbox run failed" : result.error_text;
        const int ok = write_verdict("SYSTEM_ERROR", -1, -1, info);
        return ok;
    }

    // 逐用例核对运行结论与输出；首个失败用例即终态，AC 汇总最大耗时/内存
    int max_time = 0;
    int max_mem = -1;
    int case_no = 0;
    for (const auto& one : result.cases) {
        case_no = one.index;
        if (one.verdict != "OK") {
            // runner 的 SE 是框架内部错误，归一到库里既有的 SYSTEM_ERROR
            const std::string status = one.verdict == "SE" ? "SYSTEM_ERROR" : one.verdict;
            const std::string info = one.verdict + " on test case " + std::to_string(one.index);
            const int ok = write_verdict(status, one.time_used_ms, one.memory_used_kb, info);
            return ok;
        }
        if (static_cast<std::size_t>(one.index) > cases.size()) {
            return 0;  // 沙箱返回了多余的用例，视为异常
        }
        if (one.time_used_ms > max_time) {
            max_time = one.time_used_ms;
        }
        if (one.memory_used_kb > max_mem) {
            max_mem = one.memory_used_kb;
        }
        // STANDARD+EXACT 宿主侧比对；SPJ / 交互 / 通信由沙箱或 checker 给出 WA
        if (req.problem_type == "STANDARD" && req.judge_mode == "EXACT"
         && !judge_outputs_match(cases[static_cast<std::size_t>(one.index - 1)].output, one.stdout_text)) {
            const std::string info = "WA on test case " + std::to_string(one.index);
            const int ok = write_verdict("WA", one.time_used_ms, one.memory_used_kb, info);
            return ok;
        }
    }
    if (case_no != static_cast<int>(cases.size())) {
        return 0;  // runner 提前停止却没带失败用例，视为异常
    }

    const std::string ac_info = "All " + std::to_string(cases.size()) + " test cases passed";
    const int ok = write_verdict("AC", max_time, max_mem, ac_info);
    return ok;
}

int reclaim_stale_judging(int older_than_sec) {
    // 查出超时 JUDGING / PENDING → JUDGING 乐观改回 PENDING → 重新入队

    if (older_than_sec <= 0) {
        return 0;
    }

    nloj::common::MysqlConn conn;
    if (!conn.ok()) {
        return 0;
    }

    const std::string select_sql = "SELECT id, problem_id, language, status FROM submission "
                                   "WHERE status IN ('JUDGING','PENDING') "
                                   "AND update_time < DATE_SUB(NOW(), INTERVAL "
                                  + std::to_string(older_than_sec)
                                  + " SECOND)";
    MYSQL_RES* result = nloj::common::query_select(conn.get(), select_sql);
    if (result == nullptr) {
        return 0;
    }

    struct StaleRow {
        std::int64_t id;
        std::int64_t problem_id;
        std::string language;
        std::string status;
    };
    std::vector<StaleRow> rows;
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        if (row[0] == nullptr || row[1] == nullptr || row[2] == nullptr || row[3] == nullptr) {
            continue;
        }
        StaleRow one;
        one.id = std::stoll(row[0]);
        one.problem_id = std::stoll(row[1]);
        one.language = row[2];
        one.status = row[3];
        rows.push_back(one);
    }
    mysql_free_result(result);

    int reclaimed = 0;
    for (const auto& one : rows) {
        if (one.status == "JUDGING") {
            const std::string update_sql = "UPDATE submission SET status='PENDING', "
                                           "judge_info='reclaimed stale JUDGING' WHERE id="
                                          + std::to_string(one.id)
                                          + " AND status='JUDGING'";
            if (!nloj::common::query_exec(conn.get(), update_sql)) {
                continue;
            }
            if (mysql_affected_rows(conn.get()) == 0) {
                continue;
            }
        }
        nloj::common::JudgeTaskMessage task;
        task.submission_id = one.id;
        task.problem_id = one.problem_id;
        task.language = one.language;
        if (nloj::common::publish_judge_task(task)) {
            ++reclaimed;
        }
    }
    return reclaimed;
}

}  // namespace nloj::judge
