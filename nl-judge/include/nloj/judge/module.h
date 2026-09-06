#pragma once

#include <cstdint>

namespace nloj::judge {

const char* module_name();

/**
 * @brief 消费一条判题任务（MQ / 进程内队列），无对应 HTTP。
 * @param submission_id 来自消息体。已是终态则直接返回 1（幂等）
 * @return 成功写回结果返回 1；失败返回 0
 */
int run_judge_task(std::int64_t submission_id);

/**
 * @brief 把超时仍停在 JUDGING 的提交改回 PENDING 并重新入队。
 *
 * 同样超时的 PENDING 也会再投一次（embed worker 关掉且 MQ 故障时自愈）。
 *
 * @param older_than_sec 秒内的不动
 * @return 回收条数
 */
int reclaim_stale_judging(int older_than_sec);

}  // namespace nloj::judge
