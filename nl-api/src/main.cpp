#include "nloj/common/module.h"
#include "nloj/judge/module.h"
#include "nloj/problem/module.h"
#include "nloj/submit/module.h"
#include "nloj/user/module.h"
#include "nloj/common/config.h"
#include "server.h"

#include <iostream>
#include <thread>

int main() {
    httplib::Server svr;
    nloj::api::register_http_routes(svr);

    if (nloj::api::embed_judge_worker_enabled()) {
        std::thread worker(nloj::api::judge_worker_loop);
        worker.detach();
    }

    const nloj::common::AppConfig& cfg = nloj::common::app_config();

    // JWT 密钥缺失直接拒绝启动：crypto 层已不再兜底开发默认密钥
    if (cfg.jwt_secret.empty()) {
        std::cerr << "jwt_secret 不能为空：请设置 config.json 的 server.jwt_secret 或 NLOJ_JWT_SECRET\n";
        return 1;
    }
    if (cfg.jwt_secret == "nloj-dev-secret-change-me") {
        std::cerr << "warning: 正在使用开发默认 jwt_secret，对外部署前务必用 NLOJ_JWT_SECRET 更换\n";
    }

    std::cout << "nloj_api listening on http://" << cfg.http_host << ":" << cfg.http_port << '\n'
              << "  docs  http://127.0.0.1:" << cfg.http_port << "/api/docs\n"
              << "  embedWorker=" << (nloj::api::embed_judge_worker_enabled() ? "1" : "0") << '\n'
              << "  " << nloj::common::module_name() << '\n'
              << "  " << nloj::user::module_name() << '\n'
              << "  " << nloj::problem::module_name() << '\n'
              << "  " << nloj::submit::module_name() << '\n'
              << "  " << nloj::judge::module_name() << '\n';

    if (!svr.listen(cfg.http_host.c_str(), cfg.http_port)) {
        std::cerr << "listen failed on port " << cfg.http_port << '\n';
        return 1;
    }
    return 0;
}
