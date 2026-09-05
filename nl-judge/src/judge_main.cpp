#include "nloj/judge/node.h"
#include "nloj/common/mq.h"
#include "nloj/common/redis.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <iostream>

namespace {

nloj::judge::JudgeNode* g_node = nullptr;

#ifdef _WIN32
BOOL WINAPI on_console_ctrl(DWORD) {
    if (g_node != nullptr) {
        g_node -> request_stop();
    }
    return TRUE;
}
#endif

}  // namespace

int main() {
    nloj::judge::JudgeNode node;
    g_node = &node;
#ifdef _WIN32
    SetConsoleCtrlHandler(on_console_ctrl, TRUE);
#endif

    std::cout << "nloj_judge_node id=" << node.node_id() << '\n'
              << "  rabbitmq=" << (nloj::common::mq_using_rabbit() ? "UP" : "DOWN") << '\n'
              << "  redis=" << (nloj::common::redis_using() ? "UP" : "DOWN") << '\n';
    if (!nloj::common::mq_using_rabbit()) {
        std::cout << "  warn: no RabbitMQ; this process cannot see nloj_api in-process queue\n";
    }

    node.run();
    g_node = nullptr;
    std::cout << "nloj_judge_node stopped\n";
    return 0;
}
