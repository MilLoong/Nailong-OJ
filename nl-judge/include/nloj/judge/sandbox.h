#pragma once

#include <memory>
#include <string>
#include <vector>

namespace nloj::judge {

// 一次判题请求：编译代码后，在单个沙箱（容器）里按顺序跑完全部用例。
// inputs 按题目 sort_order 排序，用例序号从 1 起。
struct SandboxJudgeRequest {
    std::string language;      // 目前只支持 CPP
    std::string code;          // 待判代码
    int time_limit_ms;         // 单个用例的运行超时
    int memory_limit_kb;       // 单个用例内存上限（Docker --memory）
    std::vector<std::string> inputs;  // 全部用例输入
};

// 单个用例的运行结果。verdict: OK | TLE | MLE | RE | SE
struct SandboxCaseResult {
    int index;                 // 用例序号，从 1 起，与 inputs 对应
    std::string verdict;       // 运行结论
    int time_used_ms;          // 容器内实测耗时
    int memory_used_kb;        // 子进程 ru_maxrss；测不到为 -1
    std::string stdout_text;   // 实际输出（宿主侧做 WA 比对）
};

// 判题结果。status: OK | CE | SYSTEM_ERROR
// status=OK 时 cases 与 inputs 顺序对应；首个失败用例后不再有后续用例。
struct SandboxJudgeResult {
    std::string status;                // OK=编译过且已跑；CE/SYSTEM_ERROR 见 error_text
    std::string error_text;            // CE 编译错误 / SYSTEM_ERROR 原因（截断）
    std::vector<SandboxCaseResult> cases;  // 顺序与 inputs 对应
};

// 判题沙箱：编译一次 + 单容器按顺序跑完全部用例，容器内逐用例计时并记录内存。
class JudgeSandbox {
public:
    virtual ~JudgeSandbox() = default;
    virtual SandboxJudgeResult judge(const SandboxJudgeRequest& request) = 0;
};

// 优先 Docker；docker 不可用时退回本机 g++（无隔离，仅演示）。
std::unique_ptr<JudgeSandbox> make_sandbox();

// 去尾空白后精确比对。相等返回 1。
int judge_outputs_match(const std::string& expected, const std::string& actual);

}  // namespace nloj::judge
