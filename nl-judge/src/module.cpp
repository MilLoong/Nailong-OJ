#include "nloj/judge/module.h"
#include "nloj/judge/sandbox.h"
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
    // 读 submission 与题目用例 → 置 JUDGING → 沙箱编译运行 → 比对输出 → UPDATE 终态

    if (submission_id <= 0) {
        return 0;
    }

    // mysql 初始化、连接
    MYSQL mysql;
    if (!nloj::common::start_mysql(mysql)) {
        return 0;
    }

    // 读 submission
    const std::string select_sql = "SELECT id, problem_id, language, code, status "
                                   "FROM submission WHERE id="
                                  + std::to_string(submission_id)
                                  + " LIMIT 1";
    MYSQL_RES* sub_result = nloj::common::query_select(&mysql, select_sql);
    if (sub_result == nullptr) {
        mysql_close(&mysql);
        return 0;
    }
    MYSQL_ROW sub_row = mysql_fetch_row(sub_result);
    if (sub_row == nullptr || sub_row[0] == nullptr || sub_row[1] == nullptr) {
        mysql_free_result(sub_result);
        mysql_close(&mysql);
        return 0;  // 提交不存在
    }
    const std::int64_t problem_id = std::stoll(sub_row[1]);
    const std::string language = sub_row[2] != nullptr ? sub_row[2] : "";
    const std::string code = sub_row[3] != nullptr ? sub_row[3] : "";
    mysql_free_result(sub_result);
    if (language.empty() || code.empty()) {
        mysql_close(&mysql);
        return 0;
    }

    // 读题目时限（判题不看 visible，已提交的隐藏题也要评）
    const std::string problem_sql = "SELECT time_limit, memory_limit FROM problem WHERE id="
                                   + std::to_string(problem_id)
                                   + " AND deleted=0 LIMIT 1";
    MYSQL_RES* problem_result = nloj::common::query_select(&mysql, problem_sql);
    if (problem_result == nullptr) {
        mysql_close(&mysql);
        return 0;
    }
    MYSQL_ROW problem_row = mysql_fetch_row(problem_result);
    if (problem_row == nullptr || problem_row[0] == nullptr || problem_row[1] == nullptr) {
        mysql_free_result(problem_result);
        mysql_close(&mysql);
        return 0;  // 题目不存在或已删
    }
    const int time_limit = std::stoi(problem_row[0]);
    const int memory_limit = std::stoi(problem_row[1]);
    mysql_free_result(problem_result);

    // 读全部用例（含隐藏点，不要只查 is_sample=1）
    const std::string case_sql = "SELECT id, input, output FROM problem_case WHERE problem_id="
                                + std::to_string(problem_id)
                                + " AND deleted=0 ORDER BY sort_order ASC, id ASC";
    MYSQL_RES* case_result = nloj::common::query_select(&mysql, case_sql);
    if (case_result == nullptr) {
        mysql_close(&mysql);
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
        mysql_close(&mysql);
        return 0;  // 没有用例无法判
    }

    // 置 JUDGING
    const std::string judging_sql = "UPDATE submission SET status='JUDGING' WHERE id="
                                   + std::to_string(submission_id);
    if (!nloj::common::query_exec(&mysql, judging_sql)) {
        mysql_close(&mysql);
        return 0;
    }

    // 沙箱编译运行 → 比对输出 → UPDATE 终态
    auto write_verdict = [&](const std::string& status, int time_used, const std::string& info) {
        std::string clipped = info;
        if (clipped.size() > 500) {
            clipped.resize(500);
        }
        const std::string escaped_status = nloj::common::escape_sql(&mysql, status);
        const std::string escaped_info = nloj::common::escape_sql(&mysql, clipped);
        const std::string time_sql = time_used < 0 ? "NULL" : std::to_string(time_used);
        const std::string update_sql = "UPDATE submission SET status='"
                                      + escaped_status
                                      + "', time_used="
                                      + time_sql
                                      + ", memory_used=NULL, judge_info='"
                                      + escaped_info
                                      + "' WHERE id="
                                      + std::to_string(submission_id);
        return nloj::common::query_exec(&mysql, update_sql) ? 1 : 0;
    };

    std::unique_ptr<JudgeSandbox> box = make_sandbox();
    SandboxRequest req;
    req.language = language;
    req.code = code;
    req.time_limit_ms = time_limit;
    req.memory_limit_kb = memory_limit;
    req.compile_only = 1;
    const SandboxResult compiled = box -> execute(req);
    if (compiled.verdict != "OK") {
        const std::string status = compiled.verdict == "CE" ? "CE" : "SYSTEM_ERROR";
        const std::string info = compiled.verdict == "CE" ? compiled.stderr_text : "sandbox compile failed";
        const int ok = write_verdict(status, -1, info);
        mysql_close(&mysql);
        return ok;
    }

    int max_time = 0;
    int case_no = 1;
    for (const auto& item : cases) {
        req.compile_only = 0;
        req.stdin_data = item.input;
        const SandboxResult ran = box -> execute(req);
        if (ran.time_used_ms > max_time) {
            max_time = ran.time_used_ms;
        }
        if (ran.verdict != "OK") {
            const std::string info = ran.verdict + " on test case " + std::to_string(case_no);
            const int ok = write_verdict(ran.verdict, max_time, info);
            mysql_close(&mysql);
            return ok;
        }
        if (!judge_outputs_match(item.output, ran.stdout_text)) {
            const std::string info = "WA on test case " + std::to_string(case_no);
            const int ok = write_verdict("WA", max_time, info);
            mysql_close(&mysql);
            return ok;
        }
        ++case_no;
    }

    const std::string ac_info = "All " + std::to_string(cases.size()) + " test cases passed";
    const int ok = write_verdict("AC", max_time, ac_info);
    mysql_close(&mysql);
    return ok;
}

}  // namespace nloj::judge
