// nl-judge 判题联调（需要本机 MySQL，以及 Docker 或 PATH 里的 g++）。
#include "nloj/common/mq.h"
#include "nloj/common/mysql.h"
#include "nloj/judge/module.h"
#include "nloj/judge/node.h"
#include "nloj/common/redis.h"
#include "nloj/problem/module.h"
#include "nloj/submit/module.h"

#include <ctime>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_failed = 0;
int g_name_seq = 0;

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

std::int64_t make_problem() {
    nloj::problem::CreateProblemRequest req;
    req.title = unique_name("ut_judge_prob_");
    req.difficulty = "EASY";
    req.description = "A + B";
    req.time_limit = 2000;
    req.memory_limit = 262144;
    req.visible = 1;
    return nloj::problem::create_problem(req);
}

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

void drain_judge_queue() {
    nloj::common::JudgeTaskMessage dump;
    while (nloj::common::try_pop_judge_task(dump)) {
        nloj::common::ack_judge_task(dump);
    }
}

// 非法 id → 提交+消费 MQ → AC / WA / CE。
void test_judge_flow() {
    drain_judge_queue();
    expect_true("reject id<=0", nloj::judge::run_judge_task(0) == 0);
    expect_true("reject missing id", nloj::judge::run_judge_task(999999999) == 0);

    const std::int64_t user_id = insert_user(unique_name("ut_judge_u_"));
    const std::int64_t problem_id = make_problem();
    expect_true("insert judge user", user_id > 0);
    expect_true("create judge problem", problem_id > 0);
    expect_true("insert case 1", insert_case(problem_id, "1 2", "3", 1));
    expect_true("insert case 2", insert_case(problem_id, "100 200", "300", 2));

    const std::string ac_code = "#include <iostream>\n"
                                "int main() { int a,b; std::cin>>a>>b; std::cout<<a+b<<std::endl; return 0; }\n";
    const std::int64_t ac_id = nloj::submit::create_submission(
        user_id, problem_id, "CPP", ac_code);
    expect_true("create AC submission", ac_id > 0);

    nloj::common::JudgeTaskMessage task;
    expect_true("pop judge task", nloj::common::try_pop_judge_task(task));
    expect_true("judge task id match", task.submission_id == ac_id);
    expect_true("run_judge_task AC ok", nloj::judge::run_judge_task(ac_id) == 1);
    nloj::common::ack_judge_task(task);

    const nloj::submit::SubmissionDetail ac =
        nloj::submit::get_submission(ac_id, user_id, "user");
    expect_true("AC status", ac.status == "AC");
    expect_true("AC judge_info mentions cases",
                ac.judge_info.find("2 test cases") != std::string::npos);

    const std::string wa_code = "#include <iostream>\n"
                                "int main() { std::cout<<0; return 0; }\n";
    const std::int64_t wa_id = nloj::submit::create_submission(
        user_id, problem_id, "CPP", wa_code);
    expect_true("create WA submission", wa_id > 0);
    drain_judge_queue();
    expect_true("run_judge_task WA ok", nloj::judge::run_judge_task(wa_id) == 1);
    const nloj::submit::SubmissionDetail wa =
        nloj::submit::get_submission(wa_id, user_id, "user");
    expect_true("WA status", wa.status == "WA");

    const std::int64_t ce_id = nloj::submit::create_submission(
        user_id, problem_id, "CPP", "this is not c++ {{{");
    expect_true("create CE submission", ce_id > 0);
    drain_judge_queue();
    expect_true("run_judge_task CE ok", nloj::judge::run_judge_task(ce_id) == 1);
    const nloj::submit::SubmissionDetail ce =
        nloj::submit::get_submission(ce_id, user_id, "user");
    expect_true("CE status", ce.status == "CE");

    expect_true("rerun AC is idempotent", nloj::judge::run_judge_task(ac_id) == 1);
    const nloj::submit::SubmissionDetail ac_again =
        nloj::submit::get_submission(ac_id, user_id, "user");
    expect_true("rerun keeps AC", ac_again.status == "AC");
}

void test_reclaim_and_heartbeat() {
    drain_judge_queue();
    const std::int64_t user_id = insert_user(unique_name("ut_reclaim_u_"));
    const std::int64_t problem_id = make_problem();
    expect_true("reclaim user", user_id > 0);
    expect_true("reclaim problem", problem_id > 0);
    expect_true("reclaim case", insert_case(problem_id, "1 2", "3", 1));

    const std::int64_t sid = nloj::submit::create_submission(
        user_id, problem_id, "CPP", "int main(){return 0;}\n");
    expect_true("reclaim submission", sid > 0);
    drain_judge_queue();

    MYSQL mysql;
    expect_true("reclaim mysql", nloj::common::start_mysql(mysql) ? 1 : 0);
    const std::string stale_sql = "UPDATE submission SET status='JUDGING', "
                                  "update_time=DATE_SUB(NOW(), INTERVAL 400 SECOND) WHERE id="
                                 + std::to_string(sid);
    expect_true("mark stale JUDGING", nloj::common::query_exec(&mysql, stale_sql) ? 1 : 0);
    mysql_close(&mysql);

    const int n = nloj::judge::reclaim_stale_judging(180);
    expect_true("reclaim count >= 1", n >= 1);

    const nloj::submit::SubmissionDetail after =
        nloj::submit::get_submission(sid, user_id, "user");
    expect_true("reclaim back to PENDING", after.status == "PENDING");

    int found_sid = 0;
    nloj::common::JudgeTaskMessage again;
    while (nloj::common::try_pop_judge_task(again)) {
        if (again.submission_id == sid) {
            found_sid = 1;
        }
        nloj::common::ack_judge_task(again);
    }
    expect_true("reclaim republished", found_sid);

    if (nloj::common::redis_using()) {
        const std::string nid = "ut-node-1";
        expect_true("heartbeat write", nloj::judge::write_judge_heartbeat(nid, 30));
        int found = 0;
        for (const auto& id : nloj::judge::list_judge_nodes()) {
            if (id == nid) {
                found = 1;
            }
        }
        expect_true("heartbeat listed", found);
        nloj::common::redis_del("nloj:judge:node:" + nid);
    }
}

}  // namespace

int main() {
    // 判题联调 → 汇总退出码
    test_judge_flow();
    test_reclaim_and_heartbeat();
    if (g_failed) {
        std::cerr << "nl-judge db tests failed (检查 MySQL / Docker 或 g++)\n";
        return EXIT_FAILURE;
    }
    std::cout << "nl-judge db tests passed\n";
    return EXIT_SUCCESS;
}
