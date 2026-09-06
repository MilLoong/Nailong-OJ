#pragma once

#include "nloj/common/error.h"

#include <cstdint>
#include <string>

namespace nloj::user {

const char* module_name();

/**
 * @brief 登录用户视图，对应 GET /api/v1/users/me 的 data。
 */
struct AuthUser {
    std::int64_t id;          ///< 用户主键
    std::string username;     ///< 用户名
    std::string role;         ///< user | admin
    std::string create_time;  ///< 注册时间，ISO 风格
};

/**
 * @brief 登录成功结果，对应 POST /api/v1/auth/login 的 data。
 */
struct LoginResult {
    std::string token;  ///< JWT，客户端放进 Authorization: Bearer
    AuthUser user;
};

/**
 * @brief 注册。
 * @param username 3-32、字母数字下划线且唯一
 * @param password 6-64
 * @param err 失败原因；可为 nullptr
 * @return 成功返回新用户 id；失败返回 -1
 */
std::int64_t register_user(const std::string& username,
                           const std::string& password,
                           nloj::common::AppError* err = nullptr);

/**
 * @brief 登录并签发 JWT。
 * @param err 失败原因；错密为 WrongPassword
 * @return 失败时 token 为空
 */
LoginResult login_user(const std::string& username,
                       const std::string& password,
                       nloj::common::AppError* err = nullptr);

/**
 * @brief 校验 JWT。给 nl-api 拦截器用。
 * @return 成功返回 token 内用户信息；无效或过期时 id==0
 */
AuthUser verify_token(const std::string& token);

/**
 * @brief 按用户 id 查当前用户资料。
 * @param user_id 来自 verify_token，不是 HTTP 直接传入
 */
AuthUser get_current_user(const std::string& user_id);

}  // namespace nloj::user
