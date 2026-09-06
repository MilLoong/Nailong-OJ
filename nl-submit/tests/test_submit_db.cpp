// nl-submit 提交 / 权限 / 列表联调（需要本机 MySQL：nloj / nloj123456 / nloj_db）。
#include "nloj/common/mq.h"
#include "nloj/common/mysql.h"
#include "nloj/problem/module.h"
#include "nloj/submit/module.h"

#include <ctime>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_failed = 0;  // 任一条失败则置 1
int g_name_seq = 0;

// 打印 [PASS]/[FAIL]；失败时记下 g_failed。
void expect_true(const char* name, int ok) {
    if (ok) {
        std::cout << "[PASS] " << name << '\n';
    } else {
        std::cout << "[FAIL] " << name << '\n';
        g_failed = 1;
    }
}

// 唯一名字，避免撞 uk_username / 标题。
std::string unique_name(const char* prefix) {
    ++g_name_seq;
    return std::string(prefix) + std::to_string(std::time(nullptr)) + "_"
           + std::to_string(g_name_seq);
}

// 插入测试用户，成功返回 id；失败返回 -1。
std::int64_t insert_user(const std::string& username) {
    MYSQL mysql;
    if (!nloj::common::start_mysql(mysql)) {
        return -1;
    }
    const std::string escaped = nloj::common::escape_sql(&mysql, username);
    const std::string sql = "INSERT INTO `user` (username, password_hash, role) VALUES ('"
                           + escaped
                           + "', 'ut_hash', 'user')";
    if (!nloj::common::query_exec(&mysql, sql)) {
        mysql_close(&mysql);
        return -1;
    }
    const std::int64_t id = static_cast<std::int64_t>(mysql_insert_id(&mysql));
    mysql_close(&mysql);
    return id;
}

// 建一道可见/隐藏题，给提交当外键。
std::int64_t make_problem(int visible) {
    nloj::problem::CreateProblemRequest req;
    req.title = unique_name("ut_sub_prob_");
    req.difficulty = "EASY";
    req.description = "submit module unit test";
    req.time_limit = 1000;
    req.memory_limit = 262144;
    req.visible = visible;
    return nloj::problem::create_problem(req);
}

// 排空进程内队列，避免上次用例残留。
void drain_judge_queue() {
    nloj::common::JudgeTaskMessage dump;
    while (nloj::common::try_pop_judge_task(dump)) {
        nloj::common::ack_judge_task(dump);
    }
}

int page_contains(const nloj::submit::SubmissionPage& page, std::int64_t id) {
    for (const auto& item : page.records) {
        if (item.id == id) {
            return 1;
        }
    }
    return 0;
}

// 非法 create -> 合法 create + MQ -> get 本人/他人/admin -> list 过滤。
void test_submit_flow() {
    drain_judge_queue();

    const std::int64_t user_a = insert_user(unique_name("ut_sub_a_"));
    const std::int64_t user_b = insert_user(unique_name("ut_sub_b_"));
    expect_true("insert user A", user_a > 0);
    expect_true("insert user B", user_b > 0);

    const std::int64_t visible_id = make_problem(1);
    const std::int64_t hidden_id = make_problem(0);
    expect_true("create visible problem", visible_id > 0);
    expect_true("create hidden problem", hidden_id > 0);

    const std::string code = "int main() { return 0; }";
    expect_true("create rejects empty code",
                nloj::submit::create_submission(user_a, visible_id, "CPP", "") < 0);
    expect_true("create rejects GO",
                nloj::submit::create_submission(user_a, visible_id, "GO", code) < 0);
    expect_true("create rejects hidden problem",
                nloj::submit::create_submission(user_a, hidden_id, "CPP", code) < 0);

    const std::int64_t sid = nloj::submit::create_submission(user_a, visible_id, "CPP", code);
    expect_true("create_submission returns id", sid > 0);

    nloj::common::JudgeTaskMessage task;
    expect_true("mq has judge task", nloj::common::try_pop_judge_task(task));
    expect_true("mq submission_id match", task.submission_id == sid);
    expect_true("mq problem_id match", task.problem_id == visible_id);
    expect_true("mq language CPP", task.language == "CPP");
    expect_true("create accepts JAVA",
                nloj::submit::create_submission(user_a, visible_id, "JAVA", code) > 0);

    const nloj::submit::SubmissionDetail own =
        nloj::submit::get_submission(sid, user_a, "user");
    expect_true("owner get id match", own.id == sid);
    expect_true("owner get code match", own.code == code);
    expect_true("owner get PENDING", own.status == "PENDING");
    expect_true("owner get time_used null", own.time_used == -1);

    const nloj::submit::SubmissionDetail other =
        nloj::submit::get_submission(sid, user_b, "user");
    expect_true("other user cannot get", other.id == 0);

    const nloj::submit::SubmissionDetail admin =
        nloj::submit::get_submission(sid, user_b, "admin");
    expect_true("admin can get", admin.id == sid);
    expect_true("admin get code match", admin.code == code);

    const nloj::submit::SubmissionPage mine =
        nloj::submit::list_my_submissions(user_a, 1, 20, 0, "");
    expect_true("list mine contains new", page_contains(mine, sid));
    int code_empty = 1;
    for (const auto& item : mine.records) {
        if (item.id == sid && !item.code.empty()) {
            code_empty = 0;
        }
    }
    expect_true("list omits code", code_empty);

    const nloj::submit::SubmissionPage by_prob =
        nloj::submit::list_my_submissions(user_a, 1, 20, visible_id, "");
    expect_true("list filter problem_id", page_contains(by_prob, sid));

    const nloj::submit::SubmissionPage by_pending =
        nloj::submit::list_my_submissions(user_a, 1, 20, 0, "PENDING");
    expect_true("list filter PENDING", page_contains(by_pending, sid));

    const nloj::submit::SubmissionPage by_ac =
        nloj::submit::list_my_submissions(user_a, 1, 20, 0, "AC");
    expect_true("list filter AC excludes PENDING", !page_contains(by_ac, sid));

    const nloj::submit::SubmissionPage other_list =
        nloj::submit::list_my_submissions(user_b, 1, 20, 0, "");
    expect_true("list other user excludes mine", !page_contains(other_list, sid));
}

}  // namespace

int main() {
    // 提交全流程联调 -> 汇总退出码
    test_submit_flow();
    if (g_failed) {
        std::cerr << "nl-submit db tests failed (检查 MySQL 是否启动且 nloj/nloj_db 已就绪)\n";
        return EXIT_FAILURE;
    }
    std::cout << "nl-submit db tests passed\n";
    return EXIT_SUCCESS;
}
