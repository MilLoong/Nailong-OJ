#include "nloj/user/auth_crypto.h"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

namespace nloj::user::crypto {
namespace {

constexpr int kPbkdf2Iterations = 100000;  // 迭代次数
constexpr int kSaltLen = 16;               // 盐长度
constexpr int kHashLen = 32;               // SHA-256 输出长度
constexpr int kJwtExpireHours = 24;
const std::string kJwtSecret = "nloj-dev-secret-change-me";

std::string to_hex(const unsigned char* data, std::size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

int hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// hex 文本解回二进制，供登录时还原盐 / digest。
std::vector<unsigned char> from_hex(const std::string& hex) {
    if (hex.size() % 2 != 0) {
        return {};
    }
    std::vector<unsigned char> out(hex.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const int hi = hex_nibble(hex[i * 2]);
        const int lo = hex_nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return {};
        }
        out[i] = static_cast<unsigned char>((hi << 4) | lo);
    }
    return out;
}

// JWT 用的 Base64URL（+/ 换成 -_，去掉 =）
std::string base64url_encode(const unsigned char* data, std::size_t len) {
    std::string out(4 * ((len + 2) / 3) + 4, '\0');
    const int n = EVP_EncodeBlock(
        reinterpret_cast<unsigned char*>(out.data()), data, static_cast<int>(len));
    if (n < 0) {
        return {};
    }
    out.resize(static_cast<std::size_t>(n));
    for (char& c : out) {
        if (c == '+') {
            c = '-';
        } else if (c == '/') {
            c = '_';
        }
    }
    while (!out.empty() && out.back() == '=') {
        out.pop_back();
    }
    return out;
}

std::string base64url_encode(const std::string& text) {
    return base64url_encode(reinterpret_cast<const unsigned char*>(text.data()), text.size());
}

// Base64URL 解回二进制（验签、解 payload 用）
std::string base64url_decode(std::string text) {
    for (char& c : text) {
        if (c == '-') {
            c = '+';
        } else if (c == '_') {
            c = '/';
        }
    }
    while (text.size() % 4 != 0) {
        text.push_back('=');
    }
    std::string out(text.size(), '\0');
    const int n = EVP_DecodeBlock(
        reinterpret_cast<unsigned char*>(out.data()),
        reinterpret_cast<const unsigned char*>(text.data()),
        static_cast<int>(text.size()));
    if (n < 0) {
        return {};
    }
    int pad = 0;
    if (text.size() >= 1 && text[text.size() - 1] == '=') {
        pad++;
    }
    if (text.size() >= 2 && text[text.size() - 2] == '=') {
        pad++;
    }
    if (n < pad) {
        return {};
    }
    out.resize(static_cast<std::size_t>(n - pad));
    return out;
}

// 用 kJwtSecret 对 signing_input 做 HMAC-SHA256。成功返回 1。
int hmac_sha256(const std::string& signing_input, unsigned char* mac, unsigned int* mac_len) {
    if (HMAC(
            EVP_sha256(),
            kJwtSecret.data(),
            static_cast<int>(kJwtSecret.size()),
            reinterpret_cast<const unsigned char*>(signing_input.data()),
            signing_input.size(),
            mac,
            mac_len) == nullptr) {
        return 0;
    }
    return 1;
}

}  // namespace

std::string hash_password(const std::string& password) {
    // 格式：pbkdf2$迭代次数$盐(hex)$哈希(hex)，登录时按同样参数再算一遍比对。
    unsigned char salt[kSaltLen];
    if (RAND_bytes(salt, kSaltLen) != 1) {
        return {};
    }
    unsigned char digest[kHashLen];
    if (PKCS5_PBKDF2_HMAC(
            password.c_str(),
            static_cast<int>(password.size()),
            salt,
            kSaltLen,
            kPbkdf2Iterations,
            EVP_sha256(),
            kHashLen,
            digest) != 1) {
        return {};
    }
    return "pbkdf2$"
          + std::to_string(kPbkdf2Iterations)
          + "$"
          + to_hex(salt, kSaltLen)
          + "$"
          + to_hex(digest, kHashLen);
}

int verify_password(const std::string& password, const std::string& stored_hash) {
    // stored_hash: pbkdf2$迭代次数$盐(hex)$digest(hex)
    std::string parts[4];
    std::size_t start = 0;
    for (int i = 0; i < 4; ++i) {
        const std::size_t pos = (i == 3) ? std::string::npos : stored_hash.find('$', start);
        if (i < 3 && pos == std::string::npos) {
            return 0;
        }
        parts[i] = stored_hash.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
        start = pos + 1;
    }
    if (parts[0] != "pbkdf2") {
        return 0;
    }

    int iterations = 0;
    try {
        iterations = std::stoi(parts[1]);
    } catch (...) {
        return 0;
    }
    const std::vector<unsigned char> salt = from_hex(parts[2]);
    const std::vector<unsigned char> digest_store = from_hex(parts[3]);
    if (salt.empty() || digest_store.size() != static_cast<std::size_t>(kHashLen)) {
        return 0;
    }

    unsigned char digest_login[kHashLen];
    if (PKCS5_PBKDF2_HMAC(
            password.c_str(),
            static_cast<int>(password.size()),
            salt.data(),
            static_cast<int>(salt.size()),
            iterations,
            EVP_sha256(),
            kHashLen,
            digest_login) != 1) {
        return 0;
    }
    // 二进制 digest 逐字节比
    return std::vector<unsigned char>(digest_login, digest_login + kHashLen) == digest_store ? 1 : 0;
}

std::string sign_hs256_jwt(
    std::int64_t user_id, const std::string& username, const std::string& role) {
    // HS256：HMAC-SHA256(secret, header.payload)，再拼成三方 JWT。
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const auto exp = now + static_cast<std::time_t>(kJwtExpireHours) * 3600;

    const std::string header = R"({"alg":"HS256","typ":"JWT"})";
    const std::string payload = std::string("{") + R"("iss":"nloj","uid":")" + std::to_string(user_id) +
                                R"(","name":")" + username + R"(","role":")" + role + R"(","iat":)" +
                                std::to_string(now) + R"(,"exp":)" + std::to_string(exp) + "}";

    const std::string signing_input = base64url_encode(header) + "." + base64url_encode(payload);

    unsigned char mac[EVP_MAX_MD_SIZE];
    unsigned int mac_len = 0;
    if (!hmac_sha256(signing_input, mac, &mac_len)) {
        return {};
    }
    return signing_input + "." + base64url_encode(mac, mac_len);
}

int verify_hs256_signature(const std::string& token, std::string& payload_json) {
    // 用 kJwtSecret 重算前两段 HMAC，和第三段比对。
    const std::size_t dot1 = token.find('.');
    const std::size_t dot2 = (dot1 == std::string::npos) ? std::string::npos : token.find('.', dot1 + 1);
    if (dot1 == std::string::npos || dot2 == std::string::npos ||
        token.find('.', dot2 + 1) != std::string::npos) {
        return 0;  // 必须刚好三段
    }

    const std::string signing_input = token.substr(0, dot2);
    const std::string sig_b64 = token.substr(dot2 + 1);
    const std::string sig = base64url_decode(sig_b64);

    unsigned char mac[EVP_MAX_MD_SIZE];
    unsigned int mac_len = 0;
    if (!hmac_sha256(signing_input, mac, &mac_len)) {
        return 0;
    }
    if (sig.size() != mac_len || CRYPTO_memcmp(sig.data(), mac, mac_len) != 0) {
        return 0;  // 签名对不上，token 被改过或密钥不对
    }

    payload_json = base64url_decode(token.substr(dot1 + 1, dot2 - dot1 - 1));
    if (payload_json.empty()) {
        return 0;
    }
    return 1;
}

}  // namespace nloj::user::crypto
