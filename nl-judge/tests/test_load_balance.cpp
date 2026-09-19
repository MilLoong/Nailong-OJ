// 最小负载选择：在线、任务数、id 次序。
#include "nloj/judge/load_balance.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_failed = 0;

void expect_true(const char* name, int ok) {
    if (ok) {
        std::cout << "[PASS] " << name << '\n';
    } else {
        std::cout << "[FAIL] " << name << '\n';
        g_failed = 1;
    }
}

void test_pick_least_load() {
    nloj::judge::LoadBalance lb;
    std::string picked;
    expect_true("empty pick fails", lb.pick(picked) == 0);

    lb.upsert("node-b", 2, 1);
    lb.upsert("node-a", 5, 1);
    lb.upsert("node-c", 2, 0);
    expect_true("skips offline", lb.pick(picked) == 1 && picked == "node-b");

    lb.upsert("node-a", 1, 1);
    expect_true("lower load wins", lb.pick(picked) == 1 && picked == "node-a");

    lb.upsert("node-d", 1, 1);
    expect_true("tie breaks by id", lb.pick(picked) == 1 && picked == "node-a");

    expect_true("inc missing", lb.inc_load("nope") == -1);
    expect_true("inc known", lb.inc_load("node-a") == 2);
    expect_true("after inc pick changes", lb.pick(picked) == 1 && picked == "node-d");

    expect_true("dec to zero", lb.dec_load("node-d") == 0);
    expect_true("dec stays zero", lb.dec_load("node-d") == 0);
    lb.set_online("node-d", 0);
    lb.set_online("node-a", 0);
    lb.set_online("node-b", 0);
    expect_true("all offline", lb.pick(picked) == 0);
}

}  // namespace

int main() {
    test_pick_least_load();
    if (g_failed) {
        std::cerr << "nl-judge load balance tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "nl-judge load balance tests passed\n";
    return EXIT_SUCCESS;
}
