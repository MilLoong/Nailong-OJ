---
name: nloj-domain-skeleton
description: >-
  NLOJ 领域函数只搭骨架：按 api.md / impl-mapping 声明 module.h，
  在 .cpp 只写「步骤 → 步骤」流程注释和失败哨兵返回，不写真实实现。
  用户说先自己写、只要注释/骨架、对照接口加函数、脚手架时使用。
---

# NLOJ 领域骨架（先注释，后实现）

用户要自己写业务逻辑时：**禁止**代写 SQL / 哈希 / 完整函数体。  
只交付：头文件声明 + `.cpp` 里一行流程注释 + 空返回（哨兵）。

对照：

- 映射表：`docs/impl-mapping.md`
- 注释风格样例：`nl-user/src/module.cpp` 里 `register_user` 等函数开头的 `// … → …`
- 码风：`nloj-cpp-style`

## 做什么

1. 打开 `docs/api.md` 与 `docs/impl-mapping.md`，确认 HTTP ↔ 函数名 ↔ 入参/返回值。
2. 在对应 `nl-*/include/nloj/*/module.h`：**结构体 + 函数声明 + 中文注释**（对应哪条 HTTP、参数约束、成功/失败返回什么）。
3. 在 `nl-*/src/module.cpp`：函数体里 **只写**：
   - 第一行：`// 步骤A → 步骤B → 步骤C`（用 `→`，与 user 一致）
   - 然后 `return` 失败哨兵（`-1` / `0` / 空结构体），可加行尾 `// TODO 自行实现`
4. **不要**写真正的校验、SQL、`mysql_*`、OpenSSL、MQ、Docker。
5. 不要删用户已写的实现；若函数已有真逻辑，只许改注释或签名，除非用户明确要求重写。

## 流程注释长什么样

```cpp
std::int64_t create_problem(const CreateProblemRequest& req) {
    // 校验字段 → INSERT problem → 返回 insert_id
    return -1;  // TODO 自行实现
}
```

`nl-user` 已实现的函数 **保持实现**，不要倒退成骨架。

## 头文件注释

短，给调用者看；点明 HTTP；写清返回哨兵：

```cpp
// 创建题目，对应 POST /api/v1/problems（调用方需已鉴权 admin）。
// 成功返回新题目 id；失败返回 -1。
std::int64_t create_problem(const CreateProblemRequest& req);
```

## 何时可以写实现

仅当用户明确说「实现 xxx」「把骨架补全」「帮我写 SQL」等。  
默认：骨架 + 映射文档，实现归用户。
