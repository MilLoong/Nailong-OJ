#include "nloj/common/module.h"
#include "nloj/judge/module.h"
#include "nloj/problem/module.h"
#include "nloj/submit/module.h"
#include "nloj/user/module.h"
#include "server.h"

#include <iostream>
#include <thread>

namespace {

constexpr const char* kHost = "0.0.0.0";
constexpr int kPort = 8080;  // 与 api.md 一致

}  // namespace

int main() {
    httplib::Server svr;
    nloj::api::register_http_routes(svr);

    if (nloj::api::embed_judge_worker_enabled()) {
        std::thread worker(nloj::api::judge_worker_loop);
        worker.detach();
    }

    std::cout << "nloj_api listening on http://" << kHost << ":" << kPort << '\n'
              << "  docs  http://127.0.0.1:" << kPort << "/api/docs\n"
              << "  embedWorker=" << (nloj::api::embed_judge_worker_enabled() ? "1" : "0") << '\n'
              << "  " << nloj::common::module_name() << '\n'
              << "  " << nloj::user::module_name() << '\n'
              << "  " << nloj::problem::module_name() << '\n'
              << "  " << nloj::submit::module_name() << '\n'
              << "  " << nloj::judge::module_name() << '\n';

    if (!svr.listen(kHost, kPort)) {
        std::cerr << "listen failed on port " << kPort << '\n';
        return 1;
    }
    return 0;
}
