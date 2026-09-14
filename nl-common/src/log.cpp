#include "nloj/common/log.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace nloj::common {
namespace {

std::mutex g_mu;
LogLevel g_level = LogLevel::Info;
std::string g_file_path;
int g_env_inited = 0;

const char* level_name(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
    }
    return "INFO";
}

LogLevel parse_level(const char* raw) {
    if (raw == nullptr || raw[0] == '\0') {
        return LogLevel::Info;
    }
    const std::string s(raw);
    if (s == "debug" || s == "DEBUG") {
        return LogLevel::Debug;
    }
    if (s == "warn" || s == "WARN" || s == "warning" || s == "WARNING") {
        return LogLevel::Warn;
    }
    if (s == "error" || s == "ERROR") {
        return LogLevel::Error;
    }
    return LogLevel::Info;
}

std::string env_str(const char* key) {
#ifdef _WIN32
    char buf[1024];
    const DWORD n = GetEnvironmentVariableA(key, buf, static_cast<DWORD>(sizeof(buf)));
    if (n == 0 || n >= sizeof(buf)) {
        return {};
    }
    return std::string(buf, n);
#else
    const char* raw = std::getenv(key);
    if (raw == nullptr) {
        return {};
    }
    return std::string(raw);
#endif
}

std::string now_text() {
    using clock = std::chrono::system_clock;
    const auto tp = clock::now();
    const std::time_t sec = clock::to_time_t(tp);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        tp.time_since_epoch()
                    ).count()
                    % 1000;
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &sec);
#else
    localtime_r(&sec, &tm);
#endif
    char buf[64];
    const int n = static_cast<int>(std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm));
    if (n <= 0) {
        return "unknown-time";
    }
    char out[80];
    std::snprintf(out, sizeof(out), "%s.%03d", buf, static_cast<int>(ms));
    return out;
}

void apply_env_unlocked() {
    const std::string level_raw = env_str("NLOJ_LOG_LEVEL");
    g_level = parse_level(level_raw.c_str());
    g_file_path = env_str("NLOJ_LOG_FILE");
    g_env_inited = 1;
}

}  // namespace

void init_log_from_env() {
    std::lock_guard<std::mutex> lock(g_mu);
    apply_env_unlocked();
}

void set_log_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_level = level;
    g_env_inited = 1;
}

void set_log_file(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_file_path = path;
    g_env_inited = 1;
}

void log_message(LogLevel level, const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_env_inited) {
        apply_env_unlocked();
    }
    if (static_cast<int>(level) < static_cast<int>(g_level)) {
        return;
    }
    const std::string line = now_text() + " [" + level_name(level) + "] " + msg + "\n";
    std::cerr << line;
    if (!g_file_path.empty()) {
        std::ofstream out(g_file_path, std::ios::app | std::ios::binary);
        if (out) {
            out << line;
        }
    }
}

void log_debug(const std::string& msg) {
    log_message(LogLevel::Debug, msg);
}

void log_info(const std::string& msg) {
    log_message(LogLevel::Info, msg);
}

void log_warn(const std::string& msg) {
    log_message(LogLevel::Warn, msg);
}

void log_error(const std::string& msg) {
    log_message(LogLevel::Error, msg);
}

}  // namespace nloj::common
