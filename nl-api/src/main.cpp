#include "nloj/common/module.h"
#include "nloj/judge/module.h"
#include "nloj/problem/module.h"
#include "nloj/submit/module.h"
#include "nloj/user/module.h"
#include "nloj/common/config.h"
#include "nloj/common/log.h"
#include "server.h"

#include <string>
#include <thread>

int main() {
    nloj::common::init_log_from_env();

    httplib::Server svr;
    nloj::api::register_http_routes(svr);

    if (nloj::api::embed_judge_worker_enabled()) {
        std::thread worker(nloj::api::judge_worker_loop);
        worker.detach();
        nloj::common::log_info("embedWorker=1");
    } else {
        nloj::common::log_info("embedWorker=0 (use nloj_judge_node)");
    }

    const nloj::common::AppConfig& cfg = nloj::common::app_config();

    // JWT 密钥缺失直接拒绝启动：crypto 层已不再兜底开发默认密钥
    if (cfg.jwt_secret.empty()) {
        nloj::common::log_error(
            "jwt_secret 不能为空：请设置 config.json 的 server.jwt_secret 或 NLOJ_JWT_SECRET"
        );
        return 1;
    }
    if (cfg.jwt_secret == "nloj-dev-secret-change-me") {
        nloj::common::log_warn(
            "正在使用开发默认 jwt_secret，对外部署前务必用 NLOJ_JWT_SECRET 更换"
        );
    }

    nloj::common::log_info(
        std::string("nloj_api listening on http://") + cfg.http_host + ":"
        + std::to_string(cfg.http_port)
        + " docs=http://127.0.0.1:" + std::to_string(cfg.http_port) + "/api/docs"
        + " " + nloj::common::module_name()
        + " " + nloj::user::module_name()
        + " " + nloj::problem::module_name()
        + " " + nloj::submit::module_name()
        + " " + nloj::judge::module_name()
    );

    if (!svr.listen(cfg.http_host.c_str(), cfg.http_port)) {
        nloj::common::log_error(
            "listen failed on port " + std::to_string(cfg.http_port)
        );
        return 1;
    }
    return 0;
}
