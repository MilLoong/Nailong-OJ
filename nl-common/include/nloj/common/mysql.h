#pragma once

#include <string>

#if __has_include(<mysql/mysql.h>)
#include <mysql/mysql.h>
#else
#include <mysql.h>
#endif

namespace nloj::common {

// 拼进引号前转义，防止 SQL 注入。缓冲区按 2*n+1 开。
std::string escape_sql(MYSQL* conn, const std::string& raw);

// SELECT：失败返回 nullptr，成功后调用方 mysql_free_result。
MYSQL_RES* query_select(MYSQL* conn, const std::string& sql);

// INSERT / UPDATE：没有结果集。成功 1，失败 0。
bool query_exec(MYSQL* conn, const std::string& sql);

// 初始化并连接本机 nloj_db。失败时已 close，调用方不要再 close。
bool start_mysql(MYSQL& mysql);

}  // namespace nloj::common
