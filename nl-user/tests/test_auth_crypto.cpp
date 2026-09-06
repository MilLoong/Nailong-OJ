// nl-user 密码 / JWT 纯逻辑单测（不连 MySQL）。
#include "nloj/user/auth_crypto.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_failed = 0;  // 任一条失败则置 1

// 测试密钥。签发/验签都显式传，不再依赖“空则用开发默认值”的旧逻辑。
const char* kTestSecret = "nloj-test-secret";

// 打印 [PASS]/[FAIL]；失败时记下 g_failed。
void expect_true(const char* name, int ok) {
    if (ok) {
        std::cout << "[PASS] " << name << '\n';
    } else {
        std::cout << "[FAIL] " << name << '\n';
        g_failed = 1;
    }
}

// 哈希非空 → 正确密码通过 → 错误密码拒绝 → 残缺串拒绝。
void test_password_roundtrip() {
    const std::string hash = nloj::user::crypto::hash_password("secret123");
    expect_true("hash_password non-empty", !hash.empty());
    expect_true("verify correct password", nloj::user::crypto::verify_password("secret123", hash));
    expect_true("reject wrong password", !nloj::user::crypto::verify_password("wrong-pass", hash));
    expect_true("reject truncated hash", !nloj::user::crypto::verify_password("secret123", "pbkdf2$1$aa"));
}

// 签发 → 验签拿 payload → 校验 claims → 篡改后应失败。
void test_jwt_roundtrip() {
    const std::string token = nloj::user::crypto::sign_hs256_jwt(42, "alice", "user", kTestSecret);
    expect_true("sign_hs256_jwt non-empty", !token.empty());

    // 验签成功后 payload 里应有 uid / name / role
    std::string payload;
    expect_true("verify_hs256_signature ok",
                nloj::user::crypto::verify_hs256_signature(token, payload, kTestSecret));
    expect_true("payload has uid", payload.find("\"uid\":\"42\"") != std::string::npos);
    expect_true("payload has name", payload.find("\"name\":\"alice\"") != std::string::npos);
    expect_true("payload has role", payload.find("\"role\":\"user\"") != std::string::npos);

    // 改 payload 段一个字符，HMAC 应对不上
    const auto dot1 = token.find('.');
    std::string tampered = token;
    if (dot1 != std::string::npos && dot1 + 1 < tampered.size()) {
        tampered[dot1 + 1] = tampered[dot1 + 1] == 'A' ? 'B' : 'A';
    }
    std::string ignored;
    expect_true("reject tampered token",
                !nloj::user::crypto::verify_hs256_signature(tampered, ignored, kTestSecret));

    const std::string quoted = nloj::user::crypto::sign_hs256_jwt(7, "a\"b", "user", kTestSecret);
    std::string quoted_payload;
    expect_true("sign username with quote", !quoted.empty());
    expect_true("verify quoted username",
                nloj::user::crypto::verify_hs256_signature(quoted, quoted_payload, kTestSecret));
    expect_true("quoted name escaped",
                quoted_payload.find("a\\\"b") != std::string::npos
             || quoted_payload.find("a\"b") != std::string::npos);
}

// secret 为空应拒绝：签发返回空串、验签失败（不再退回已知开发默认密钥）。
void test_empty_secret() {
    const std::string token = nloj::user::crypto::sign_hs256_jwt(1, "alice", "user", "");
    expect_true("sign with empty secret empty", token.empty());

    const std::string signed_ok =
        nloj::user::crypto::sign_hs256_jwt(1, "alice", "user", kTestSecret);
    std::string payload;
    expect_true("verify with empty secret rejected",
                !nloj::user::crypto::verify_hs256_signature(signed_ok, payload, ""));
}

}  // namespace

int main() {
    // 密码往返 → JWT 往返 → 空 secret 拒绝 → 汇总退出码
    test_password_roundtrip();
    test_jwt_roundtrip();
    test_empty_secret();
    if (g_failed) {
        std::cerr << "nl-user crypto tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "nl-user crypto tests passed\n";
    return EXIT_SUCCESS;
}
