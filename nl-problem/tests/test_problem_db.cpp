// nl-problem 题目 CRUD / 分页联调（需要本机 MySQL：nloj / nloj123456 / nloj_db）。
#include "nloj/problem/module.h"
#include "nloj/common/mysql.h"
#include "nloj/common/redis.h"

#include <ctime>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_failed = 0;  // 任一条失败则置 1

// 打印 [PASS]/[FAIL]；失败时记下 g_failed。
void expect_true(const char* name, int ok) {
    if (ok) {
        std::cout << "[PASS] " << name << '\n';
    } else {
        std::cout << "[FAIL] " << name << '\n';
        g_failed = 1;
    }
}

// 唯一标题，避免列表里难辨认。
std::string unique_title() {
    return "ut_prob_" + std::to_string(std::time(nullptr));
}

// 给详情测样例：插入一条 is_sample=1 的用例。
int insert_sample_case(std::int64_t problem_id) {
    MYSQL mysql;
    if (!nloj::common::start_mysql(mysql)) {
        return 0;
    }
    const std::string sql = "INSERT INTO problem_case (problem_id, input, output, is_sample, sort_order) VALUES ("
                           + std::to_string(problem_id)
                           + ", '1 2', '3', 1, 1)";
    const int ok = nloj::common::query_exec(&mysql, sql) ? 1 : 0;
    mysql_close(&mysql);
    return ok;
}

// 非法 create → 合法 create → list 能搜到 → get 含样例 → update → 再 get。
void test_problem_crud_flow() {
    nloj::problem::CreateProblemRequest bad;
    bad.title = "";
    bad.difficulty = "EASY";
    bad.description = "x";
    bad.time_limit = 1000;
    bad.memory_limit = 262144;
    bad.visible = 1;
    expect_true("create rejects empty title", nloj::problem::create_problem(bad) < 0);

    nloj::problem::CreateProblemRequest req;
    req.title = unique_title();
    req.difficulty = "EASY";
    req.description = "unit test problem body";
    req.time_limit = 1000;
    req.memory_limit = 262144;
    req.visible = 1;

    const std::int64_t id = nloj::problem::create_problem(req);
    expect_true("create_problem returns id", id > 0);

    expect_true("insert sample case", insert_sample_case(id));

    const nloj::problem::ProblemPage page =
        nloj::problem::list_problems(1, 20, "EASY", "ut_prob_");
    expect_true("list total > 0", page.total > 0);
    int found = 0;
    for (const auto& item : page.records) {
        if (item.id == id) {
            found = 1;
            expect_true("list title match", item.title == req.title);
            break;
        }
    }
    expect_true("list contains new problem", found);

    const nloj::problem::ProblemDetail detail = nloj::problem::get_problem(id);
    expect_true("get_problem id match", detail.id == id);
    expect_true("get_problem title match", detail.title == req.title);
    expect_true("get_problem has sample", !detail.samples.empty());
    if (!detail.samples.empty()) {
        expect_true("sample input match", detail.samples[0].input == "1 2");
        expect_true("sample output match", detail.samples[0].output == "3");
    }

    req.title = req.title + "_upd";
    req.difficulty = "MEDIUM";
    expect_true("update_problem ok", nloj::problem::update_problem(id, req));

    const nloj::problem::ProblemDetail after = nloj::problem::get_problem(id);
    expect_true("get after update title", after.title == req.title);
    expect_true("get after update difficulty", after.difficulty == "MEDIUM");

    if (nloj::common::redis_using()) {
        const nloj::problem::ProblemCacheStats before_hit = nloj::problem::problem_cache_stats();
        const nloj::problem::ProblemDetail again = nloj::problem::get_problem(id);
        expect_true("cache second get title", again.title == req.title);
        const nloj::problem::ProblemCacheStats after_hit = nloj::problem::problem_cache_stats();
        expect_true("second get counted as cache hit",
                    after_hit.l1_hit + after_hit.redis_hit
                    > before_hit.l1_hit + before_hit.redis_hit);
        expect_true("missing id is empty", nloj::problem::get_problem(999999999).id == 0);
        std::string nil_raw;
        expect_true("nil cached for missing id",
                    nloj::common::redis_get("nloj:problem:999999999", nil_raw)
                    && nil_raw == "__nil__");
    }

    expect_true("update missing id fails", !nloj::problem::update_problem(999999999, req));
}

}  // namespace

int main() {
    // 题目 CRUD 联调 → 汇总退出码
    test_problem_crud_flow();
    if (g_failed) {
        std::cerr << "nl-problem db tests failed (检查 MySQL 是否启动且 nloj/nloj_db 已就绪)\n";
        return EXIT_FAILURE;
    }
    std::cout << "nl-problem db tests passed\n";
    return EXIT_SUCCESS;
}
