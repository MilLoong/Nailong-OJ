#include "nloj/judge/node.h"
#include "nloj/judge/module.h"
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
#include <iostream>
#include <sstream>

namespace nloj::judge {
namespace {

constexpr const char* kHeartbeatPrefix = "nloj:judge:node:";
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
    th_ = std::thread([this] {
        while (running_.load() == 1) {
            write_judge_heartbeat(node_id_, kHeartbeatTtlSec);
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
}

const std::string& HeartbeatReporter::node_id() const {
    return node_id_;
}

JudgeNode::JudgeNode()
    : node_id_(make_node_id()), hb_(node_id_) {
}

JudgeNode::~JudgeNode() {
    request_stop();
}

void JudgeNode::run() {
    // 心跳 -> 循环：超时回收 -> 取任务 -> 判题 -> ACK

    stop_.store(0);
    hb_.start();
    std::cout << "nloj_judge_node " << node_id_ << " running" << std::endl;
    auto last_reclaim = std::chrono::steady_clock::now();

    while (stop_.load() == 0) {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_reclaim >= std::chrono::milliseconds(kReclaimEveryMs)) {
            const int n = reclaim_stale_judging(kStaleJudgingSec);
            if (n > 0) {
                std::cout << "reclaimed " << n << " stale JUDGING" << std::endl;
            }
            last_reclaim = now;
        }

        nloj::common::JudgeTaskMessage task;
        if (nloj::common::try_pop_judge_task(task)) {
            run_judge_task(task.submission_id);
            nloj::common::ack_judge_task(task);
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
