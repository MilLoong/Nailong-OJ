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

private:
    std::string node_id_;
    std::atomic<int> running_{0};
    std::thread th_;
};

/**
 * @brief 独立判题进程里的聚合体：心跳 + 超时回收 + 竞争消费。
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
