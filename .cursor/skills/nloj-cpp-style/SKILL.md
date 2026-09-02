---
name: nloj-cpp-style
description: >-
  NLOJ C++ 注释与代码风格。编写或修改 nl-* 模块的 .h/.cpp、对照
  nl-user 的 module.h 与 module.cpp 时使用。覆盖头文件/实现注释分工、
  分段中文注释、布尔用 0/1、SQL 字符串分行拼接、改代码时保留已有注释。
---

# NLOJ C++ 风格

权威样例（有冲突时以这两份为准）：

- `nl-user/include/nloj/user/module.h`
- `nl-user/src/module.cpp`

改逻辑时 **只改有问题的行**，禁止整文件覆盖把注释清掉。

## 注释写在哪

- **`.h`**：给调用者看。函数干什么、参数约束、返回值、和哪条 HTTP 对应。短，几行。不要写 SQL、PBKDF2 细节。
- **`.cpp`**：怎么做。算法、SQL、盐/哈希格式、JWT 怎么签。内部辅助函数也在定义上一行用 `//` 说明。
- 结构体：类型上一行说明用途；字段用行尾 `//`，尽量对齐。
- 语言：**中文**，`//` 即可，不要上 Doxygen。

### 头文件示例

```cpp
// 登录用户视图，对应 GET /api/v1/users/me 的 data。
struct AuthUser {
    std::int64_t id;          // 用户主键
    std::string username;     // 用户名
};

// 注册。username 3-32 且唯一，password 6-64。
// 成功返回新用户 id；用户名已存在等业务错误由实现抛出。
std::int64_t register_user(const std::string& username, const std::string& password);
```

### 实现里的分段注释

对外函数开头一行写完整流程（用 `→` 串起来）。每个步骤前再写一行小标题：

```cpp
std::int64_t register_user(...) {
    // 校验长度 → 查重 username → PBKDF2 哈希 → INSERT → 返回 insert_id

    // 校验长度
    ...
    // mysql 初始化、连接
    ...
    // username 查重（转义后拼 SQL，只认未删除用户）
    ...
}
```

失败分支可在 `return` 行尾补一句，如 `return -1;  // 用户名已存在`。

尚未实现的函数保留流程注释，不要删。

## 布尔

`false` 写成 `0`，`true` 写成 `1`。不要写 `true` / `false`。

```cpp
if (!start_mysql(mysql)) {
    return -1;
}
return digest_login == digest_store ? 1 : 0;
```

`query_exec` / `start_mysql` 这类 `bool` 返回值同样用 `return 0` / `return 1`。

## SQL 字符串拼接

跟 `register_user` / `login_user` 一样：首段是完整 SQL 前缀字符串，后面每一段变量或后缀单独一行，行首 `+`，对齐在同一列。

```cpp
const std::string select_sql = "SELECT id FROM `user` WHERE username='"
                              + escaped_username
                              + "' AND deleted=0 LIMIT 1";

const std::string insert_sql = "INSERT INTO `user` (username, password_hash) VALUES ('"
                              + escaped_username + "', '" + escaped_hash + "')";
```

数字主键用 `std::to_string(id)` 拼进 SQL（已是整数，不必再加引号）：

```cpp
const std::string select_sql = "SELECT id, username, role, create_time FROM `user` WHERE id="
                              + std::to_string(id)
                              + " AND deleted=0 LIMIT 1";
```

字符串条件必须先 `escape_sql`，再放进单引号里。表名 `user` 要反引号：`` `user` ``。

不要写成一整行超长 `+`，也不要把 `AND` 和下一字段名粘在一起。

## 其它

- 命名空间：`nloj::<模块>`，如 `nloj::user`。标识符不能用 `-`。
- 常量：`k` 前缀，行尾可写简短中文，如 `constexpr int kSaltLen = 16; // 盐长度`。
- 内部工具放在 `namespace { }`，调用方只看到 `.h` 里的接口。
- 空指针用 `nullptr`，不要用 `NULL` 或整数 `0` 表示指针。
- 中文源文件已开 `/utf-8`，注释保持 UTF-8。
