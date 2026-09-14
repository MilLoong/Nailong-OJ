// 轻量日志：级别过滤、可选写文件。
#include "nloj/common/log.h"

#include <cstdlib>
#include <fstream>
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

std::string read_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

void test_level_and_file() {
    const std::string path = "nloj_log_test_out.txt";
    {
        std::ofstream trunc(path, std::ios::trunc);
    }

    nloj::common::set_log_file(path);
    nloj::common::set_log_level(nloj::common::LogLevel::Warn);
    nloj::common::log_info("should-skip-info");
    nloj::common::log_warn("keep-warn");
    nloj::common::log_error("keep-error");

    const std::string text = read_all(path);
    expect_true("skips info when warn", text.find("should-skip-info") == std::string::npos);
    expect_true("writes warn", text.find("keep-warn") != std::string::npos);
    expect_true("writes error", text.find("keep-error") != std::string::npos);
    expect_true("has WARN tag", text.find("[WARN]") != std::string::npos);

    nloj::common::set_log_file("");
    nloj::common::set_log_level(nloj::common::LogLevel::Info);
    std::remove(path.c_str());
}

}  // namespace

int main() {
    test_level_and_file();
    if (g_failed) {
        std::cerr << "nl-common log tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "nl-common log tests passed\n";
    return EXIT_SUCCESS;
}
