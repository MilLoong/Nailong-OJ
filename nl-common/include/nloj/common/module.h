#pragma once

namespace nloj::common {

/**
 * @brief 公共模块名。
 *
 * MySQL 连接池 / 转义 / 预处理：见 nloj/common/mysql.h。
 * 配置：见 nloj/common/config.h。领域错误码：见 nloj/common/error.h。
 * 判题任务队列（RabbitMQ / 进程内降级）：见 nloj/common/mq.h。
 * 题目缓存（Redis String，连不上则跳过）：见 nloj/common/redis.h。
 */
const char* module_name();

}  // namespace nloj::common
