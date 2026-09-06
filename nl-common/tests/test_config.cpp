// 配置：默认值 + 环境变量覆盖（不连 MySQL）。
#include "nloj/common/config.h"

#include <cstdlib>
#include <iostream>

#ifdef _WIN32
#include <stdlib.h>
#endif

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

void set_env(const char* key, const char* value) {
#ifdef _WIN32
    _putenv_s(key, value);
#else
    setenv(key, value, 1);
#endif
}

void test_defaults_and_env() {
    nloj::common::reset_app_config();
    const nloj::common::AppConfig d = nloj::common::app_config();
    expect_true("default mysql user", d.mysql_user == "nloj");
    expect_true("default mysql port", d.mysql_port == 3306);
    expect_true("default http port", d.http_port == 8080);
    expect_true("default pool size", d.mysql_pool_size == 8);

    set_env("NLOJ_HTTP_PORT", "18080");
    set_env("NLOJ_MYSQL_POOL_SIZE", "4");
    nloj::common::reset_app_config();
    const nloj::common::AppConfig e = nloj::common::app_config();
    expect_true("env http port", e.http_port == 18080);
    expect_true("env pool size", e.mysql_pool_size == 4);

    set_env("NLOJ_HTTP_PORT", "");
    set_env("NLOJ_MYSQL_POOL_SIZE", "");
    nloj::common::reset_app_config();
}

}  // namespace

int main() {
    test_defaults_and_env();
    if (g_failed) {
        std::cerr << "nl-common config tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "nl-common config tests passed\n";
    return EXIT_SUCCESS;
}
