#include "nloj/user/module.h"
#include "nloj/user/auth_crypto.h"
#include "nloj/common/config.h"
#include "nloj/common/mysql.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <vector>

namespace nloj::user {
namespace {

int username_charset_ok(const std::string& username) {
    for (const char c : username) {
        const int ok = (c >= 'a' && c <= 'z')
                    || (c >= 'A' && c <= 'Z')
                    || (c >= '0' && c <= '9')
                    || c == '_';
        if (!ok) {
            return 0;
        }
    }
    return 1;
}

}  // namespace

const char* module_name() {
    return "nl-user";
}

std::int64_t register_user(const std::string& username,
                           const std::string& password,
                           nloj::common::AppError* err) {
    // 校验长度与字符集 → 查重 username → PBKDF2 哈希 → INSERT → 返回 insert_id

    // 校验长度与字符集
    if (username.size() < 3 || username.size() > 32
     || password.size() < 6 || password.size() > 64
     || !username_charset_ok(username)) {
        nloj::common::set_error(err, nloj::common::AppError::InvalidArgument);
        return -1;
    }

    nloj::common::MysqlConn conn;
    if (!conn.ok()) {
        nloj::common::set_error(err, nloj::common::AppError::Database);
        return -1;
    }

    // username 查重（预处理，只认未删除用户）
    std::vector<std::vector<std::string>> rows;
    if (!nloj::common::query_stmt(
            conn.get(),
            "SELECT id FROM `user` WHERE username=? AND deleted=0 LIMIT 1",
            {username},
            &rows,
            nullptr
        )) {
        nloj::common::set_error(err, nloj::common::AppError::Database);
        return -1;
    }
    if (!rows.empty()) {
        nloj::common::set_error(err, nloj::common::AppError::UsernameTaken);
        return -1;  // 用户名已存在
    }

    // PBKDF2-HMAC-SHA256，盐随机，哈希串写入 password_hash 列
    const std::string password_hash = crypto::hash_password(password);
    if (password_hash.empty()) {
        nloj::common::set_error(err, nloj::common::AppError::Internal);
        return -1;
    }

    std::uint64_t insert_id = 0;
    if (!nloj::common::query_stmt(
            conn.get(),
            "INSERT INTO `user` (username, password_hash) VALUES (?, ?)",
            {username, password_hash},
            nullptr,
            &insert_id
        )) {
        nloj::common::set_error(err, nloj::common::AppError::Database);
        return -1;
    }

    nloj::common::set_error(err, nloj::common::AppError::Ok);
    return static_cast<std::int64_t>(insert_id);
}

LoginResult login_user(const std::string& username,
                       const std::string& password,
                       nloj::common::AppError* err) {
    // 按 username 查库 → 比对密码哈希 → 签发 HS256 JWT（payload: uid/role/name）

    nloj::common::MysqlConn conn;
    if (!conn.ok()) {
        nloj::common::set_error(err, nloj::common::AppError::Database);
        return {};
    }

    std::vector<std::vector<std::string>> rows;
    if (!nloj::common::query_stmt(
            conn.get(),
            "SELECT id, password_hash, role, create_time FROM `user` "
            "WHERE username=? AND deleted=0 LIMIT 1",
            {username},
            &rows,
            nullptr
        )) {
        nloj::common::set_error(err, nloj::common::AppError::Database);
        return {};
    }
    if (rows.empty() || rows[0].size() < 2 || rows[0][0].empty() || rows[0][1].empty()) {
        nloj::common::set_error(err, nloj::common::AppError::WrongPassword);
        return {};  // 不存在或已逻辑删除，对外与错密相同
    }

    const std::int64_t user_id = std::stoll(rows[0][0]);
    const std::string stored_hash = rows[0][1];
    const std::string role = rows[0].size() > 2 && !rows[0][2].empty() ? rows[0][2] : "user";
    const std::string create_time = rows[0].size() > 3 ? rows[0][3] : "";

    if (!crypto::verify_password(password, stored_hash)) {
        nloj::common::set_error(err, nloj::common::AppError::WrongPassword);
        return {};
    }

    LoginResult result;
    result.token = crypto::sign_hs256_jwt(
        user_id, username, role, nloj::common::app_config().jwt_secret
    );
    if (result.token.empty()) {
        nloj::common::set_error(err, nloj::common::AppError::Internal);
        return {};
    }
    result.user.id = user_id;
    result.user.username = username;
    result.user.role = role;
    result.user.create_time = create_time;
    nloj::common::set_error(err, nloj::common::AppError::Ok);
    return result;
}

AuthUser verify_token(const std::string& token) {
    // 用 jwt_secret 验签、查过期时间 → 解析 uid/role → 填 AuthUser

    std::string payload_json;
    if (!crypto::verify_hs256_signature(
            token, payload_json, nloj::common::app_config().jwt_secret
        )) {
        return {};
    }

    const nlohmann::json j = nlohmann::json::parse(payload_json, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return {};
    }
    if (!j.contains("exp") || !j["exp"].is_number_integer()) {
        return {};
    }
    const std::int64_t exp = j["exp"].get<std::int64_t>();
    const auto now = static_cast<std::int64_t>(
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())
    );
    if (now >= exp) {
        return {};
    }

    AuthUser user;
    if (!j.contains("uid") || !j["uid"].is_string()) {
        return {};
    }
    try {
        user.id = std::stoll(j["uid"].get<std::string>());
    } catch (...) {
        return {};
    }
    user.username = j.value("name", "");
    user.role = j.value("role", "");
    if (user.username.empty() || user.role.empty()) {
        return {};
    }
    return user;
}

AuthUser get_current_user(const std::string& user_id) {
    // SELECT 未删除用户；不存在则返回空 AuthUser（对应 404）

    std::int64_t id = 0;
    try {
        id = std::stoll(user_id);
    } catch (...) {
        return {};
    }

    nloj::common::MysqlConn conn;
    if (!conn.ok()) {
        return {};
    }

    const std::string select_sql = "SELECT id, username, role, create_time FROM `user` WHERE id="
                                  + std::to_string(id)
                                  + " AND deleted=0 LIMIT 1";
    MYSQL_RES* select_result = nloj::common::query_select(conn.get(), select_sql);
    if (select_result == nullptr) {
        return {};
    }
    if (mysql_num_rows(select_result) == 0) {
        mysql_free_result(select_result);
        return {};
    }

    MYSQL_ROW row = mysql_fetch_row(select_result);
    if (row == nullptr || row[0] == nullptr || row[1] == nullptr) {
        mysql_free_result(select_result);
        return {};
    }

    AuthUser user;
    user.id = std::stoll(row[0]);
    user.username = row[1];
    user.role = row[2] != nullptr ? row[2] : "user";
    user.create_time = row[3] != nullptr ? row[3] : "";
    mysql_free_result(select_result);
    return user;
}

}  // namespace nloj::user
