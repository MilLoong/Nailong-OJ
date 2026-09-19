#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace nloj::judge {

/**
 * @brief 写一条节点心跳。
 * @param node_id 写入 key nloj:judge:node:{id}，SET EX ttl
 * @return 成功 1
 */
int write_judge_heartbeat(const std::string& node_id, int ttl_sec);

/**
 * @brief 当前心跳未过期的节点 id 列表（读 Redis KEYS）。
 */
std::vector<std::string> list_judge_nodes();

/**
 * @brief 写节点当前任务数，key nloj:judge:load:{id}，TTL 与心跳相同。
 * @return 成功 1
 */
int write_judge_load(const std::string& node_id, int load, int ttl_sec);

/**
 * @brief 在心跳仍在的节点里选负载最低的一台。
 * @param[out] out 选中的节点 id
 * @return 选出返回 1；没有在线节点或 Redis 不可用返回 0
 */
int least_loaded_online_node(std::string& out);

/**
 * @brief Redis TTL 心跳。start() 后后台续约，stop() 删 key 并停线程。
 */
class HeartbeatReporter {
public:
    explicit HeartbeatReporter(std::string node_id);
    ~HeartbeatReporter();

    HeartbeatReporter(const HeartbeatReporter&) = delete;
    HeartbeatReporter& operator=(const HeartbeatReporter&) = delete;

    void start();
    void stop();
    const std::string& node_id() const;

    /**
     * @brief 更新本节点正在跑的任务数，并立刻写 Redis。
     */
    void set_load(int load);
    int load() const;

private:
    std::string node_id_;
    std::atomic<int> running_{0};
    std::atomic<int> load_{0};
    std::thread th_;
};

/**
 * @brief 独立判题进程里的聚合体：心跳、最小负载、超时回收、取任务。
 */
class JudgeNode {
public:
    JudgeNode();
    ~JudgeNode();

    JudgeNode(const JudgeNode&) = delete;
    JudgeNode& operator=(const JudgeNode&) = delete;

    /**
     * @brief 阻塞直到 request_stop()。
     */
    void run();
    void request_stop();
    const std::string& node_id() const;

private:
    std::string node_id_;
    HeartbeatReporter hb_;
    std::atomic<int> stop_{0};
};

}  // namespace nloj::judge
