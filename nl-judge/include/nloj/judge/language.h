#pragma once

#include <string>
#include <vector>

namespace nloj::judge {

// 提交语言：CPP | C | PYTHON | JAVA。支持返回 1。
int language_supported(const std::string& language);

// 交互 / 通信的用户程序目前只要单一可执行文件：CPP | C。
int language_allows_interactive(const std::string& language);

// 源文件名：main.cpp / main.c / main.py / Main.java。
const char* language_source_name(const std::string& language);

// 空串表示解释型、不用编译。posix=1 产出 ./main；posix=0 产出 main.exe。
std::string language_compile_cmd(const std::string& language, int posix);

// 跑用户程序的 argv，如 {"./main"} / {"python3","main.py"}。
std::vector<std::string> language_run_argv(const std::string& language, int posix);

// 通信题从一份 code 拆出 alice / bob。成功 1。
int split_comm_sources(const std::string& code, std::string& alice, std::string& bob);

// PATH 里能否找到命令。找到返回 1。
int command_exists(const char* name);

}  // namespace nloj::judge
