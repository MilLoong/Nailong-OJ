# NLOJ — 在线判题系统后端

**NLOJ**（Online Judge）面向腾讯后台开发校招的 **C++ 后端**练手项目。**A～D 路线图已完成**：单体可跑通全链路，也可拆出 `nloj_judge_node` 多进程竞争消费。

## 一句话亮点（简历用）

> 自研 NLOJ 在线判题后端：C++20 模块化单体 + RabbitMQ 异步判题 + Docker 代码沙箱，覆盖用户鉴权、题目管理、提交评测全链路，并针对高并发读题场景设计 Redis 缓存与消息队列削峰。

## 技术栈


| 层级     | 选型                       | 说明                            |
| ------ | ------------------------ | ----------------------------- |
| 语言     | C++20                    | 高性能后台，腾讯后台常见语言                |
| HTTP   | cpp-httplib              | 轻量 REST，无 Boost 依赖            |
| JSON   | nlohmann/json            | 请求/响应序列化                      |
| 数据库    | MySQL 8 + libmysqlclient | 连接池；登录注册预处理，其余转义拼接           |
| 缓存     | Redis（RESP 直连）           | 题目详情 String 缓存；连不上 6379 则跳过 |
| 消息队列   | RabbitMQ + rabbitmq-c    | 异步判题、削峰填谷                     |
| 鉴权     | OpenSSL HMAC-SHA256      | 手写 HS256 JWT；payload 用 nlohmann |
| 密码     | PBKDF2-HMAC-SHA256       | OpenSSL 实现                    |
| API 文档 | OpenAPI 3                | `docs/openapi.yaml`           |
| 判题沙箱   | Docker（Phase B）          | 隔离运行不可信代码                     |
| 构建     | CMake 多模块 + vcpkg        | 模块化单体，后续可拆独立进程                |
| 本地依赖   | Docker Compose           | MySQL / Redis / RabbitMQ 一键启动 |




## 项目结构

整个 **NLOJ** 是一个独立项目，所有子模块都在这个文件夹里：

```
MyProject/
└── NLOJ/                       ← 项目根目录（你在这里开发）
    ├── README.md
    ├── CMakeLists.txt          # CMake 父工程
    ├── docker-compose.yml
    ├── config.example.json     # 复制为 config.json 或设 NLOJ_CONFIG
    ├── .github/workflows/ci.yml
    ├── sql/
    │   └── schema.sql
    ├── docs/
    │   ├── architecture.md
    │   ├── api.md
    │   ├── interview.md
    │   ├── bench-report.md
    │   └── openapi.yaml
    ├── scripts/
    │   └── bench.ps1              # 读题压测；原始数字写 bench-report.generated.md
    ├── deploy/
    │   └── nginx.conf             # 反向代理 + 提交限流示例
    ├── nl-common/              # 公共模块：工具类、错误码/枚举、Result/分页、第三方与基础设施封装、配置常量
    ├── nl-user/                # 用户模块：注册、登录、鉴权
    ├── nl-problem/             # 题目模块：CRUD、用例
    ├── nl-submit/              # 提交模块：创建提交、发 MQ
    ├── nl-judge/               # 判题模块：消费 MQ、沙箱、比对；可执行文件 nloj_judge_node
    └── nl-api/                 # 启动模块：HTTP 路由、配置
```

**命名约定**


| 层级       | 名称                     | 说明                            |
| -------- | ---------------------- | ----------------------------- |
| 项目文件夹    | `NLOJ`                 | 一眼识别这是完整项目                    |
| CMake 工程 | `nloj`                 | project() 名称，小写               |
| 子模块      | `nl-common`、`nl-user`… | `nl-` 前缀表示 NLOJ 子模块           |
| C++ 命名空间 | `nloj::*`              | 如 `nloj::common`、`nloj::user` |




## 快速开始（基础设施）

**现在（Phase A）不用装任何中间件**，看文档即可。`docker-compose.yml` 只是可选的一键启动方式，**没装 Docker 完全没问题**。

以后写代码时，需要本机有 MySQL / Redis / RabbitMQ，连的都是 `localhost`，和用不用 Docker 无关：


| 服务           | 地址                                               | 账号                             |
| ------------ | ------------------------------------------------ | ------------------------------ |
| MySQL        | localhost:3306                                   | nloj / nloj123456，库名 `nloj_db` |
| Redis        | localhost:6379                                   | 无密码                            |
| RabbitMQ     | localhost:5672                                   | nloj / nloj123456              |
| RabbitMQ 控制台 | [http://localhost:15672](http://localhost:15672) | nloj / nloj123456              |




### 方式 A：Windows 本机安装（无 Docker）

管理员 PowerShell 可用 `winget`（没有就去各官网下载安装包）：

```powershell
winget install Oracle.MySQL
winget install Redis.Redis
winget install RabbitMQ.RabbitMQ
```

装完后：

1. **MySQL**：用安装向导设 root 密码，打开 MySQL Command Line 或 MySQL Workbench，执行：

```sql
CREATE DATABASE IF NOT EXISTS nloj_db
    DEFAULT CHARACTER SET utf8mb4
    DEFAULT COLLATE utf8mb4_unicode_ci;
CREATE USER IF NOT EXISTS 'nloj'@'localhost' IDENTIFIED BY 'nloj123456';
GRANT ALL PRIVILEGES ON nloj_db.* TO 'nloj'@'localhost';
FLUSH PRIVILEGES;
```

然后导入表结构（把路径换成你的仓库路径）：

```powershell
mysql -h 127.0.0.1 -u nloj -pnloj123456 nloj_db < C:\code\MyProject\NLOJ\sql\schema.sql
```

1. **Redis**：安装后应已监听 `6379`。PowerShell 里 `redis-cli ping`，返回 `PONG` 即成功。本项目开发环境不设密码。
2. **RabbitMQ**：依赖 Erlang，安装包一般会带。浏览器打开 [http://localhost:15672](http://localhost:15672) ，用下面命令创建与文档一致的账号（默认 guest 只能本机用）：

```powershell
rabbitmqctl add_user nloj nloj123456
rabbitmqctl set_user_tags nloj administrator
rabbitmqctl set_permissions -p / nloj ".*" ".*" ".*"
```

应用配置仍指向 `127.0.0.1`，不必改文档里的端口。

判题沙箱需要 [Docker Desktop](https://www.docker.com/products/docker-desktop/) 与镜像 `gcc:13-bookworm`（`docker pull gcc:13-bookworm`）。`docker version` 连不上时才降级本机 g++（无隔离，仅演示）。注意：自定义 WSL 内核若缺 `iso9660`，Desktop 会卡在 Start engine，需先去掉 `.wslconfig` 里的 `kernel=`。



### 方式 B：Docker Compose（已安装 Docker 时）

```bash
cd NLOJ
docker compose up -d
```

首次启动会自动执行 `sql/schema.sql`。也可手动导入：

```bash
mysql -h 127.0.0.1 -u nloj -pnloj123456 nloj_db < sql/schema.sql
```



### 应用配置

启动时读 `NLOJ_CONFIG` 指定的 JSON，否则读当前目录 `config.json`，再用环境变量覆盖。示例见 [config.example.json](config.example.json)。常用变量：`NLOJ_MYSQL_*`、`NLOJ_JWT_SECRET`、`NLOJ_HTTP_PORT`、`NLOJ_RABBITMQ_*`、`NLOJ_REDIS_*`。

示例（与默认开发环境一致）：

```json
{
  "server": {
    "host": "0.0.0.0",
    "port": 8080,
    "jwt_secret": "nloj-dev-secret-change-me"
  },
  "mysql": {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "nloj",
    "password": "nloj123456",
    "database": "nloj_db"
  },
  "redis": {
    "host": "localhost",
    "port": 6379
  },
  "rabbitmq": {
    "host": "localhost",
    "port": 5672,
    "username": "nloj",
    "password": "nloj123456"
  }
}
```



## 编译（CMake 第一阶段）

这一阶段有多模块骨架与 `nl-user`。密码 / JWT 单元测试不依赖 MySQL 服务；注册登录联调需要本机 MySQL。

Windows（VS2022 + 本仓库 `third_party` 里的 Connector/C，OpenSSL 可用 Anaconda）：

```powershell
$env:OPENSSL_ROOT_DIR = "C:\anaconda\anaconda3\Library"
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --preset vs2022
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset debug
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset debug --target nloj_user_crypto_test
.\build\bin\Debug\nloj_user_crypto_test.exe
```

本机若已把 CMake 加进 PATH：

```powershell
cmake --preset vs2022
cmake --build --preset debug --target nloj_user_crypto_test
.\build\bin\Debug\nloj_user_crypto_test.exe
```

骨架可执行文件：`build\bin\Debug\nloj_api.exe`。跑起来应打印模块名列表。浏览器打开 [http://127.0.0.1:8080/api/docs](http://127.0.0.1:8080/api/docs) 是 Swagger UI（C++ 没有 Knife4j，契约仍是 `docs/openapi.yaml`）。登录后点 **Authorize** 填 token 即可在页面上试接口。

读题压测（需 `nloj_api` 已在 8080、库里至少一道题）：

```powershell
cmake --build --preset debug --target nloj_api_bench
powershell -File scripts/bench.ps1
```

中文结论在 [docs/bench-report.md](docs/bench-report.md)。复跑脚本另写 `docs/bench-report.generated.md`。对照关缓存用请求头 `X-NLOJ-Skip-Cache: 1`。

独立判题进程（需 RabbitMQ；可与 API 同时开多个，竞争同一队列）：

```powershell
.\build\bin\Debug\nloj_judge_node.exe
# API 不再内嵌 Worker 时：
$env:NLOJ_EMBED_WORKER = "0"
.\build\bin\Debug\nloj_api.exe
```

`GET /api/v1/health` 的 `judgeNodes` 是 Redis 心跳未过期的节点 id。反向代理示例见 `deploy/nginx.conf`。


## 核心功能

- **用户**：注册、登录（JWT 或 Redis Session）、获取当前用户
- **题目**：分页列表、详情、管理员 CRUD、样例用例展示
- **提交**：提交代码 → 落库 PENDING → 发 MQ → 返回 submissionId
- **判题**：消费 MQ → 沙箱编译 + 单容器跑完全部用例（容器内计时、记录内存）→ 比对用例 → 更新 AC/WA/TLE 等



## 文档索引


| 文档                                           | 内容                    |
| -------------------------------------------- | --------------------- |
| [docs/architecture.md](docs/architecture.md) | 架构图、模块职责、判题时序、扩展点     |
| [docs/api.md](docs/api.md)                   | REST 接口清单、请求/响应示例、错误码 |
| [docs/impl-mapping.md](docs/impl-mapping.md) | API → 业务函数 → 表字段对照（含「领域」说明） |
| [docs/interview.md](docs/interview.md)       | 校招 STAR 叙述、高频面试题      |
| [docs/bench-report.md](docs/bench-report.md) | 本机压测 QPS / P99 / 缓存命中率 |
| [docs/openapi.yaml](docs/openapi.yaml)       | OpenAPI 3 机器可读契约      |
| [sql/schema.sql](sql/schema.sql)             | 数据库表结构与索引             |




## 开发路线图



### Phase A：文档 + 基础设施

- 技术选型、架构设计、接口契约、库表设计
- Docker Compose 本地环境（可选）



### Phase B：核心链路跑通

- [x] CMake 多模块骨架（`nloj` 父工程 + `nl-*` 子模块）
- [x] 用户注册登录（PBKDF2 + JWT；`nl-user` 领域逻辑 + crypto 单元测试）
- [x] 题目 CRUD + 分页（`nl-problem` 领域逻辑 + DB 联调测试）
- [x] 提交（create/get/list + MQ 投递；`nl-submit` DB 联调测试）
- [x] RabbitMQ 异步判题（`rabbitmq-c`；连不上 5672 降级进程内队列；`nloj_common_mq_test`）
- [x] Docker 沙箱（`gcc:13-bookworm`；`--network=none --memory --pids-limit=64`；编译一次 + 单容器跑完全部用例，容器内逐用例计时并用 `ru_maxrss` 记录内存、`memory_used` 落库；无 Docker 时本机 g++ 降级；本机 Desktop 已跑通 `nloj_judge_db_test` AC/WA/CE + `nloj_api_e2e_test`）
- [x] HTTP 接入（`nl-api` cpp-httplib 路由 / 统一 code-message-data；`nloj_api_http_test`；Swagger UI `/api/docs`）
- [x] HTTP 全链路集成（进程内起服务打注册/建题/提交/判题；`nloj_api_e2e_test`）



### Phase C：工程化与亮点

- [x] Redis 题目缓存（`nloj:problem:{id}` String JSON；空值短 TTL 防穿透；SET NX 锁 + 进程内 L1 防击穿；TTL 抖动防雪崩；更新 DEL）
- [x] 提交列表分页、按题目/状态筛选（`list_my_submissions` + HTTP `problemId` / `status`）
- [x] 管理员权限拦截（建题 / 改题；无权限 `40101`）
- [x] 压测 + 故障注入数据（`nloj_api_bench` + `scripts/bench.ps1`；报告 [bench-report.md](docs/bench-report.md)）
- [x] 单元测试 / 联调测试（crypto / DB / MQ / Redis / HTTP JSON / `nloj_api_e2e_test`）
- [x] 工程化：MySQL 连接池、`config.json`/环境变量、领域 `AppError`、MQ/JWT 用 nlohmann、GitHub Actions CI



### Phase D（可选）：拆分演进

- [x] 多节点判题机：多进程竞争消费同一 RabbitMQ 队列；Redis 心跳 TTL；`JUDGING` 超时改回 PENDING 再入队（`reclaim_stale_judging`）
- [x] 判题进程独立部署（`nloj_judge_node` / `JudgeNode`）；API 默认仍内嵌 Worker，`NLOJ_EMBED_WORKER=0` 关掉
- [x] 反向代理示例（`deploy/nginx.conf`：入口转发 + `/api/v1/submissions` 限流；JWT 仍在 API）



路线图到此结束。后续若继续，属于加分项（限流进进程、WebSocket 推结果、SPJ 等），不是未完成的 Phase。

## License

MIT（个人学习项目）