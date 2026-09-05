#pragma once

#include <cstdint>

namespace nloj::judge {

const char* module_name();

// 消费一条判题任务（MQ / 进程内队列），无对应 HTTP。
// submission_id 来自消息体。成功写回结果返回 1；失败返回 0。
int run_judge_task(std::int64_t submission_id);

}  // namespace nloj::judge
