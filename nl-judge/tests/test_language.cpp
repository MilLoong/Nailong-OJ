// 语言约定 / 通信源码拆分（不连 MySQL）
#include "nloj/judge/language.h"

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

void test_language_names() {
    expect_true("CPP supported", nloj::judge::language_supported("CPP"));
    expect_true("C supported", nloj::judge::language_supported("C"));
    expect_true("PYTHON supported", nloj::judge::language_supported("PYTHON"));
    expect_true("JAVA supported", nloj::judge::language_supported("JAVA"));
    expect_true("GO rejected", !nloj::judge::language_supported("GO"));
    expect_true("CPP interactive ok", nloj::judge::language_allows_interactive("CPP"));
    expect_true("PYTHON interactive no", !nloj::judge::language_allows_interactive("PYTHON"));
    expect_true("CPP source main.cpp",
                std::string(nloj::judge::language_source_name("CPP")) == "main.cpp");
    expect_true("C source main.c",
                std::string(nloj::judge::language_source_name("C")) == "main.c");
    expect_true("PYTHON source main.py",
                std::string(nloj::judge::language_source_name("PYTHON")) == "main.py");
    expect_true("JAVA source Main.java",
                std::string(nloj::judge::language_source_name("JAVA")) == "Main.java");
    expect_true("CPP compile nonempty",
                !nloj::judge::language_compile_cmd("CPP", 1).empty());
    expect_true("PYTHON no compile",
                nloj::judge::language_compile_cmd("PYTHON", 1).empty());
}

void test_split_comm() {
    std::string alice;
    std::string bob;
    expect_true("split reject empty",
                !nloj::judge::split_comm_sources("int main(){}", alice, bob));
    const std::string code = "===NLOJ_FILE:alice===\nint alice(){return 1;}\n"
                             "===NLOJ_FILE:bob===\nint bob(){return 2;}\n";
    expect_true("split ok", nloj::judge::split_comm_sources(code, alice, bob));
    expect_true("split alice", alice.find("alice()") != std::string::npos);
    expect_true("split bob", bob.find("bob()") != std::string::npos);
}

}  // namespace

int main() {
    test_language_names();
    test_split_comm();
    if (g_failed) {
        std::cerr << "nl-judge language tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "nl-judge language tests passed\n";
    return EXIT_SUCCESS;
}
