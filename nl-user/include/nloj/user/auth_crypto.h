#pragma once

#include <cstdint>
#include <string>

namespace nloj::user::crypto {

// 格式：pbkdf2$迭代次数$盐(hex)$哈希(hex)
std::string hash_password(const std::string& password);

// 用库存盐和迭代次数重算 digest。相同返回 1，否则 0。
int verify_password(const std::string& password, const std::string& stored_hash);

// HS256 JWT，payload 含 uid/name/role/iat/exp。
std::string sign_hs256_jwt(std::int64_t user_id, const std::string& username, const std::string& role);

// 验签通过返回 1，并写出 payload JSON；失败返回 0。
int verify_hs256_signature(const std::string& token, std::string& payload_json);

}  // namespace nloj::user::crypto
