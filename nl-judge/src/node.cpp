#include "nloj/judge/node.h"
#include "nloj/judge/load_balance.h"
#include "nloj/judge/module.h"
#include "nloj/common/log.h"
#include "nloj/common/mq.h"
#include "nloj/common/redis.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <chrono>
#include <cstdint>
#include <sstream>
#include <string>
#include <thread>

namespace nloj::judge {
namespace {

constexpr const char* kHeartbeatPrefix = "nloj:judge:node:";
constexpr const char* kLoadPrefix = "nloj:judge:load:";
constexpr int kHeartbeatTtlSec = 15;
constexpr int kHeartbeatEveryMs = 5000;
constexpr int kReclaimEveryMs = 30000;
constexpr int kStaleJudgingSec = 180;
constexpr int kPopIdleMs = 50;

std::string make_node_id() {
    std::ostringstream o;
#ifdef _WIN32
    char host[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
    if (!GetComputerNameA(host, &n)) {
        o << "win";
    } else {
        o << host;
    }
    o << '-' << GetCurrentProcessId();
#else
    char host[256];
    if (gethostname(host, sizeof(host)) != 0) {
        o << "node";
    } else {
        o << host;
    }
    o << '-' << getpid();
#endif
    return o.str();
}

}  // namespace

int write_judge_heartbeat(const std::string& node_id, int ttl_sec) {
    if (node_id.empty() || ttl_sec <= 0) {
        return 0;
    }
    const std::string key = std::string(kHeartbeatPrefix) + node_id;
    return nloj::common::redis_set_ex(key, "1", ttl_sec);
}

std::vector<std::string> list_judge_nodes() {
    std::vector<std::string> keys;
    std::vector<std::string> ids;
    if (!nloj::common::redis_keys(std::string(kHeartbeatPrefix) + "*", keys)) {
        return ids;
    }
    const std::size_t prefix_n = std::string(kHeartbeatPrefix).size();
    for (const auto& key : keys) {
        if (key.size() > prefix_n) {
            ids.push_back(key.substr(prefix_n));
        }
    }
    return ids;
}

int write_judge_load(const std::string& node_id, int load, int ttl_sec) {
    if (node_id.empty() || ttl_sec <= 0) {
        return 0;
    }
    if (load < 0) {
        load = 0;
    }
    const std::string key = std::string(kLoadPrefix) + node_id;
    return nloj::common::redis_set_ex(key, std::to_string(load), ttl_sec);
}

int least_loaded_online_node(std::string& out) {
    // 列在线节点 -> 读各自负载（没有 key 当 0）-> LoadBalance::pick

    const std::vector<std::string> ids = list_judge_nodes();
    if (ids.empty()) {
        return 0;
    }
    LoadBalance lb;
    const std::string prefix(kLoadPrefix);
    for (const auto& id : ids) {
        int load = 0;
        std::string raw;
        if (nloj::common::redis_get(prefix + id, raw)) {
            try {
                load = std::stoi(raw);
            } catch (...) {
                load = 0;
            }
            if (load < 0) {
                load = 0;
            }
        }
        lb.upsert(id, load, 1);
    }
    return lb.pick(out);
}

HeartbeatReporter::HeartbeatReporter(std::string node_id)
    : node_id_(std::move(node_id)) {
}

HeartbeatReporter::~HeartbeatReporter() {
    stop();
}

void HeartbeatReporter::start() {
    if (running_.exchange(1) == 1) {
        return;
    }
    write_judge_heartbeat(node_id_, kHeartbeatTtlSec);
    write_judge_load(node_id_, load_.load(), kHeartbeatTtlSec);
    th_ = std::thread([this] {
        while (running_.load() == 1) {
            write_judge_heartbeat(node_id_, kHeartbeatTtlSec);
            write_judge_load(node_id_, load_.load(), kHeartbeatTtlSec);
            for (int i = 0; i < kHeartbeatEveryMs / 50 && running_.load() == 1; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    });
}

void HeartbeatReporter::stop() {
    if (running_.exchange(0) == 0) {
        if (th_.joinable()) {
            th_.join();
        }
        return;
    }
    if (th_.joinable()) {
        th_.join();
    }
    const std::string key = std::string(kHeartbeatPrefix) + node_id_;
    nloj::common::redis_del(key);
    nloj::common::redis_del(std::string(kLoadPrefix) + node_id_);
}

const std::string& HeartbeatReporter::node_id() const {
    return node_id_;
}

void HeartbeatReporter::set_load(int load) {
    if (load < 0) {
        load = 0;
    }
    load_.store(load);
    write_judge_load(node_id_, load, kHeartbeatTtlSec);
}

int HeartbeatReporter::load() const {
    return load_.load();
}

JudgeNode::JudgeNode()
    : node_id_(make_node_id()), hb_(node_id_) {
}

JudgeNode::~JudgeNode() {
    request_stop();
}

void JudgeNode::run() {
    // 心跳 -> 循环：超时回收 -> 取任务 -> 负载不归自己则放回 -> 判题 -> ACK

    stop_.store(0);
    hb_.start();
    nloj::common::log_info("nloj_judge_node " + node_id_ + " running");
    auto last_reclaim = std::chrono::steady_clock::now();

    while (stop_.load() == 0) {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_reclaim >= std::chrono::milliseconds(kReclaimEveryMs)) {
            const int n = reclaim_stale_judging(kStaleJudgingSec);
            if (n > 0) {
                nloj::common::log_warn("reclaimed " + std::to_string(n) + " stale JUDGING");
            }
            last_reclaim = now;
        }

        nloj::common::JudgeTaskMessage task;
        if (nloj::common::try_pop_judge_task(task)) {
            // 多节点且走 RabbitMQ 时，把任务让给当前负载更低的节点
            std::string picked;
            if (nloj::common::mq_using_rabbit()
             && least_loaded_online_node(picked) == 1
             && picked != node_id_) {
                nloj::common::JudgeTaskMessage again = task;
                again.delivery_tag = 0;
                if (nloj::common::publish_judge_task(again)) {
                    nloj::common::ack_judge_task(task);
                    nloj::common::log_info(
                        "load balance defer submissionId="
                        + std::to_string(task.submission_id)
                        + " to " + picked
                    );
                    std::this_thread::sleep_for(std::chrono::milliseconds(kPopIdleMs));
                    continue;
                }
            }

            hb_.set_load(hb_.load() + 1);
            nloj::common::log_info(
                "judge task begin submissionId=" + std::to_string(task.submission_id)
                + " node=" + node_id_ + " load=" + std::to_string(hb_.load())
            );
            run_judge_task(task.submission_id);
            nloj::common::ack_judge_task(task);
            hb_.set_load(hb_.load() - 1);
            nloj::common::log_info(
                "judge task end submissionId=" + std::to_string(task.submission_id)
            );
            continue;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kPopIdleMs));
    }

    hb_.stop();
}

void JudgeNode::request_stop() {
    stop_.store(1);
}

const std::string& JudgeNode::node_id() const {
    return node_id_;
}

}  // namespace nloj::judge
