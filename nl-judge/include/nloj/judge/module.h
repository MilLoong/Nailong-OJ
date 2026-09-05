#pragma once

#include <cstdint>

namespace nloj::judge {

const char* module_name();

// 消费一条判题任务（MQ / 进程内队列），无对应 HTTP。
// submission_id 来自消息体。已是终态则直接返回 1（幂等）。
// 成功写回结果返回 1；失败返回 0。
int run_judge_task(std::int64_t submission_id);

// 把超时仍停在 JUDGING 的提交改回 PENDING 并重新入队。
// older_than_sec 秒内的不动。返回回收条数。
int reclaim_stale_judging(int older_than_sec);

}  // namespace nloj::judge
