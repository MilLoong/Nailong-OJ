#include "nloj/common/mysql.h"

#include <iostream>

namespace nloj::common {
namespace {

void log_mysql_error(MYSQL* conn) {
    std::cerr << mysql_error(conn) << std::endl;
}

}  // namespace

std::string escape_sql(MYSQL* conn, const std::string& raw) {
    std::string escaped(raw.size() * 2 + 1, '\0');
    const auto n = mysql_real_escape_string(
        conn, escaped.data(), raw.c_str(), static_cast<unsigned long>(raw.size()));
    escaped.resize(n);
    return escaped;
}

MYSQL_RES* query_select(MYSQL* conn, const std::string& sql) {
    if (mysql_query(conn, sql.c_str()) != 0) {
        log_mysql_error(conn);
        return nullptr;
    }
    MYSQL_RES* result = mysql_store_result(conn);
    if (result == nullptr) {
        log_mysql_error(conn);
    }
    return result;
}

bool query_exec(MYSQL* conn, const std::string& sql) {
    if (mysql_query(conn, sql.c_str()) != 0) {
        log_mysql_error(conn);
        return 0;
    }
    return 1;
}

bool start_mysql(MYSQL& mysql) {
    if (mysql_init(&mysql) == nullptr) {
        return 0;
    }
    if (!mysql_real_connect(
            &mysql,
            "127.0.0.1",
            "nloj",
            "nloj123456",
            "nloj_db",
            3306,
            nullptr,
            0
        )) {
        log_mysql_error(&mysql);
        mysql_close(&mysql);
        return 0;
    }
    mysql_set_character_set(&mysql, "utf8mb4");
    return 1;
}

}  // namespace nloj::common
