#pragma once

#include <string>

namespace nloj::common {

/**
 * @brief 运行时配置。默认与本机开发环境一致；可被 config.json / 环境变量覆盖。
 */
struct AppConfig {
    std::string http_host = "0.0.0.0";
    int http_port = 8080;
    std::string jwt_secret = "nloj-dev-secret-change-me";

    std::string mysql_host = "127.0.0.1";
    int mysql_port = 3306;
    std::string mysql_user = "nloj";
    std::string mysql_password = "nloj123456";
    std::string mysql_database = "nloj_db";
    int mysql_pool_size = 8;

    std::string redis_host = "127.0.0.1";
    int redis_port = 6379;

    std::string rabbit_host = "127.0.0.1";
    int rabbit_port = 5672;
    std::string rabbit_user = "nloj";
    std::string rabbit_password = "nloj123456";
};

/**
 * @brief 首次调用时加载：可选 JSON（NLOJ_CONFIG 或 ./config.json）-> 环境变量覆盖。
 */
const AppConfig& app_config();

/**
 * @brief 测试用：清掉缓存，下次 app_config() 重新读。
 */
void reset_app_config();

}  // namespace nloj::common
