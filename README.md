# NLOJ — 在线判题系统后端

**NLOJ**（Online Judge）面向腾讯后台开发校招的 **C++ 后端**练手项目。当前处于 **Phase B**：CMake 骨架与 `nl-user`（PBKDF2 + JWT）已落地，HTTP / 题目 / 判题待续。

## 一句话亮点（简历用）

> 自研 NLOJ 在线判题后端：C++20 模块化单体 + RabbitMQ 异步判题 + Docker 代码沙箱，覆盖用户鉴权、题目管理、提交评测全链路，并针对高并发读题场景设计 Redis 缓存与消息队列削峰。

## 技术栈


| 层级     | 选型                       | 说明                            |
| ------ | ------------------------ | ----------------------------- |
| 语言     | C++20                    | 高性能后台，腾讯后台常见语言                |
| HTTP   | cpp-httplib              | 轻量 REST，无 Boost 依赖            |
| JSON   | nlohmann/json            | 请求/响应序列化                      |
| 数据库    | MySQL 8 + libmysqlclient | 连接池，查询转义/预处理防注入               |
| 缓存     | Redis + hiredis          | Token/题目缓存                    |
| 消息队列   | RabbitMQ + rabbitmq-c    | 异步判题、削峰填谷                     |
| 鉴权     | jwt-cpp + OpenSSL        | HS256 JWT                     |
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
    ├── sql/
    │   └── schema.sql
    ├── docs/
    │   ├── architecture.md
    │   ├── api.md
    │   ├── interview.md
    │   └── openapi.yaml
    ├── nl-common/              # 公共模块：Result、错误码、枚举
    ├── nl-user/                # 用户模块：注册、登录、鉴权
    ├── nl-problem/             # 题目模块：CRUD、用例
    ├── nl-submit/              # 提交模块：创建提交、发 MQ
    ├── nl-judge/               # 判题模块：消费 MQ、沙箱、比对
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

> Phase B 的 **Docker 代码沙箱**才真正需要 Docker。到那一步再装 [Docker Desktop](https://www.docker.com/products/docker-desktop/) 即可；前期判题也可以先做成「本机受限进程」做演示。



### 方式 B：Docker Compose（已安装 Docker 时）

```bash
cd NLOJ
docker compose up -d
```

首次启动会自动执行 `sql/schema.sql`。也可手动导入：

```bash
mysql -h 127.0.0.1 -u nloj -pnloj123456 nloj_db < sql/schema.sql
```



### 应用配置参考（自行实现时使用）

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

骨架可执行文件：`build\bin\Debug\nloj_api.exe`。跑起来应打印模块名列表。


## 核心功能

- **用户**：注册、登录（JWT 或 Redis Session）、获取当前用户
- **题目**：分页列表、详情、管理员 CRUD、样例用例展示
- **提交**：提交代码 → 落库 PENDING → 发 MQ → 返回 submissionId
- **判题**：消费 MQ → Docker 沙箱编译运行 → 比对用例 → 更新 AC/WA/TLE 等



## 文档索引


| 文档                                           | 内容                    |
| -------------------------------------------- | --------------------- |
| [docs/architecture.md](docs/architecture.md) | 架构图、模块职责、判题时序、扩展点     |
| [docs/api.md](docs/api.md)                   | REST 接口清单、请求/响应示例、错误码 |
| [docs/interview.md](docs/interview.md)       | 校招 STAR 叙述、高频面试题      |
| [docs/openapi.yaml](docs/openapi.yaml)       | OpenAPI 3 机器可读契约      |
| [sql/schema.sql](sql/schema.sql)             | 数据库表结构与索引             |




## 开发路线图



### Phase A：文档 + 基础设施

- 技术选型、架构设计、接口契约、库表设计
- Docker Compose 本地环境（可选）



### Phase B：核心链路跑通

- [x] CMake 多模块骨架（`nloj` 父工程 + `nl-*` 子模块）
- [x] 用户注册登录（PBKDF2 + JWT；`nl-user` 领域逻辑 + crypto 单元测试）
- [ ] 题目 CRUD + 分页
- [ ] 提交 + RabbitMQ 异步判题
- [ ] Docker 沙箱（先支持 C++，可扩展 Python/Go/Java）
- [ ] HTTP 接入（`nl-api` 路由 / 统一错误码）



### Phase C：工程化与亮点

- Redis 题目缓存（防穿透/击穿）
- 提交列表分页、按题目/状态筛选
- 管理员权限拦截
- 压测报告、单元测试



### Phase D（可选）：拆分演进

- 判题进程独立部署、水平扩展
- 引入反向代理做统一鉴权与限流



## License

MIT（个人学习项目）