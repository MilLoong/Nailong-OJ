#include "nloj/common/config.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

namespace nloj::common {
namespace {

std::mutex g_mu;
AppConfig g_cfg;
int g_loaded = 0;

int file_exists(const std::string& path) {
    std::ifstream in(path);
    return in.good() ? 1 : 0;
}

int read_all_text(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return 0;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return 1;
}

void apply_string(std::string& dest, const nlohmann::json& obj, const char* key) {
    if (obj.contains(key) && obj[key].is_string()) {
        dest = obj[key].get<std::string>();
    }
}

void apply_int(int& dest, const nlohmann::json& obj, const char* key) {
    if (obj.contains(key) && obj[key].is_number_integer()) {
        dest = obj[key].get<int>();
    }
}

void apply_json(AppConfig& cfg, const nlohmann::json& root) {
    if (!root.is_object()) {
        return;
    }
    if (root.contains("server") && root["server"].is_object()) {
        const nlohmann::json& s = root["server"];
        apply_string(cfg.http_host, s, "host");
        apply_int(cfg.http_port, s, "port");
        apply_string(cfg.jwt_secret, s, "jwt_secret");
    }
    if (root.contains("mysql") && root["mysql"].is_object()) {
        const nlohmann::json& m = root["mysql"];
        apply_string(cfg.mysql_host, m, "host");
        apply_int(cfg.mysql_port, m, "port");
        apply_string(cfg.mysql_user, m, "user");
        apply_string(cfg.mysql_password, m, "password");
        apply_string(cfg.mysql_database, m, "database");
        apply_int(cfg.mysql_pool_size, m, "pool_size");
    }
    if (root.contains("redis") && root["redis"].is_object()) {
        const nlohmann::json& r = root["redis"];
        apply_string(cfg.redis_host, r, "host");
        apply_int(cfg.redis_port, r, "port");
    }
    if (root.contains("rabbitmq") && root["rabbitmq"].is_object()) {
        const nlohmann::json& q = root["rabbitmq"];
        apply_string(cfg.rabbit_host, q, "host");
        apply_int(cfg.rabbit_port, q, "port");
        apply_string(cfg.rabbit_user, q, "username");
        apply_string(cfg.rabbit_password, q, "password");
    }
}

void apply_env_str(std::string& dest, const char* name) {
    const char* v = std::getenv(name);
    if (v != nullptr && v[0] != '\0') {
        dest = v;
    }
}

void apply_env_int(int& dest, const char* name) {
    const char* v = std::getenv(name);
    if (v == nullptr || v[0] == '\0') {
        return;
    }
    try {
        dest = std::stoi(v);
    } catch (...) {
    }
}

void apply_env(AppConfig& cfg) {
    apply_env_str(cfg.http_host, "NLOJ_HTTP_HOST");
    apply_env_int(cfg.http_port, "NLOJ_HTTP_PORT");
    apply_env_str(cfg.jwt_secret, "NLOJ_JWT_SECRET");
    apply_env_str(cfg.mysql_host, "NLOJ_MYSQL_HOST");
    apply_env_int(cfg.mysql_port, "NLOJ_MYSQL_PORT");
    apply_env_str(cfg.mysql_user, "NLOJ_MYSQL_USER");
    apply_env_str(cfg.mysql_password, "NLOJ_MYSQL_PASSWORD");
    apply_env_str(cfg.mysql_database, "NLOJ_MYSQL_DATABASE");
    apply_env_int(cfg.mysql_pool_size, "NLOJ_MYSQL_POOL_SIZE");
    apply_env_str(cfg.redis_host, "NLOJ_REDIS_HOST");
    apply_env_int(cfg.redis_port, "NLOJ_REDIS_PORT");
    apply_env_str(cfg.rabbit_host, "NLOJ_RABBITMQ_HOST");
    apply_env_int(cfg.rabbit_port, "NLOJ_RABBITMQ_PORT");
    apply_env_str(cfg.rabbit_user, "NLOJ_RABBITMQ_USER");
    apply_env_str(cfg.rabbit_password, "NLOJ_RABBITMQ_PASSWORD");
}

AppConfig load_config() {
    AppConfig cfg;
    std::string path;
    const char* env_path = std::getenv("NLOJ_CONFIG");
    if (env_path != nullptr && env_path[0] != '\0' && file_exists(env_path)) {
        path = env_path;
    } else if (file_exists("config.json")) {
        path = "config.json";
    }
    if (!path.empty()) {
        std::string raw;
        if (read_all_text(path, raw)) {
            const nlohmann::json j = nlohmann::json::parse(raw, nullptr, false);
            if (!j.is_discarded()) {
                apply_json(cfg, j);
            }
        }
    }
    apply_env(cfg);
    if (cfg.mysql_pool_size < 1) {
        cfg.mysql_pool_size = 1;
    }
    if (cfg.http_port <= 0) {
        cfg.http_port = 8080;
    }
    return cfg;
}

}  // namespace

const AppConfig& app_config() {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_loaded) {
        g_cfg = load_config();
        g_loaded = 1;
    }
    return g_cfg;
}

void reset_app_config() {
    std::lock_guard<std::mutex> lock(g_mu);
    g_loaded = 0;
}

}  // namespace nloj::common
