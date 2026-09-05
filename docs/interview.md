# NLOJ — 校招面试指南

面向腾讯后台开发校招，本文档帮你用 STAR 法则讲清项目，并覆盖常见追问。

---

## 1. 30 秒电梯演讲

> 我自研了 **NLOJ** 在线判题后端，类似 LeetCode 的提交评测流程。整体是 **C++20 模块化单体**（`nl-common` / `nl-user` / `nl-problem` / `nl-submit` / `nl-judge` / `nl-api`），核心链路是：用户提交代码 → 写入 MySQL 并发送 **RabbitMQ** 消息 → 判题 Worker 在 **Docker 沙箱**里编译运行 → 比对测试用例后更新 AC/WA 等状态。读题场景用了 **Redis 缓存** 减轻数据库压力。

---

## 2. STAR 项目叙述模板

### Situation（背景）

校招需要能体现后端工程能力的项目。OJ 天然涉及：**异步任务、安全隔离、数据库设计、缓存、进程资源限制**，比 CRUD 后台更有深度，也和刷算法题的场景贴合。

### Task（目标）

实现一个可本地运行的 OJ 后端 MVP：

- 用户注册登录
- 题目浏览与管理
- 代码提交与异步判题
- 查询判题结果

### Action（行动）

| 模块 | 我做了什么 |
|------|-----------|
| 架构 | CMake 多模块（NLOJ 项目下 nl-* 子模块），HTTP 层与领域层分离 |
| 提交 | 提交接口快速返回，状态 PENDING，通过 RabbitMQ 解耦判题 |
| 判题 | 抽象 `JudgeSandbox` 接口，Docker 容器 `--network=none` + 内存/CPU 限制 |
| 比对 | 策略模式封装 ExactMatch，预留 SPJ 扩展 |
| 缓存 | 题目详情 Redis 缓存，更新时主动失效 |
| 鉴权 | JWT + 中间件，admin 接口角色校验 |
| 数据 | 设计 submission 状态机与复合索引 |
| 资源 | RAII 管理连接池，MQ 不可用时进程内队列降级 |

### Result（结果）

- 本地 Docker Compose 一键启动 MySQL/Redis/RabbitMQ
- 本机压测 `GET /problems/{id}`：缓存命中约 **1379 QPS**、P99 **35ms**、命中率 100%；强制回源约 244 QPS、P99 69ms（约 5.6×）
- 提交接口目标 P99 < 50ms（不含判题）
- 支持 C++ 判题（可扩展 Python/Go/Java）
- 完整 OpenAPI 契约

（压测细节见 [bench-report.md](bench-report.md)）

---

## 3. 高频面试题 & 参考回答

### Q1：为什么判题要异步，不能同步返回结果？

**答**：判题包含编译、多组用例运行，耗时从几百毫秒到数秒不等。同步 HTTP 会占用连接、易触发超时，且无法削峰。异步方案：提交后立即返回 submissionId，客户端轮询查结果；MQ 缓冲突发流量，判题 Worker 可按需扩容。

### Q2：如何保证用户代码安全？恶意代码怎么办？

**答**：不可信代码必须在隔离环境执行，不能在判题进程里直接 `fork` 后裸跑。我用 Docker 沙箱：

- `--network=none` 禁网
- `--memory` / `--cpus` 限资源
- `--pids-limit` 防 fork bomb
- 超时强杀进程
- 运行阶段只读文件系统

生产级 OJ 还会用 nsjail / cgroup v2 / gVisor，本质都是内核隔离 + 资源配额。

### Q3：RabbitMQ 消息丢了怎么办？

**答**：

- 生产者开启 **Publisher Confirm**，确保消息到达 Broker
- 消费者 **手动 ACK**，判题成功再确认；失败 nack 重试
- 重试次数超限进 **死信队列**，人工排查
- 消费端按 submissionId **幂等**：已是 JUDGING/终态则跳过

本地开发 MQ 连不上时，降级为进程内队列，保证链路可演示。

### Q4：Redis 缓存题目，怎么防击穿/穿透/雪崩？

**答**：

| 问题 | 方案 |
|------|------|
| 穿透 | 不存在 id 缓存空值，短 TTL |
| 击穿 | 热点 key 互斥锁重建，或逻辑过期 |
| 雪崩 | TTL 加随机抖动；多级缓存（进程内 LRU + Redis） |

管理员更新题目时主动 `DEL nloj:problem:{id}`。

### Q5：JWT 和 Redis Session 怎么选？

**答**：

- **JWT**：无状态，易水平扩展；缺点是无法主动失效（除非黑名单）
- **Redis Session**：Token → userId 存 Redis，可登出、可续期；多一次 Redis 查询

校招项目选一种讲透即可。本项目选 JWT + 过期时间。

### Q6：submission 表数据量大怎么优化？

**答**：

- 索引：`idx_user_create_time` 支持「我的提交」分页
- 列表接口不返回 code 大字段
- 历史数据按时间归档或分表
- 读写分离（进阶）

### Q7：为什么用模块化单体而不是直接微服务？

**答**：校招项目优先 **跑通核心链路**。单体开发调试快，CMake 子模块把边界划清，后续可把 judge 拆成独立进程。过早拆服务会增加部署、链路追踪、分布式事务复杂度，性价比低。

### Q8：设计模式在哪里用了？

**答**：

- **策略模式**：`JudgeStrategy` 不同语言/比对方式
- **模板方法**（可选）：沙箱执行流程 compile → run → collect
- **工厂 / 适配器**：`MessageBus` 在 RabbitMQ 与进程内队列之间切换

### Q9：MySQL 索引怎么设计的？

**答**：见 `sql/schema.sql`。举例：

- `submission(user_id, create_time)`：用户提交列表
- `submission(problem_id, status)`：某题通过数统计
- `user(username)` 唯一索引：登录查询

### Q10：如果判题服务挂了会怎样？

**答**：消息积压在 RabbitMQ，恢复后继续消费。submission 保持 PENDING/JUDGING，可增加超时任务将长时间 JUDGING 标为 SYSTEM_ERROR 并重试。

### Q11（C++ 追问）：连接池怎么避免泄漏？线程安全怎么保证？

**答**：连接用 RAII 包装，`ConnectionGuard` 析构时归还池中。池内部用 `mutex` + `condition_variable` 保护空闲队列。HTTP 线程只负责写库和投递消息，判题在独立消费者线程，共享状态（池、缓存）都有锁或按连接租借。

---

## 4. 腾讯后台校招 — 本项目覆盖的知识点

| 考察维度 | 本项目体现 |
|----------|-----------|
| C++ | C++20、RAII、智能指针、线程、条件变量 |
| 网络 | HTTP REST、JWT、Docker 网络隔离 |
| 数据库 | 表设计、索引、状态机、连接池 |
| Redis | 缓存、防击穿穿透 |
| 消息队列 | RabbitMQ 异步、削峰、可靠性、降级 |
| 操作系统 | 进程隔离、cgroup/资源限制、超时 |
| 设计模式 | 策略、适配器、模块化分层 |
| 系统设计 | 异步判题、沙箱、可扩展架构 |

---

## 5. 可主动延伸的加分项

面试末尾若时间充裕，可提：

1. **限流**：令牌桶保护提交接口
2. **WebSocket**：判题结果推送，减少轮询
3. **Special Judge**：输出多解题目
4. **压测**：`nloj_api_bench` 对 `GET /problems/{id}` 压测，输出 QPS / P99 / 缓存命中率（见 `docs/bench-report.md`）
5. **CI**：GitHub Actions 编译 + 单元测试

---

## 6. 简历项目描述示例

```
NLOJ 在线判题后端 | C++20 / CMake / MySQL / Redis / RabbitMQ / Docker
- NLOJ 项目采用 CMake 多模块（nl-common ~ nl-api），REST + OpenAPI 文档
- 提交与判题解耦：RabbitMQ 异步消费，Docker 沙箱隔离运行不可信代码
- Redis 缓存题目详情，更新时主动失效；submission 表复合索引优化分页查询
- 支持 C++ 判题，策略模式封装输出比对，预留 SPJ 扩展
```

---

## 7. 自测清单（实现完成后勾选）

- [ ] 注册登录全流程，密码 PBKDF2 存储
- [ ] 未登录访问 /submissions 返回 401
- [ ] admin 才能 POST /problems
- [ ] 提交后 status 从 PENDING → JUDGING → AC/WA
- [ ] 恶意代码（死循环）被判为 TLE
- [ ] fork bomb 被 pids-limit 拦截
- [ ] 能用 STAR 在 3 分钟内讲清架构
