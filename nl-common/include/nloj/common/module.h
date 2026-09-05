#pragma once

namespace nloj::common {

const char* module_name();

// MySQL 连接 / 转义 / 查询：见 nloj/common/mysql.h。
// 判题任务队列（RabbitMQ / 进程内降级）：见 nloj/common/mq.h。
// 题目缓存（Redis String，连不上则跳过）：见 nloj/common/redis.h。

}  // namespace nloj::common
