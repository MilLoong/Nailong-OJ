#include "nloj/judge/node.h"
#include "nloj/common/log.h"
#include "nloj/common/mq.h"
#include "nloj/common/redis.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <string>

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
    nloj::common::init_log_from_env();

    nloj::judge::JudgeNode node;
    g_node = &node;
#ifdef _WIN32
    SetConsoleCtrlHandler(on_console_ctrl, TRUE);
#endif

    nloj::common::log_info(
        "nloj_judge_node id=" + node.node_id()
        + " rabbitmq=" + std::string(nloj::common::mq_using_rabbit() ? "UP" : "DOWN")
        + " redis=" + std::string(nloj::common::redis_using() ? "UP" : "DOWN")
    );
    if (!nloj::common::mq_using_rabbit()) {
        nloj::common::log_warn(
            "no RabbitMQ; this process cannot see nloj_api in-process queue"
        );
    }

    node.run();
    g_node = nullptr;
    nloj::common::log_info("nloj_judge_node stopped");
    return 0;
}
