#pragma once

#include <cstdint>
#include <string>

namespace nloj::common {

/**
 * @brief 判题任务消息。
 */
struct JudgeTaskMessage {
    std::int64_t submission_id;  ///< 提交主键
    std::int64_t problem_id;     ///< 题目
    std::string language;        ///< CPP | JAVA | ...
    std::uint64_t delivery_tag = 0;  ///< RabbitMQ delivery_tag；进程内为 0
};

/**
 * @brief 投递判题任务。优先连本机 RabbitMQ；连不上 Broker 则降级进程内队列。
 * @return 成功 1，失败 0
 */
bool publish_judge_task(const JudgeTaskMessage& msg);

/**
 * @brief 非阻塞取一条。
 * @return 有消息返回 1 并写入 out；队列空返回 0
 */
bool try_pop_judge_task(JudgeTaskMessage& out);

/**
 * @brief 阻塞取一条。队列空则等待，直到 publish 唤醒后再弹出。
 */
void wait_pop_judge_task(JudgeTaskMessage& out);

/**
 * @brief 确认已处理完。进程内或 tag=0 为空操作。
 *
 * 判题成功/失败后都要调，否则 RabbitMQ 不放行。
 */
void ack_judge_task(const JudgeTaskMessage& msg);

/**
 * @brief 当前是否走 RabbitMQ。首次 publish/pop 时探测。
 * @return 1=已连上 Broker，0=进程内降级
 */
bool mq_using_rabbit();

}  // namespace nloj::common
