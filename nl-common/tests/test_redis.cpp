// nl-common Redis 字符串读写（需要本机 6379）。
#include "nloj/common/redis.h"

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

void test_string_roundtrip() {
    const std::string key = "nloj:ut:redis";
    nloj::common::redis_del(key);

    std::string miss;
    expect_true("miss after del", !nloj::common::redis_get(key, miss));

    expect_true("set_ex ok", nloj::common::redis_set_ex(key, "hello", 30));
    std::string hit;
    expect_true("get hit", nloj::common::redis_get(key, hit) && hit == "hello");

    expect_true("del ok", nloj::common::redis_del(key));
    expect_true("miss after del again", !nloj::common::redis_get(key, miss));
}

void test_set_nx() {
    const std::string key = "nloj:ut:redis:lock";
    nloj::common::redis_del(key);
    expect_true("nx first wins", nloj::common::redis_set_nx_ex(key, "1", 10));
    expect_true("nx second loses", !nloj::common::redis_set_nx_ex(key, "2", 10));
    nloj::common::redis_del(key);
}

}  // namespace

int main() {
    if (!nloj::common::redis_using()) {
        std::cerr << "nl-common redis tests failed: Redis 未就绪（127.0.0.1:6379）\n";
        return EXIT_FAILURE;
    }
    test_string_roundtrip();
    test_set_nx();
    if (g_failed) {
        std::cerr << "nl-common redis tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "nl-common redis tests passed\n";
    return EXIT_SUCCESS;
}
