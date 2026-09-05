---
name: nloj-cpp-test
description: >-
  NLOJ C++ 单元测试约定。编写或修改 nl-*/tests、CMake 里 add_test、
  跑 nloj_*_test 时使用。覆盖目录布局、轻量 assert 风格、纯逻辑与
  MySQL 联调分离、以及 Windows 下如何编译运行。
---

# NLOJ C++ 测试

权威样例（有冲突时以这份为准）：

- `nl-user/tests/test_auth_crypto.cpp`
- `nl-user/CMakeLists.txt` 末尾 `enable_testing` / `add_test`

## 放哪里

按模块放，**不要**在仓库根建统一 `test/`：

```
nl-user/tests/test_auth_crypto.cpp
nl-problem/tests/test_xxx.cpp   # 以后同理
```

- 文件名：`test_<主题>.cpp`
- 目标名：`nloj_<模块>_<主题>_test`（如 `nloj_user_crypto_test`）
- CTest 名：短横线式，如 `user_crypto`

## 测什么 / 不测什么

| 类型 | 依赖 | 做法 |
|------|------|------|
| 纯逻辑（哈希、JWT、校验） | 无 MySQL / 无 HTTP | 单独小库（如 `nl-user-crypto`）+ 单测可执行文件 |
| 领域 + DB（注册登录） | 本机 mysqld + schema | 另开集成测试；服务未起时跳过或明确失败，不要拖垮纯逻辑 CI |
| HTTP / 全链路 | 本机 mysqld + 沙箱（Docker 或 g++） | `nl-api/tests/test_http_e2e.cpp`；服务未起/MySQL 未就绪时明确失败，不要拖垮 `nloj_api_http_test` |

能抽离的依赖（密码、JWT）优先抽成可测小库，单测只链它，不链整个 `nl-user`。

## 写法（轻量自研，不上 gtest）

对照 `test_auth_crypto.cpp`：

- `namespace { }` 里放 `g_failed`、`expect_true`、各 `test_*`
- `expect_true(name, ok)`：打印 `[PASS]` / `[FAIL]`，失败置 `g_failed = 1`
- 布尔用 `0` / `1`（与 `nloj-cpp-style` 一致）；`ok` 用表达式即可
- `main`：依次调 `test_*`，有失败则 `EXIT_FAILURE` + stderr 一行总结
- 中文 `//` 只写非显而易见处（如「篡改 payload」）
- 断言名用英文短句，方便日志检索

```cpp
void expect_true(const char* name, int ok) {
    if (ok) {
        std::cout << "[PASS] " << name << '\n';
    } else {
        std::cout << "[FAIL] " << name << '\n';
        g_failed = 1;
    }
}
```

正向 + 负向都要盖：正确密码 / 错误密码、合法 token / 篡改 token。

## CMake

在对应模块 `CMakeLists.txt` 末尾：

```cmake
enable_testing()
add_executable(nloj_user_crypto_test tests/test_auth_crypto.cpp)
target_link_libraries(nloj_user_crypto_test PRIVATE nl-user-crypto)
add_test(NAME user_crypto COMMAND nloj_user_crypto_test)
```

- 单测目标只链必要库；不要为了方便把 MySQL 硬链进纯 crypto 测试
- 根 `CMakeLists.txt` 不必为每个测试再写一遍；模块内 `add_test` 即可

## 怎么跑（Windows）

优先直接跑 exe（本机未必有 PATH 里的 `ctest`）：

```powershell
$env:OPENSSL_ROOT_DIR = "C:\anaconda\anaconda3\Library"
cmake --preset vs2022   # 或 VS 自带 cmake 全路径
cmake --build --preset debug --target nloj_user_crypto_test
.\build\bin\Debug\nloj_user_crypto_test.exe
```

改完相关逻辑后应跑对应测试；全绿再改 README Phase 勾选或提 commit。
