#pragma once

#include <string>

namespace nloj::common {

/**
 * @brief 日志级别。低于当前阈值的消息会被丢掉。
 */
enum class LogLevel {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
};

/**
 * @brief 从环境变量初始化：NLOJ_LOG_LEVEL、NLOJ_LOG_FILE。
 *
 * NLOJ_LOG_LEVEL：debug / info / warn / error（默认 info）。
 * NLOJ_LOG_FILE：可选，追加写入该路径；未设则只打 stderr。
 */
void init_log_from_env();

/**
 * @brief 运行时改级别（单测 / 调试）。
 */
void set_log_level(LogLevel level);

/**
 * @brief 运行时改文件路径；空串表示不再写文件。
 */
void set_log_file(const std::string& path);

/**
 * @brief 写一条日志。自动带时间戳和级别名。
 */
void log_message(LogLevel level, const std::string& msg);

void log_debug(const std::string& msg);
void log_info(const std::string& msg);
void log_warn(const std::string& msg);
void log_error(const std::string& msg);

}  // namespace nloj::common
