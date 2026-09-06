// nl-judge 判题联调（需要本机 MySQL，以及 Docker 或 PATH 里的 g++）。
#include "nloj/common/mq.h"
#include "nloj/common/mysql.h"
#include "nloj/judge/module.h"
#include "nloj/judge/language.h"
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

std::int64_t make_problem_ex(const std::string& type,
                             const std::string& mode,
                             const std::string& extra) {
    nloj::problem::CreateProblemRequest req;
    req.title = unique_name("ut_judge_prob_");
    req.difficulty = "EASY";
    req.description = "A + B";
    req.time_limit = 8000;
    req.memory_limit = 262144;
    req.visible = 1;
    req.problem_type = type;
    req.judge_mode = mode;
    req.extra_code = extra;
    return nloj::problem::create_problem(req);
}

std::int64_t make_problem() {
    return make_problem_ex("STANDARD", "EXACT", "");
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

// 非法 id -> 提交+消费 MQ -> AC / WA / CE。
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

void judge_one_language(const char* lang,
                        const std::string& code,
                        const char* tool) {
    if (tool != nullptr && !nloj::judge::command_exists(tool)) {
        std::cout << "[SKIP] " << lang << " (no " << tool << ")\n";
        return;
    }
    drain_judge_queue();
    const std::int64_t user_id = insert_user(unique_name("ut_lang_u_"));
    const std::int64_t problem_id = make_problem();
    expect_true("lang user", user_id > 0);
    expect_true("lang problem", problem_id > 0);
    expect_true("lang case", insert_case(problem_id, "1 2", "3", 1));
    const std::int64_t sid = nloj::submit::create_submission(user_id, problem_id, lang, code);
    expect_true("lang submit", sid > 0);
    drain_judge_queue();
    expect_true("lang judge ok", nloj::judge::run_judge_task(sid) == 1);
    const nloj::submit::SubmissionDetail d =
        nloj::submit::get_submission(sid, user_id, "user");
    if (d.status != "AC") {
        std::cout << lang << " got " << d.status << " info=" << d.judge_info << '\n';
    }
    const std::string ac_name = std::string(lang) + " AC";
    expect_true(ac_name.c_str(), d.status == "AC");
}

void test_languages() {
    judge_one_language(
        "C",
        "#include <stdio.h>\nint main(){int a,b;scanf(\"%d %d\",&a,&b);printf(\"%d\\n\",a+b);return 0;}\n",
        "gcc"
    );
    judge_one_language(
        "PYTHON",
        "a,b=map(int,input().split())\nprint(a+b)\n",
#ifdef _WIN32
        nloj::judge::command_exists("python") ? "python" : "python3"
#else
        "python3"
#endif
    );
    judge_one_language(
        "JAVA",
        "import java.util.*;\npublic class Main{public static void main(String[] a){"
        "Scanner s=new Scanner(System.in);int x=s.nextInt(),y=s.nextInt();"
        "System.out.println(x+y);}}\n",
        "javac"
    );
}

void test_spj() {
    drain_judge_queue();
    const std::string checker =
        "#include <fstream>\n"
        "int main(int argc, char** argv) {\n"
        "  if (argc < 4) return 2;\n"
        "  std::ifstream in(argv[1]);\n"
        "  std::ifstream out(argv[2]);\n"
        "  int n = 0, a = 0, b = 0;\n"
        "  if (!(in >> n)) return 1;\n"
        "  if (!(out >> a >> b)) return 1;\n"
        "  return (a + b == n) ? 0 : 1;\n"
        "}\n";
    const std::int64_t user_id = insert_user(unique_name("ut_spj_u_"));
    const std::int64_t problem_id = make_problem_ex("STANDARD", "SPJ", checker);
    expect_true("spj user", user_id > 0);
    expect_true("spj problem", problem_id > 0);
    expect_true("spj case", insert_case(problem_id, "10", "0 10", 1));

    const std::string ac =
        "#include <iostream>\n"
        "int main() { int n; std::cin >> n; std::cout << 0 << ' ' << n << std::endl; return 0; }\n";
    const std::int64_t ac_id = nloj::submit::create_submission(user_id, problem_id, "CPP", ac);
    expect_true("spj AC submit", ac_id > 0);
    drain_judge_queue();
    expect_true("spj judge AC", nloj::judge::run_judge_task(ac_id) == 1);
    const nloj::submit::SubmissionDetail acd =
        nloj::submit::get_submission(ac_id, user_id, "user");
    if (acd.status != "AC") {
        std::cout << "spj AC got " << acd.status << " info=" << acd.judge_info << '\n';
    }
    expect_true("spj AC status", acd.status == "AC");

    const std::string wa =
        "#include <iostream>\n"
        "int main() { int n; std::cin >> n; std::cout << 1 << ' ' << 1 << std::endl; return 0; }\n";
    const std::int64_t wa_id = nloj::submit::create_submission(user_id, problem_id, "CPP", wa);
    expect_true("spj WA submit", wa_id > 0);
    drain_judge_queue();
    expect_true("spj judge WA", nloj::judge::run_judge_task(wa_id) == 1);
    const nloj::submit::SubmissionDetail wad =
        nloj::submit::get_submission(wa_id, user_id, "user");
    if (wad.status != "WA") {
        std::cout << "spj WA got " << wad.status << " info=" << wad.judge_info << '\n';
    }
    expect_true("spj WA status", wad.status == "WA");
}

void test_interactive() {
    drain_judge_queue();
    const std::string interactor =
        "#include <fstream>\n#include <iostream>\n"
        "int main(int argc,char** argv){if(argc<2)return 2;"
        "std::ifstream in(argv[1]);int a=0,b=0;in>>a>>b;"
        "std::cout<<a<<' '<<b<<std::endl;std::cout.flush();"
        "int c=0;if(!(std::cin>>c))return 1;return c==a+b?0:1;}\n";
    const std::int64_t user_id = insert_user(unique_name("ut_ia_u_"));
    const std::int64_t problem_id = make_problem_ex("INTERACTIVE", "EXACT", interactor);
    expect_true("interactive user", user_id > 0);
    expect_true("interactive problem", problem_id > 0);
    expect_true("interactive case", insert_case(problem_id, "1 2", "", 1));
    const std::string ac =
        "#include <iostream>\nint main(){int a,b;std::cin>>a>>b;std::cout<<a+b<<std::endl;return 0;}\n";
    const std::int64_t sid = nloj::submit::create_submission(user_id, problem_id, "CPP", ac);
    expect_true("interactive submit", sid > 0);
    drain_judge_queue();
    expect_true("interactive judge ran", nloj::judge::run_judge_task(sid) == 1);
    const nloj::submit::SubmissionDetail d =
        nloj::submit::get_submission(sid, user_id, "user");
    if (d.status == "SYSTEM_ERROR"
     && d.judge_info.find("require Docker or Linux") != std::string::npos) {
        std::cout << "[SKIP] interactive (needs Docker or Linux)\n";
        return;
    }
    expect_true("interactive AC", d.status == "AC");
}

void test_communication() {
    drain_judge_queue();
    const std::string manager =
        "#include <fstream>\n"
        "int main(int argc,char** argv){if(argc<6)return 2;"
        "std::ifstream in(argv[1]);int n=0;in>>n;"
        "std::ofstream ta(argv[2]);ta<<n<<std::endl;ta.close();"
        "std::ifstream fa(argv[3]);int a=0;fa>>a;"
        "std::ofstream tb(argv[4]);tb<<a<<std::endl;tb.close();"
        "std::ifstream fb(argv[5]);int b=0;fb>>b;"
        "return b==n+2?0:1;}\n";
    const std::int64_t user_id = insert_user(unique_name("ut_comm_u_"));
    const std::int64_t problem_id = make_problem_ex("COMMUNICATION", "EXACT", manager);
    expect_true("comm user", user_id > 0);
    expect_true("comm problem", problem_id > 0);
    expect_true("comm case", insert_case(problem_id, "3", "", 1));
    const std::string code =
        "===NLOJ_FILE:alice===\n"
        "#include <iostream>\nint main(){int n;std::cin>>n;std::cout<<n+1<<std::endl;return 0;}\n"
        "===NLOJ_FILE:bob===\n"
        "#include <iostream>\nint main(){int x;std::cin>>x;std::cout<<x+1<<std::endl;return 0;}\n";
    const std::int64_t sid = nloj::submit::create_submission(user_id, problem_id, "CPP", code);
    expect_true("comm submit", sid > 0);
    drain_judge_queue();
    expect_true("comm judge ran", nloj::judge::run_judge_task(sid) == 1);
    const nloj::submit::SubmissionDetail d =
        nloj::submit::get_submission(sid, user_id, "user");
    if (d.status == "SYSTEM_ERROR"
     && d.judge_info.find("require Docker or Linux") != std::string::npos) {
        std::cout << "[SKIP] communication (needs Docker or Linux)\n";
        return;
    }
    expect_true("comm AC", d.status == "AC");
}

}  // namespace

int main() {
    // 判题联调 -> 汇总退出码
    test_judge_flow();
    test_reclaim_and_heartbeat();
    test_languages();
    test_spj();
    test_interactive();
    test_communication();
    if (g_failed) {
        std::cerr << "nl-judge db tests failed (检查 MySQL / Docker 或 g++)\n";
        return EXIT_FAILURE;
    }
    std::cout << "nl-judge db tests passed\n";
    return EXIT_SUCCESS;
}
