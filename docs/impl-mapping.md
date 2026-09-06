# 接口 → C++ 领域函数对照

本文是 **全量映射表**：`api.md` 每条 HTTP 对应哪个 C++ 函数、入参、返回值。  
`code` / `message` 由将来的 `nl-api` 包装，**不出现在领域返回值里**。

失败哨兵（与 `nl-user` 现状一致）：整数用 `-1`；结构体用「空」（`id==0` 或关键字段为空）。

`.sql` 只是建表脚本；真正的表在 MySQL 里。列名以 `sql/schema.sql` 为准。

骨架注释约定见 skill：`nloj-domain-skeleton`（只写 `// a → b → c`，不写实现，留给你先写）。

---

## 总表

| HTTP | 模块 | 领域函数 | 成功返回（领域） |
|------|------|----------|------------------|
| `POST /api/v1/auth/register` | `nl-user` | `register_user` | `int64` 新用户 id |
| `POST /api/v1/auth/login` | `nl-user` | `login_user` | `LoginResult` |
| `GET /api/v1/users/me` | `nl-user` | `verify_token` + `get_current_user` | `AuthUser` |
| `GET /api/v1/problems` | `nl-problem` | `list_problems` | `ProblemPage` |
| `GET /api/v1/problems/{id}` | `nl-problem` | `get_problem` | `ProblemDetail` |
| `POST /api/v1/problems` | `nl-problem` | `create_problem` | `int64` 新题 id |
| `PUT /api/v1/problems/{id}` | `nl-problem` | `update_problem` | `int`：`1` 成功 / `0` 失败 |
| `POST /api/v1/submissions` | `nl-submit` | `create_submission` | `int64` submission id |
| `GET /api/v1/submissions/{id}` | `nl-submit` | `get_submission` | `SubmissionDetail` |
| `GET /api/v1/submissions` | `nl-submit` | `list_my_submissions` | `SubmissionPage` |
| （无 HTTP，MQ Worker） | `nl-judge` | `run_judge_task` / `JudgeNode::run` | `int`：`1` 成功 / `0` 失败 |
| （无 HTTP，超时自愈） | `nl-judge` | `reclaim_stale_judging` | 回收条数 |
| `GET /api/v1/health` | `nl-api` | （路由内联即可） | 不进领域模块 |

鉴权辅助（无独立业务 URL，供 `nl-api` 拦截器调用）：

| 用途 | 函数 | 返回 |
|------|------|------|
| 解析 Bearer JWT | `nloj::user::verify_token` | `AuthUser` |
| 按 id 查用户 | `nloj::user::get_current_user` | `AuthUser` |

---

## 1. nl-user（已实现，对照用）

### `register_user` ← `POST /api/v1/auth/register`

```cpp
std::int64_t register_user(const std::string& username, const std::string& password);
```

| | |
|--|--|
| 入参 | 请求体 `username`、`password` |
| 返回 | 成功 `id>0`；失败 `-1` |
| API `data` | 该 id |
| 流程注释 | `校验长度 → 查重 username → PBKDF2 哈希 → INSERT → 返回 insert_id` |

### `login_user` ← `POST /api/v1/auth/login`

```cpp
LoginResult login_user(const std::string& username, const std::string& password);
```

| | |
|--|--|
| 入参 | 请求体 `username`、`password` |
| 返回 | 成功：填好的 `LoginResult{token, user}`；失败：空 `token` |
| API `data` | `token`；`user.id`→`userId`；`user.username`；`user.role` |
| 流程注释 | `按 username 查库 → 比对密码哈希 → 签发 HS256 JWT` |

### `verify_token` + `get_current_user` ← `GET /api/v1/users/me`

```cpp
AuthUser verify_token(const std::string& token);
AuthUser get_current_user(const std::string& user_id);
```

| | |
|--|--|
| 入参 | Header Bearer → `verify_token`；其 `id` 再进 `get_current_user` |
| 返回 | `AuthUser`；失败 `id==0` |
| API `data` | `id/username/role/createTime` |
| 流程注释 | 验签：`用 jwt_secret 验签、查过期 → 解析 uid/role → 填 AuthUser`；查库：`按 id 查未删除用户 → 填 AuthUser` |

---

## 2. nl-problem（骨架已声明，实现留给你）

类型与函数见 `nl-problem/include/nloj/problem/module.h`。

### `list_problems` ← `GET /api/v1/problems`

| | |
|--|--|
| 入参 | `page_num`、`page_size`、`difficulty`（可空）、`keyword`（可空）← Query |
| 返回 | `ProblemPage{page_num,page_size,total,records}`；失败可 `total=0` 且空列表 |
| API `data` | 整页对象；`records[]` 无 `description` |
| 表 | 读 `problem`，一般 `visible=1 AND deleted=0` |
| 流程注释 | `拼过滤条件 → COUNT total → SELECT 分页列表（不含题面）→ 填 ProblemPage` |

### `get_problem` ← `GET /api/v1/problems/{id}`

| | |
|--|--|
| 入参 | `id` ← 路径；可选头 `X-NLOJ-Skip-Cache: 1` 强制回源（压测对照） |
| 返回 | `ProblemDetail`（含 `samples`）；失败空结构（`id==0`） |
| API `data` | 详情 + `samples`（仅 `is_sample=1`） |
| 表 | `problem` + `problem_case` |
| 缓存计数 | `problem_cache_stats()`，挂在 `GET /health` 的 `data.cache` |
| 流程注释 | `L1 → Redis String → 互斥锁重建 → MySQL → 回填（空值短 TTL）` |

### `create_problem` ← `POST /api/v1/problems`（需 admin，鉴权在 api 层）

| | |
|--|--|
| 入参 | `CreateProblemRequest`：title/difficulty/description/time_limit/memory_limit/visible；可选 problem_type / judge_mode / extra_code |
| 返回 | 成功新题 `id>0`；失败 `-1` |
| API `data` | 该 id |
| 表 | INSERT `problem`（用例可后续再扩；Phase B 可先只建题） |
| 流程注释 | `校验字段 → INSERT problem → 返回 insert_id` |

### `update_problem` ← `PUT /api/v1/problems/{id}`（需 admin）

| | |
|--|--|
| 入参 | `id` + 同创建请求体字段 |
| 返回 | 成功 `1`；失败 `0` |
| API `data` | `null` |
| 表 | UPDATE `problem` WHERE id AND deleted=0 |
| 流程注释 | `校验字段 → 按 id UPDATE → DEL nloj:problem:{id}` |

---

## 3. nl-submit（骨架已声明，实现留给你）

见 `nl-submit/include/nloj/submit/module.h`。

### `create_submission` ← `POST /api/v1/submissions`（需登录）

| | |
|--|--|
| 入参 | `user_id`（来自 token，**不是**请求体）、`problem_id`、`language`、`code` |
| 返回 | 成功 submission `id>0`；失败 `-1` |
| API `data` | `{ "submissionId": <id> }` |
| 表 | INSERT `submission`，`status=PENDING`；再投递判题消息（可先空实现） |
| 流程注释 | `校验题目存在且可见 → INSERT PENDING → 发判题消息 → 返回 submission_id` |

### `get_submission` ← `GET /api/v1/submissions/{id}`

| | |
|--|--|
| 入参 | `id`；`viewer_user_id` + `viewer_role`（本人或 admin） |
| 返回 | `SubmissionDetail`；失败 `id==0` |
| API `data` | 详情字段（含可选 `code`） |
| 流程注释 | `按 id 查 submission → 校验本人或 admin → 填 SubmissionDetail` |

### `list_my_submissions` ← `GET /api/v1/submissions`

| | |
|--|--|
| 入参 | `user_id`（当前用户）、分页、`problem_id`（可选 0 表示不限）、`status`（可空） |
| 返回 | `SubmissionPage`（列表可不含完整 `code`） |
| 流程注释 | `按 user_id 与可选过滤 COUNT → SELECT 分页 → 填 SubmissionPage` |

---

## 4. nl-judge（无直接 HTTP）

### `run_judge_task` ← 消费 MQ / 本地队列

| | |
|--|--|
| 入参 | `submission_id`（消息里带的） |
| 返回 | `1` 成功写回结果；`0` 失败 |
| 副作用 | 更新 `submission` 的 status/time_used/memory_used/judge_info |
| 流程注释 | `读 submission 与题目用例 → 置 JUDGING → 沙箱单容器跑全部用例 → 比对输出 → UPDATE 终态` |

---

## 5. 和 HTTP 外壳的关系（提醒）

```
请求 JSON ──解析──► 领域函数入参
领域函数返回值 ──映射──► 响应 JSON 的 data
错误哨兵 ──映射──► code/message（如 40100、50001）
```

实现顺序建议：先按 `module.h` 签名 + `.cpp` 里流程注释自己写逻辑；`nl-api` 路由后接。
