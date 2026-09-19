#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace nloj::judge {

/**
 * @brief 一台判题机的当前负载。
 */
struct JudgeMachine {
    std::string id;  ///< 节点 id，与心跳 key 后缀一致
    int load = 0;    ///< 正在处理的任务数
    int online = 1;  ///< 1=可派活，0=下线
};

/**
 * @brief 在在线节点里选当前任务数最少的一台。
 *
 * 相同任务数时取 id 字典序较小者，结果稳定。
 */
class LoadBalance {
public:
    /**
     * @brief 登记或覆盖一台机器。
     */
    void upsert(const std::string& id, int load, int online);

    /**
     * @brief 只改在线标记。没有这台机器则忽略。
     */
    void set_online(const std::string& id, int online);

    /**
     * @brief 任务开始，负载 +1。
     * @return 新负载；没有这台机器返回 -1
     */
    int inc_load(const std::string& id);

    /**
     * @brief 任务结束，负载 -1，不低于 0。
     * @return 新负载；没有这台机器返回 -1
     */
    int dec_load(const std::string& id);

    /**
     * @brief 选出在线且负载最低的节点。
     * @param[out] out 选中的 id
     * @return 选出返回 1；没有在线机器返回 0
     */
    int pick(std::string& out) const;

private:
    mutable std::mutex mu_;
    std::vector<JudgeMachine> machines_;
};

}  // namespace nloj::judge
