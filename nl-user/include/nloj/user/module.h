#pragma once

#include "nloj/common/error.h"

#include <cstdint>
#include <string>

namespace nloj::user {

const char* module_name();

// 登录用户视图，对应 GET /api/v1/users/me 的 data。
struct AuthUser {
    std::int64_t id;          // 用户主键
    std::string username;     // 用户名
    std::string role;         // user | admin
    std::string create_time;  // 注册时间，ISO 风格
};

// 登录成功结果，对应 POST /api/v1/auth/login 的 data。
struct LoginResult {
    std::string token;  // JWT，客户端放进 Authorization: Bearer <token>
    AuthUser user;
};

// 注册。username 3-32、字母数字下划线且唯一，password 6-64。
// 成功返回新用户 id；失败返回 -1，并通过 err 给出原因。
std::int64_t register_user(const std::string& username,
                           const std::string& password,
                           nloj::common::AppError* err = nullptr);

// 登录。校验账号密码后签发 JWT。
// 失败返回空 token，并通过 err 给出原因（错密为 WrongPassword）。
LoginResult login_user(const std::string& username,
                       const std::string& password,
                       nloj::common::AppError* err = nullptr);

// 校验 JWT。给 nl-api 拦截器用：从 Header 取出 token 后调用。
// 成功返回 token 内的用户信息；无效或过期返回 id==0。
AuthUser verify_token(const std::string& token);

// 按用户 id 查当前用户资料。id 来自 verify_token，不是 HTTP 直接传入。
AuthUser get_current_user(const std::string& user_id);

}  // namespace nloj::user
