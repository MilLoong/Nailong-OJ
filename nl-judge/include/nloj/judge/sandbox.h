#pragma once

#include <memory>
#include <string>

namespace nloj::judge {

// 沙箱一次调用：compile_only=1 只编译；否则用已编译产物跑一组输入。
struct SandboxRequest {
    std::string language;      // 目前只支持 CPP
    std::string code;          // 编译时用
    std::string stdin_data;    // 运行时喂给程序
    int time_limit_ms;         // 运行超时
    int memory_limit_kb;       // Docker --memory
    int compile_only;          // 1 只编译 0 运行
};

// 沙箱结果。verdict: OK | CE | TLE | MLE | RE | SYSTEM_ERROR
struct SandboxResult {
    std::string verdict;
    std::string stdout_text;
    std::string stderr_text;
    int time_used_ms;
    int memory_used_kb;  // 测不到时为 -1
};

// 判题沙箱。Docker 实现为主，本机进程为降级。
class JudgeSandbox {
public:
    virtual ~JudgeSandbox() = default;
    virtual SandboxResult execute(const SandboxRequest& request) = 0;
};

// 优先 Docker；docker 不可用时退回本机 g++（无隔离，仅演示）。
std::unique_ptr<JudgeSandbox> make_sandbox();

// 去尾空白后精确比对。相等返回 1。
int judge_outputs_match(const std::string& expected, const std::string& actual);

}  // namespace nloj::judge
