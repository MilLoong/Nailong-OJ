#include "nloj/judge/load_balance.h"

namespace nloj::judge {

void LoadBalance::upsert(const std::string& id, int load, int online) {
    if (id.empty()) {
        return;
    }
    if (load < 0) {
        load = 0;
    }
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& m : machines_) {
        if (m.id == id) {
            m.load = load;
            m.online = online ? 1 : 0;
            return;
        }
    }
    JudgeMachine m;
    m.id = id;
    m.load = load;
    m.online = online ? 1 : 0;
    machines_.push_back(m);
}

void LoadBalance::set_online(const std::string& id, int online) {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& m : machines_) {
        if (m.id == id) {
            m.online = online ? 1 : 0;
            return;
        }
    }
}

int LoadBalance::inc_load(const std::string& id) {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& m : machines_) {
        if (m.id == id) {
            m.load += 1;
            return m.load;
        }
    }
    return -1;
}

int LoadBalance::dec_load(const std::string& id) {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& m : machines_) {
        if (m.id == id) {
            if (m.load > 0) {
                m.load -= 1;
            }
            return m.load;
        }
    }
    return -1;
}

int LoadBalance::pick(std::string& out) const {
    // 跳过下线 -> 负载更小者优先 -> 负载相同取 id 更小者

    std::lock_guard<std::mutex> lock(mu_);
    int found = 0;
    int best_load = 0;
    std::string best_id;
    for (const auto& m : machines_) {
        if (m.online != 1) {
            continue;
        }
        if (found == 0
         || m.load < best_load
         || (m.load == best_load && m.id < best_id)) {
            found = 1;
            best_load = m.load;
            best_id = m.id;
        }
    }
    if (found == 0) {
        return 0;
    }
    out = best_id;
    return 1;
}

}  // namespace nloj::judge
