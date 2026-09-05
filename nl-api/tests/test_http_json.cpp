// nl-api 统一响应 / Bearer / 分页解析（不启 HTTP 服务）。
#include "http_json.h"

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

void test_extract_bearer() {
    expect_true("bearer ok", nloj::api::extract_bearer("Bearer abc.def") == "abc.def");
    expect_true("reject missing prefix", nloj::api::extract_bearer("abc.def").empty());
    expect_true("reject basic", nloj::api::extract_bearer("Basic abc").empty());
    expect_true("reject empty", nloj::api::extract_bearer("").empty());
    expect_true("reject bearer only", nloj::api::extract_bearer("Bearer ").empty());
}

void test_parse_page_query() {
    std::int64_t num = 0;
    std::int64_t size = 0;
    expect_true("page defaults", nloj::api::parse_page_query("", "", num, size)
                && num == 1 && size == 20);
    expect_true("page custom", nloj::api::parse_page_query("2", "10", num, size)
                && num == 2 && size == 10);
    expect_true("reject pageNum 0", !nloj::api::parse_page_query("0", "20", num, size));
    expect_true("reject pageSize 101", !nloj::api::parse_page_query("1", "101", num, size));
    expect_true("reject garbage", !nloj::api::parse_page_query("x", "20", num, size));
}

void test_json_envelope() {
    const std::string ok = nloj::api::json_ok(10001);
    expect_true("ok has code 0", ok.find("\"code\":0") != std::string::npos);
    expect_true("ok has data id", ok.find("10001") != std::string::npos);

    const std::string err = nloj::api::json_err(40100, "未登录");
    expect_true("err has 40100", err.find("\"code\":40100") != std::string::npos);
    expect_true("err data null", err.find("\"data\":null") != std::string::npos);

    expect_true("int_or_null pending", nloj::api::int_or_null(-1).is_null());
    expect_true("int_or_null value", nloj::api::int_or_null(12) == 12);
}

}  // namespace

int main() {
    test_extract_bearer();
    test_parse_page_query();
    test_json_envelope();
    if (g_failed) {
        std::cerr << "nl-api http json tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "nl-api http json tests passed\n";
    return EXIT_SUCCESS;
}
