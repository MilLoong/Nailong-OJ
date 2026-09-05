---
name: nloj-cpp-style
description: >-
  NLOJ / 通用 C++ 中文注释与代码风格（规则自包含，不依赖仓库内某几个文件）。
  编写或修改 .h/.cpp 时使用。覆盖头文件/实现注释分工、分段流程注释（→）、
  布尔用 0/1、SQL 字符串分行拼接（字符串一定要在 = 后面）、
  ||/&& 放下一行行首且操作数对齐、长函数调用右括号单独一行、
  指针用空格箭头 ` -> `、改代码时保留已有注释。
---

# C++ 风格（中文注释）

本 skill **自包含**：规则与示例都写在这里。  
（个人机亦可使用同内容 skill：`cpp-style-zh`。）

改逻辑时 **只改有问题的行**，禁止整文件覆盖把注释清掉。

公共能力（DB 封装、工具函数）放在公共模块命名空间里复用，业务模块不要各抄一份。本仓库对应 `nloj::common`。

## 注释写在哪

- **`.h`**：给调用者看。函数干什么、参数约束、返回值、对应哪条 API（若有）。短，几行。不要写 SQL、哈希算法等实现细节。
- **`.cpp`**：怎么做。算法、SQL、协议细节。内部辅助函数也在定义上一行用 `//` 说明。
- **结构体**：类型上一行说明用途；字段用行尾 `//`，尽量对齐。
- **语言**：**中文**，`//` 即可，不要上 Doxygen。

### 头文件示例

```cpp
// 登录用户视图，对应 GET /api/v1/users/me 的 data。
struct AuthUser {
    std::int64_t id;          // 用户主键
    std::string username;     // 用户名
};

// 注册。username 3-32 且唯一，password 6-64。
// 成功返回新用户 id；失败返回 -1（或项目约定的哨兵）。
std::int64_t register_user(const std::string& username, const std::string& password);
```

### 实现里的分段注释

对外函数开头一行写完整流程（用 `→` 串起来）。每个步骤前再写一行小标题：

```cpp
std::int64_t register_user(...) {
    // 校验长度 → 查重 username → 密码哈希 → INSERT → 返回 insert_id

    // 校验长度
    ...
    // 数据库连接
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

返回 `bool` 的辅助函数同样用 `return 0` / `return 1`。

## SQL 字符串拼接

**硬性规则：字符串一定要在 `=` 后面。**  
`const std::string xxx =` 同一行必须立刻出现 `"..."`，禁止 `=` 独占一行再换行写字面量。

```cpp
// 对
const std::string list_sql = "SELECT id, user_id, problem_id, language, status, "
                             "time_used, memory_used, judge_info, create_time FROM submission"
                            + where_sql
                            + " ORDER BY id DESC LIMIT "
                            + std::to_string(page_size)
                            + " OFFSET "
                            + std::to_string(offset);

// 错：= 后面没有字符串
const std::string list_sql =
    "SELECT id FROM submission"
    + where_sql;
```

首段是完整 SQL 前缀字符串，后面每一段变量或后缀单独一行，行首 `+`，对齐在同一列。

```cpp
const std::string select_sql = "SELECT id FROM `user` WHERE username='"
                              + escaped_username
                              + "' AND deleted=0 LIMIT 1";

const std::string insert_sql = "INSERT INTO `user` (username, password_hash) VALUES ('"
                              + escaped_username + "', '" + escaped_hash + "')";
```

数字主键用 `std::to_string(id)` 拼进 SQL（已是整数，不必再加引号，也不必 `escape`）：

```cpp
const std::string select_sql = "SELECT id, username, role, create_time FROM `user` WHERE id="
                              + std::to_string(id)
                              + " AND deleted=0 LIMIT 1";
```

- 字符串条件必须先做 SQL 转义，再放进单引号里。
- 保留字表名（如 `user`）要反引号。
- 不要写成一整行超长 `+`，也不要把 `AND` 和下一字段名粘在一起。
- 分页的 `LIMIT` / `OFFSET` 用整数转字符串拼接，不要对页码做字符串 escape。

## 长函数调用换行

`=` 后面必须立刻出现函数名和 `(`。参数换行写；**右括号 `)` 单独一行**，与 `xxx =` 那一行对齐。禁止 `);` 贴在最后一个参数后面。

```cpp
// 对
HANDLE out_handle = CreateFileA(
    stdout_path.string().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
);

const BOOL ok = CreateProcessA(
    nullptr, cmdline.data(), nullptr, nullptr, TRUE,
    CREATE_NO_WINDOW, nullptr, cwd_arg, &si, &pi
);

// 错：) 贴在最后一行参数上
CreateFileA(
    path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

// 错：= 后面没有函数名
const BOOL ok =
    CreateProcessA(...);
```

一行能放下的短调用不必拆。

## 条件 / 运算符换行

`||` / `&&`（以及 SQL 的 `+`）放在**下一行**，不要贴在上一行末尾。  
对齐的是操作数（两个 `!read_...`），**`||` 不必和 `if` 对齐**。  
`if (` 比 `|| ` 多一个 `(`，续行要比 `if` 那一行多一个空格，`!read` 才落在同一列。

```cpp
// 对：两个 !read_string_field 对齐
if (!read_string_field(body, "username", username)
 || !read_string_field(body, "password", password)) {
}

if (!read_i64_field(body, "problemId", problem_id)
 || !read_string_field(body, "language", language)
 || !read_string_field(body, "code", code)) {
}

// 错：|| 在上一行末尾
if (!read_string_field(body, "username", username) ||
    !read_string_field(body, "password", password)) {
}

// 错：|| 和 if 对齐，!read 错开一列（漏算了 '('）
if (!read_string_field(body, "username", username)
|| !read_string_field(body, "password", password)) {
}
```

一行能放下的短条件不必拆。

## 其它

- **命名空间**：`项目前缀::模块`；本仓库为 `nloj::user` 等。标识符不能用 `-`。
- **常量**：`k` 前缀，行尾可写简短中文，如 `constexpr int kSaltLen = 16; // 盐长度`。
- **内部工具**：放在 `.cpp` 的 `namespace { }`，调用方只看到头文件里的接口。
- **空指针**：用 `nullptr`，不要用 `NULL` 或整数 `0` 表示指针。
- **指针成员**：写成 `ptr -> mem`，箭头两边有空格；不要写 `ptr->mem`。
- **编码**：源文件按 UTF-8（MSVC 建议 `/utf-8`），中文注释保持 UTF-8。

## 与 OOP 的边界（可选）

- 无状态、与 HTTP/用例一一对应的业务：可用命名空间 + `struct`（DTO）+ 自由函数。
- 有生命周期、持有连接/线程的实体：用类；跨模块可替换点用接口（抽象类）；纯逻辑仍用自由函数。
