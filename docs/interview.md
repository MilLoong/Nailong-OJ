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
| 判题 | 抽象 `JudgeSandbox` 接口，Docker 容器 `--network=none` + 内存/CPU 限制；编译一次后单容器跑完全部用例，容器内逐用例计时并记录真实内存 |
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
- 判题可拆独立进程 `nloj_judge_node`：多进程竞争消费、Redis 心跳、超时 JUDGING 回收

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
- 编译一次后**单容器跑完全部用例**（不是每个用例起一个容器），容器内逐用例计时，用 `wait4/ru_maxrss` 记录内存峰值，`time_used` / `memory_used` 真实落库

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

### Q12（迭代）：判题为什么从「每个用例起一个容器」改成「单容器跑全部用例」？

**答**：`docker run` 每次有容器启动/挂载开销，N 个用例 = 1 次编译 + N 个容器，而且每用例计时会把容器启动也算进去，测得不准。改成：编译一个容器 + 运行阶段**一个容器**里由 runner 按顺序 `fork ./main` 喂入各用例输入，容器内逐用例计时。

追问1：为什么编译和运行不并成一个容器？——编译要放开内存下限（给 g++ 至少 512MB，防止误杀编译），运行要严格按题面 limit 判 MLE，两种限制不能在同一 cgroup 里共存，所以拆两个容器。
追问2：超时怎么判？——runner 自己掐表（steady_clock），到点主动 `SIGKILL` 记为 TLE；外部 SIGKILL（容器 OOM）才是 MLE。这样 TLE/MLE 不会混。
追问3：WA 为什么不在容器里判？——输出归一化（去 `\r`、行尾空白、末尾空行）只有一份实现（`judge_outputs_match`），放在宿主侧避免 runner 与宿主两套规则漂移。

### Q13（迭代）：判题结果里的 time_used / memory_used 现在可信吗？

**答**：time 是容器内实测耗时（不含 Docker 启动），memory 是 `wait4` 拿到的子进程 `ru_maxrss`（KB），AC 记全用例峰值，失败用例记该用例实测值。改之前 `memory_used` 一直是 NULL——只靠容器 OOM 判 MLE，从不落真实值，这是当时的短板。

追问1：Windows 本机降级路径为什么 memory 还是 NULL？——Windows 没有 `getrusage`，降级路径诚实记 -1（不编造数字），Linux/容器路径才有真实值。
追问2：计时有没有误差？——runner 每 2ms 轮询 `wait4`，边界上可能多放行 ~2ms 的越限；OJ 判题普遍可接受，要更严可以用 `setitimer`/`prlimit`。

### Q14（迭代）：Redis 抖动一次缓存就永久失效，这种坑你怎么发现/怎么修？

**答**：原实现启动时探测一次 Redis，之后任何一次读写下失败就把 `g_use_redis` 永久置 0——进程内 Redis 缓存从此关掉，还表现为「health 显示 UP 但缓存实际失效」。改成故障状态机：失败 → 关闭连接并进入 **5s 冷却**，冷却过后自动重连探测，恢复即重新启用（日志只在状态翻转时打一次）。

追问1：为什么失败后不立即重连？——每次 connect 有阻塞/占锁代价，Redis 挂掉时会让每个请求都卡一下，冷却 5s 是「尽快恢复」和「别熔断自损」的折中。
追问2：冷却期间请求怎么办？——读写直接返回 0，业务层照旧回源 MySQL（缓存本来就是加速层），L1 进程内缓存仍在。
追问3：怎么保证能自动发现这个 bug？——需要"断 Redis → 观察 → 恢复 Redis → 观察"的联调用例，属于测试基建缺口，已记录。

### Q15（迭代）：JWT 的 secret 写在代码里当兜底默认值有什么风险？

**答**：原来 `auth_crypto` 里硬编码 `"nloj-dev-secret-change-me"`，配置缺 secret 时静默退回它——任何知道代码的人都能用这个公开密钥伪造 admin token。改成：**secret 为空一律拒绝**（签发返回空、验签直接失败，防空密钥伪造），去掉头文件默认参数逼调用方显式传；`nloj_api` 启动时空 secret 直接拒绝启动，等于开发默认值则打 warning。

追问1：为什么用对称 HMAC-SHA256 不用 RS256？——HS256 简单够用、无公钥分发；多服务各自验签要独立鉴权服务时再考虑 RS256/密钥管理。
追问2：验签为什么安全？——只重算 header.payload 的 HMAC 与第三段做常量时间比较，不信任 header 里的 alg，天然免疫 `alg=none`/算法混淆。
追问3：密钥怎么换？——`NLOJ_JWT_SECRET` 环境变量注入，代码/仓库只留 `config.example.json`；JWT 无状态，轮换要等旧 token 过期或引入 key id。

### Q16（迭代）：提交接口为什么限 64KB？

**答**：`submission.code` 是 MEDIUMTEXT（上限约 16MB），不限长的话超大请求能撑爆 DB 行、判题沙箱写盘和带宽。校验放在领域层 `create_submission`，超限按参数错误（40000）拒绝，HTTP 层无需感知。

追问：还有哪些输入该限？——题面/用例长度、HTTP body 上限、判题输出读取上限（现为每用例 256KB）、`judge_info` 截断 500 字符。安全边界的思路是"每一层都假设上层不可信"。

### Q17（迭代）：你的 CI 是真跑测试还是"看起来绿"？

**答**：踩过坑——各子模块各自 `enable_testing()`，根目录没有 `CTestTestfile.cmake`，在 build 根目录跑 `ctest` 会报 "No tests were found"（其实根本没跑任何测试）。已在顶层补 `enable_testing()`，现在根目录 `ctest` 能发现全部 11 个测试。

追问：怎么防止以后又静默不跑？——CI 里用 `ctest --output-on-failure` 并把"0 tests"当失败处理（断言测试数），这是教训：**CI 绿不代表被测过，要看有没有测试被执行**。

### Q18（软实力）：讲一个「发现旧设计有坑 → 重做」的例子

**答**：三个素材任选其一，按 STAR 讲：① 判题每用例一个 Docker 容器、计时含启动开销、memory 恒 NULL → 重做成单容器 runner + 容器内计时 + `ru_maxrss`；② Redis 断线一次永久降级 → 冷却自动重连状态机；③ JWT 硬编码默认密钥兜底 → 空密钥 fail-fast + 启动校验。重点讲**怎么发现的**（读代码/故障注入/压测对照），**改动的代价**（多一次 wait4 轮询、多一层状态机），以及**哪些还没验证**（本机 Docker 引擎起不来，judge 联调要在 Linux CI 覆盖）——诚实说短板反而加分。

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
- [ ] judge_db 跑通后 `submission.memory_used` 有真实值（非 NULL）
- [ ] 停 Redis 后缓存失效、恢复后 5s 内自动重连（health 回 UP）
- [ ] 提交 >64KB 代码被 40000 拒绝
- [ ] 空 `jwt_secret` 启动直接拒绝
- [ ] 能用 STAR 在 3 分钟内讲清架构
