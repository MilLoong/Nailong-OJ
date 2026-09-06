# NLOJ

C++20 在线判题后端：用户鉴权、题目管理、提交评测。提交写入 MySQL 后经 RabbitMQ 异步判题，用户代码在 Docker 沙箱里编译运行。

## 模块

| 模块 | 职责 |
|------|------|
| `nl-common` | MySQL 连接池、Redis、RabbitMQ、配置、错误码 |
| `nl-user` | 注册 / 登录、PBKDF2、HS256 JWT |
| `nl-problem` | 题目 CRUD、详情缓存 |
| `nl-submit` | 提交落库、投递判题任务 |
| `nl-judge` | 沙箱评测；独立进程 `nloj_judge_node` |
| `nl-api` | HTTP 入口，Swagger UI：`/api/docs` |

## 依赖

MySQL 8、Redis、RabbitMQ。可用仓库根目录的 `docker-compose.yml` 拉起。

默认开发账号与 `config.example.json` 一致：MySQL `nloj` / `nloj123456` / `nloj_db`，RabbitMQ 同用户名密码，Redis 无密码。

```bash
docker compose up -d
mysql -h 127.0.0.1 -u nloj -pnloj123456 nloj_db < sql/schema.sql
```

已有库要加题型 / SPJ 字段时执行 `sql/migrate_v2_judge_types.sql`。

## 构建

复制 `config.example.json` 为 `config.json`，或设置 `NLOJ_CONFIG` / `NLOJ_JWT_SECRET` 等环境变量。

### Linux

```bash
sudo apt-get install -y g++ cmake libssl-dev libmysqlclient-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

### Windows

需要 VS2022、OpenSSL（可用 Anaconda），以及 MySQL 客户端库。

```powershell
$env:OPENSSL_ROOT_DIR = "C:\anaconda\anaconda3\Library"
cmake --preset vs2022
cmake --build --preset debug
```

启动 API：

```powershell
.\build\bin\Debug\nloj_api.exe
```

浏览器打开 [http://127.0.0.1:8080/api/docs](http://127.0.0.1:8080/api/docs)。独立判题进程为 `nloj_judge_node`；设置 `NLOJ_EMBED_WORKER=0` 后 API 不再内嵌 Worker。

## 语言与题型

- 语言：`CPP` / `C` / `PYTHON` / `JAVA`
- 题型：`STANDARD` / `INTERACTIVE` / `COMMUNICATION`
- 比对：`EXACT` / `SPJ`

Docker 默认镜像 `gcc:13-bookworm`。Python / Java 可先构建 `deploy/judge.Dockerfile` 得到 `nloj-judge:bookworm`；没有该镜像时走本机解释器（无隔离）。反向代理示例见 `deploy/nginx.conf`。

接口契约：[`docs/openapi.yaml`](docs/openapi.yaml)。

## License

MIT
