#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

// 预处理：sql 里用 ? 绑定字符串。SELECT 填 rows；INSERT 可拿 insert_id。
// 成功 1，失败 0。
int query_stmt(MYSQL* conn,
               const char* sql,
               const std::vector<std::string>& args,
               std::vector<std::vector<std::string>>* rows,
               std::uint64_t* insert_id);

// 从连接池借一条；析构归还。失败时 ok()==0。
class MysqlConn {
public:
    MysqlConn();
    ~MysqlConn();
    MysqlConn(const MysqlConn&) = delete;
    MysqlConn& operator=(const MysqlConn&) = delete;
    int ok() const;
    MYSQL* get();

private:
    MYSQL* conn_;
};

// 独立短连接（测试夹具）。失败时已 close，调用方不要再 close。
bool start_mysql(MYSQL& mysql);

}  // namespace nloj::common
