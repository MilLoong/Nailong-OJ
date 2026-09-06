// nl-user 注册 / 登录 / 鉴权集成测试（需要本机 MySQL：nloj / nloj123456 / nloj_db）。
#include "nloj/user/module.h"

#include <ctime>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_failed = 0;  // 任一条失败则置 1

// 打印 [PASS]/[FAIL]；失败时记下 g_failed。
void expect_true(const char* name, int ok) {
    if (ok) {
        std::cout << "[PASS] " << name << '\n';
    } else {
        std::cout << "[FAIL] " << name << '\n';
        g_failed = 1;
    }
}

// 唯一用户名，避免重复跑测试撞 uk_username。
std::string unique_username() {
    return "ut_" + std::to_string(std::time(nullptr));
}

// 注册成功 -> 重复注册失败 -> 登录拿 token -> verify_token -> get_current_user -> 错密登录失败。
void test_register_login_flow() {
    const std::string username = unique_username();
    const std::string password = "secret123";

    // 注册
    const std::int64_t user_id = nloj::user::register_user(username, password);
    expect_true("register_user returns id", user_id > 0);

    nloj::common::AppError taken = nloj::common::AppError::Ok;
    const std::int64_t dup_id = nloj::user::register_user(username, password, &taken);
    expect_true("register duplicate rejected", dup_id < 0);
    expect_true("duplicate is UsernameTaken", taken == nloj::common::AppError::UsernameTaken);

    nloj::common::AppError bad_name = nloj::common::AppError::Ok;
    const std::int64_t quote_id = nloj::user::register_user("bad\"name", password, &bad_name);
    expect_true("quote username rejected", quote_id < 0);
    expect_true("quote is InvalidArgument", bad_name == nloj::common::AppError::InvalidArgument);

    // 登录
    const nloj::user::LoginResult login = nloj::user::login_user(username, password);
    expect_true("login token non-empty", !login.token.empty());
    expect_true("login user id match", login.user.id == user_id);
    expect_true("login username match", login.user.username == username);
    expect_true("login role is user", login.user.role == "user");

    // JWT 校验
    const nloj::user::AuthUser from_token = nloj::user::verify_token(login.token);
    expect_true("verify_token id match", from_token.id == user_id);
    expect_true("verify_token username match", from_token.username == username);

    // 按 id 查库
    const nloj::user::AuthUser from_db =
        nloj::user::get_current_user(std::to_string(user_id));
    expect_true("get_current_user id match", from_db.id == user_id);
    expect_true("get_current_user username match", from_db.username == username);

    // 错密
    nloj::common::AppError pw_err = nloj::common::AppError::Ok;
    const nloj::user::LoginResult bad = nloj::user::login_user(username, "wrong-pass", &pw_err);
    expect_true("wrong password rejected", bad.token.empty());
    expect_true("wrong password is WrongPassword", pw_err == nloj::common::AppError::WrongPassword);
}

}  // namespace

int main() {
    // 注册登录全流程 -> 汇总退出码
    test_register_login_flow();
    if (g_failed) {
        std::cerr << "nl-user auth db tests failed (检查 MySQL 是否启动且 nloj/nloj_db 已就绪)\n";
        return EXIT_FAILURE;
    }
    std::cout << "nl-user auth db tests passed\n";
    return EXIT_SUCCESS;
}
