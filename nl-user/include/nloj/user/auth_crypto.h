#pragma once

#include <cstdint>
#include <string>

namespace nloj::user::crypto {

/**
 * @brief 密码哈希。
 * @return 格式 pbkdf2$迭代次数$盐(hex)$哈希(hex)
 */
std::string hash_password(const std::string& password);

/**
 * @brief 用库存盐和迭代次数重算 digest。
 * @return 相同返回 1，否则 0
 */
int verify_password(const std::string& password, const std::string& stored_hash);

/**
 * @brief 签发 HS256 JWT，payload 含 uid/name/role/iat/exp。
 * @param secret 必须非空；空则签发失败
 * @return JWT；失败返回空串
 */
std::string sign_hs256_jwt(std::int64_t user_id,
                           const std::string& username,
                           const std::string& role,
                           const std::string& secret);

/**
 * @brief 验签。
 * @param[out] payload_json 验签通过时写出 payload JSON
 * @param secret 必须非空；空则直接拒绝
 * @return 通过返回 1，失败返回 0
 */
int verify_hs256_signature(const std::string& token,
                           std::string& payload_json,
                           const std::string& secret);

}  // namespace nloj::user::crypto
