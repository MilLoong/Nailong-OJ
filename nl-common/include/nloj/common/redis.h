#pragma once

#include <string>
#include <vector>

namespace nloj::common {

/**
 * @brief 本机 Redis 字符串缓存（题目详情用）。
 *
 * RESP 直连 6379，连不上时读写返回 0，业务走 MySQL；
 * 断线后按固定冷却间隔自动重连探测，Redis 恢复即重新启用缓存。
 */

/**
 * @brief 当前是否已连上 Redis。首次调用时探测，之后故障自动重连。
 * @return 1=可用，0=未连上
 */
int redis_using();

/**
 * @brief GET。
 * @return 命中（含主动写入的空值）返回 1 并写入 out；未命中或不可用返回 0
 */
int redis_get(const std::string& key, std::string& out);

/**
 * @brief SET key value EX ttl。
 * @return 成功 1
 */
int redis_set_ex(const std::string& key, const std::string& value, int ttl_sec);

/**
 * @brief SET key value EX ttl NX。
 * @return 抢到锁返回 1，已存在或不可用返回 0
 */
int redis_set_nx_ex(const std::string& key, const std::string& value, int ttl_sec);

/**
 * @brief DEL。key 不存在也视为成功。
 * @return Redis 不可用返回 0
 */
int redis_del(const std::string& key);

/**
 * @brief KEYS pattern。
 * @return 成功 1（out 可为空）；Redis 不可用返回 0
 */
int redis_keys(const std::string& pattern, std::vector<std::string>& out);

}  // namespace nloj::common
