#include "nloj/user/auth_crypto.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_failed = 0;

void expect_true(const char* name, int ok) {
    if (ok) {
        std::cout << "[PASS] " << name << '\n';
    } else {
        std::cout << "[FAIL] " << name << '\n';
        g_failed = 1;
    }
}

void test_password_roundtrip() {
    const std::string hash = nloj::user::crypto::hash_password("secret123");
    expect_true("hash_password non-empty", !hash.empty());
    expect_true("verify correct password", nloj::user::crypto::verify_password("secret123", hash));
    expect_true("reject wrong password", !nloj::user::crypto::verify_password("wrong-pass", hash));
    expect_true("reject truncated hash", !nloj::user::crypto::verify_password("secret123", "pbkdf2$1$aa"));
}

void test_jwt_roundtrip() {
    const std::string token = nloj::user::crypto::sign_hs256_jwt(42, "alice", "user");
    expect_true("sign_hs256_jwt non-empty", !token.empty());

    std::string payload;
    expect_true("verify_hs256_signature ok", nloj::user::crypto::verify_hs256_signature(token, payload));
    expect_true("payload has uid", payload.find("\"uid\":\"42\"") != std::string::npos);
    expect_true("payload has name", payload.find("\"name\":\"alice\"") != std::string::npos);
    expect_true("payload has role", payload.find("\"role\":\"user\"") != std::string::npos);

    // 篡改 payload 中间一段
    const auto dot1 = token.find('.');
    std::string tampered = token;
    if (dot1 != std::string::npos && dot1 + 1 < tampered.size()) {
        tampered[dot1 + 1] = tampered[dot1 + 1] == 'A' ? 'B' : 'A';
    }
    std::string ignored;
    expect_true("reject tampered token", !nloj::user::crypto::verify_hs256_signature(tampered, ignored));
}

}  // namespace

int main() {
    test_password_roundtrip();
    test_jwt_roundtrip();
    if (g_failed) {
        std::cerr << "nl-user crypto tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "nl-user crypto tests passed\n";
    return EXIT_SUCCESS;
}
