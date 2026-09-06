// 领域错误码 -> HTTP code / message。
#include "nloj/common/error.h"

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

void test_http_mapping() {
    expect_true("ok is 0", nloj::common::http_code(nloj::common::AppError::Ok) == 0);
    expect_true("invalid is 40000",
                nloj::common::http_code(nloj::common::AppError::InvalidArgument) == 40000);
    expect_true("taken is 40000",
                nloj::common::http_code(nloj::common::AppError::UsernameTaken) == 40000);
    expect_true("wrong password is 40100",
                nloj::common::http_code(nloj::common::AppError::WrongPassword) == 40100);
    expect_true("forbidden is 40101",
                nloj::common::http_code(nloj::common::AppError::Forbidden) == 40101);
    expect_true("not found is 40400",
                nloj::common::http_code(nloj::common::AppError::NotFound) == 40400);
    expect_true("db is 50000",
                nloj::common::http_code(nloj::common::AppError::Database) == 50000);
    expect_true("internal is 50001",
                nloj::common::http_code(nloj::common::AppError::Internal) == 50001);
    expect_true("taken message",
                std::string(nloj::common::http_message(nloj::common::AppError::UsernameTaken))
                    == "用户名已存在");

    nloj::common::AppError err = nloj::common::AppError::Ok;
    nloj::common::set_error(&err, nloj::common::AppError::WrongPassword);
    expect_true("set_error writes", err == nloj::common::AppError::WrongPassword);
    nloj::common::set_error(nullptr, nloj::common::AppError::Database);
}

}  // namespace

int main() {
    test_http_mapping();
    if (g_failed) {
        std::cerr << "nl-common error tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "nl-common error tests passed\n";
    return EXIT_SUCCESS;
}
