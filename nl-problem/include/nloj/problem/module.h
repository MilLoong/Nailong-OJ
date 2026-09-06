#pragma once

#include "nloj/common/error.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nloj::problem {

const char* module_name();

// 列表项，对应 GET /api/v1/problems 的 records[]（不含题面）。
struct ProblemSummary {
    std::int64_t id;          // 题目主键
    std::string title;        // 标题
    std::string difficulty;   // EASY | MEDIUM | HARD
    int time_limit;           // ms
    int memory_limit;         // KB
    int visible;              // 1 可见 0 隐藏
    std::string create_time;  // 创建时间
    std::string problem_type; // STANDARD | INTERACTIVE | COMMUNICATION
    std::string judge_mode;   // EXACT | SPJ
};

// 样例用例，对应详情里 samples[]（仅 is_sample=1）。
struct ProblemSample {
    std::int64_t id;     // 用例主键
    std::string input;   // 输入
    std::string output;  // 期望输出
};

// 题目详情，对应 GET /api/v1/problems/{id} 的 data。
struct ProblemDetail {
    std::int64_t id;
    std::string title;
    std::string difficulty;
    std::string description;  // 题面 Markdown
    int time_limit;
    int memory_limit;
    int visible;
    std::string create_time;
    std::string problem_type; // STANDARD | INTERACTIVE | COMMUNICATION
    std::string judge_mode;   // EXACT | SPJ
    std::vector<ProblemSample> samples;
};

// 分页列表，对应 GET /api/v1/problems 的 data。
struct ProblemPage {
    std::int64_t page_num;
    std::int64_t page_size;
    std::int64_t total;
    std::vector<ProblemSummary> records;
};

// 创建/更新请求体字段（HTTP JSON → 领域入参）。
struct CreateProblemRequest {
    std::string title;
    std::string difficulty;
    std::string description;
    int time_limit;
    int memory_limit;
    int visible;  // 0/1
    std::string problem_type = "STANDARD";  // STANDARD | INTERACTIVE | COMMUNICATION
    std::string judge_mode = "EXACT";       // EXACT | SPJ
    std::string extra_code;                 // checker / 交互器 / 管理器；不对用户展示
};

// 判题机用的题目配置（含 extra_code；不看 visible）。
struct ProblemJudgeConfig {
    std::int64_t id;
    int time_limit;
    int memory_limit;
    std::string problem_type;
    std::string judge_mode;
    std::string extra_code;
};

// 题目分页列表。difficulty / keyword 空表示不限。
// 对应 GET /api/v1/problems。
ProblemPage list_problems(std::int64_t page_num,
                          std::int64_t page_size,
                          const std::string& difficulty,
                          const std::string& keyword);

// 进程内缓存计数，给 health / 压测看命中率。
struct ProblemCacheStats {
    std::int64_t total;       // get_problem 调用次数
    std::int64_t l1_hit;      // 进程内 L1 命中
    std::int64_t redis_hit;   // Redis 命中（含空值）
    std::int64_t mysql_load;  // 回源 MySQL
};

// 当前进程的缓存计数快照。
ProblemCacheStats problem_cache_stats();

// 题目详情（含样例）。对应 GET /api/v1/problems/{id}。
// 先 L1 / Redis（String JSON），未命中再查库；不存在的 id 缓存短 TTL 空值。
// skip_cache=1 时跳过缓存并强制回源（压测对照；HTTP 头 X-NLOJ-Skip-Cache: 1）。
// 成功填满 id>0；失败返回 id==0 的空结构。
ProblemDetail get_problem(std::int64_t id, int skip_cache = 0);

// 创建题目。对应 POST /api/v1/problems（api 层保证 admin）。
// 成功返回新 id；失败返回 -1，并通过 err 给出原因。
std::int64_t create_problem(const CreateProblemRequest& req,
                            nloj::common::AppError* err = nullptr);

// 更新题目。对应 PUT /api/v1/problems/{id}。
// 成功返回 1；失败返回 0，并通过 err 给出原因。
int update_problem(std::int64_t id,
                   const CreateProblemRequest& req,
                   nloj::common::AppError* err = nullptr);

// 判题读时限 / 题型 / SPJ 源码。成功 id>0；失败 id==0。
ProblemJudgeConfig get_problem_judge_config(std::int64_t id);

}  // namespace nloj::problem
