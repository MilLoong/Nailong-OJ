#pragma once

#include <cstdint>
#include <string>

namespace nloj::common {

// 判题任务消息，对应 architecture 里的 JudgeTaskMessage。
struct JudgeTaskMessage {
    std::int64_t submission_id;  // 提交主键
    std::int64_t problem_id;     // 题目
    std::string language;        // CPP | JAVA | ...
};

// 投递判题任务。成功 1，失败 0。
// 优先连本机 RabbitMQ；连不上 Broker 则降级进程内队列。
bool publish_judge_task(const JudgeTaskMessage& msg);

// 非阻塞取一条。有消息返回 1 并写入 out；队列空返回 0。
bool try_pop_judge_task(JudgeTaskMessage& out);

// 阻塞取一条。队列空则等待，直到 publish 唤醒后再弹出。
void wait_pop_judge_task(JudgeTaskMessage& out);

// 当前是否走 RabbitMQ。1=已连上 Broker，0=进程内降级。首次 publish/pop 时探测。
bool mq_using_rabbit();

}  // namespace nloj::common
