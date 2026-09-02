#include "nloj/user/module.h"
#include "nloj/user/auth_crypto.h"

#include <chrono>
#include <iostream>
#include <string>

#if __has_include(<mysql/mysql.h>)
#include <mysql/mysql.h>
#else
#include <mysql.h>
#endif

namespace nloj::user {
namespace {

std::string json_get_string(const std::string& json, const std::string& key) {
    const std::string pat = "\"" + key + "\":\"";
    const std::size_t p = json.find(pat);
    if (p == std::string::npos) {
        return {};
    }
    const std::size_t start = p + pat.size();
    const std::size_t end = json.find('"', start);
    if (end == std::string::npos) {
        return {};
    }
    return json.substr(start, end - start);
}

std::int64_t json_get_int64(const std::string& json, const std::string& key) {
    const std::string pat = "\"" + key + "\":";
    const std::size_t p = json.find(pat);
    if (p == std::string::npos) {
        return -1;
    }
    try {
        return std::stoll(json.substr(p + pat.size()));
    } catch (...) {
        return -1;
    }
}

// 拼进引号前转义，防止 SQL 注入。缓冲区按 2*n+1 开。
std::string escape_sql(MYSQL* conn, const std::string& raw) {
    std::string escaped(raw.size() * 2 + 1, '\0');
    const auto n = mysql_real_escape_string(
        conn, escaped.data(), raw.c_str(), static_cast<unsigned long>(raw.size()));
    escaped.resize(n);
    return escaped;
}

void log_mysql_error(MYSQL* conn) {
    std::cerr << mysql_error(conn) << std::endl;
}

// SELECT：失败返回 nullptr，成功后调用方 mysql_free_result。
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

// INSERT / UPDATE：没有结果集。
bool query_exec(MYSQL* conn, const std::string& sql) {
    if (mysql_query(conn, sql.c_str()) != 0) {
        log_mysql_error(conn);
        return 0;
    }
    return 1;
}

// 初始化并连接 MySQL。失败时已 close，调用方不要再 close。
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

}  // namespace

const char* module_name() {
    return "nl-user";
}

std::int64_t register_user(const std::string& username, const std::string& password) {
    // 校验长度 → 查重 username → PBKDF2 哈希 → INSERT → 返回 insert_id

    // 校验长度
    if (username.size() < 3 || username.size() > 32
     || password.size() < 6 || password.size() > 64) {
        return -1;
    }

    // mysql 初始化、连接
    MYSQL mysql;
    if (!start_mysql(mysql)) {
        return -1;
    }

    // username 查重（转义后拼 SQL，只认未删除用户）
    const std::string escaped_username = escape_sql(&mysql, username);
    const std::string select_sql = "SELECT id FROM `user` WHERE username='"
                                  + escaped_username
                                  + "' AND deleted=0 LIMIT 1";

    MYSQL_RES* select_result = query_select(&mysql, select_sql);
    if (select_result == nullptr) {
        mysql_close(&mysql);
        return -1;
    }
    const bool exists = mysql_num_rows(select_result) > 0;
    mysql_free_result(select_result);
    if (exists) {
        mysql_close(&mysql);
        return -1;  // 用户名已存在
    }

    // PBKDF2-HMAC-SHA256，盐随机，哈希串写入 password_hash 列
    const std::string password_hash = crypto::hash_password(password);
    if (password_hash.empty()) {
        mysql_close(&mysql);
        return -1;
    }

    // 插入用户数据（只写 username / password_hash，其余列用表默认值）
    const std::string escaped_hash = escape_sql(&mysql, password_hash);
    const std::string insert_sql = "INSERT INTO `user` (username, password_hash) VALUES ('"
                                  + escaped_username + "', '" + escaped_hash + "')";

    if (!query_exec(&mysql, insert_sql)) {
        mysql_close(&mysql);
        return -1;
    }

    // 自增主键，作为注册接口返回的用户 id
    const std::int64_t user_id = static_cast<std::int64_t>(mysql_insert_id(&mysql));
    mysql_close(&mysql);
    return user_id;
}

LoginResult login_user(const std::string& username, const std::string& password) {
    // 按 username 查库 → 比对密码哈希 → 签发 HS256 JWT（payload: uid/role/name）

    // mysql 初始化、连接
    MYSQL mysql;
    if (!start_mysql(mysql)) {
        return {};
    }

    // 只查未删除用户；有行才说明账号有效（不必再读 deleted 列）
    const std::string escaped_username = escape_sql(&mysql, username);
    const std::string select_sql = "SELECT id, password_hash, role, create_time FROM `user` WHERE username='"
                                  + escaped_username
                                  + "' AND deleted=0 LIMIT 1";

    MYSQL_RES* select_result = query_select(&mysql, select_sql);
    if (select_result == nullptr) {
        mysql_close(&mysql);
        return {};
    }
    if (mysql_num_rows(select_result) == 0) {
        mysql_free_result(select_result);
        mysql_close(&mysql);
        return {};  // 不存在或已逻辑删除
    }

    MYSQL_ROW row = mysql_fetch_row(select_result);
    if (row == nullptr || row[0] == nullptr || row[1] == nullptr) {
        mysql_free_result(select_result);
        mysql_close(&mysql);
        return {};
    }

    // row[0] = id, row[1] = password_hash, row[2] = role, row[3] = create_time
    const std::int64_t user_id = std::stoll(row[0]);
    const std::string stored_hash = row[1];
    const std::string role = row[2] != nullptr ? row[2] : "user";
    const std::string create_time = row[3] != nullptr ? row[3] : "";
    mysql_free_result(select_result);
    mysql_close(&mysql);

    // 拆库存串，用原盐再算 PBKDF2，比较 digest
    if (!crypto::verify_password(password, stored_hash)) {
        return {};
    }

    LoginResult result;
    result.token = crypto::sign_hs256_jwt(user_id, username, role);
    if (result.token.empty()) {
        return {};
    }
    result.user.id = user_id;
    result.user.username = username;
    result.user.role = role;
    result.user.create_time = create_time;
    return result;
}

AuthUser verify_token(const std::string& token) {
    // 用 jwt_secret 验签、查过期时间 → 解析 uid/role → 填 AuthUser

    // jwt_secret 验签（同一把密钥重算 HMAC，和第三段比）
    std::string payload_json;
    if (!crypto::verify_hs256_signature(token, payload_json)) {
        return {};
    }

    // 过期时间
    const std::int64_t exp = json_get_int64(payload_json, "exp");
    const auto now = static_cast<std::int64_t>(
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())
    );
    if (exp < 0 || now >= exp) {
        return {};
    }

    // 解析 uid / name / role
    AuthUser user;
    try {
        user.id = std::stoll(json_get_string(payload_json, "uid"));
    } catch (...) {
        return {};
    }
    user.username = json_get_string(payload_json, "name");
    user.role = json_get_string(payload_json, "role");
    if (user.username.empty() || user.role.empty()) {
        return {};
    }
    return user;
}

AuthUser get_current_user(const std::string& user_id) {
    // SELECT 未删除用户；不存在则返回空 AuthUser（对应 404）

    // 入参转成 BIGINT
    std::int64_t id = 0;
    try {
        id = std::stoll(user_id);
    } catch (...) {
        return {};
    }

    // mysql 初始化、连接
    MYSQL mysql;
    if (!start_mysql(mysql)) {
        return {};
    }

    // SELECT 未删除用户（id 来自 verify_token，只认 deleted=0）
    const std::string select_sql = "SELECT id, username, role, create_time FROM `user` WHERE id="
                                  + std::to_string(id)
                                  + " AND deleted=0 LIMIT 1";

    MYSQL_RES* select_result = query_select(&mysql, select_sql);
    if (select_result == nullptr) {
        mysql_close(&mysql);
        return {};
    }
    if (mysql_num_rows(select_result) == 0) {
        mysql_free_result(select_result);
        mysql_close(&mysql);
        return {};  // 不存在或已逻辑删除
    }

    MYSQL_ROW row = mysql_fetch_row(select_result);
    if (row == nullptr || row[0] == nullptr || row[1] == nullptr) {
        mysql_free_result(select_result);
        mysql_close(&mysql);
        return {};
    }

    // row[0]=id, row[1]=username, row[2]=role, row[3]=create_time
    AuthUser user;
    user.id = std::stoll(row[0]);
    user.username = row[1];
    user.role = row[2] != nullptr ? row[2] : "user";
    user.create_time = row[3] != nullptr ? row[3] : "";
    mysql_free_result(select_result);
    mysql_close(&mysql);
    return user;
}

}  // namespace nloj::user
