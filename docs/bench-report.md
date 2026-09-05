# NLOJ 压测与故障注入报告

本机实测时间：2026-09-05 23:12。客户端：`nloj_api_bench`（`scripts/bench.ps1`）。  
目标：`nloj_api` `http://127.0.0.1:8080`。读题详情走 L1 + Redis；题目列表不缓存。

| 项 | 值 |
| --- | --- |
| OS | Windows |
| 题目 id | 13 |
| health.mysql | UP |
| health.redis（压测前） | UP |
| health.rabbitmq | UP |
| 预热后 cache | total=11666，l1Hit=10299，redisHit=1，mysqlLoad=1366，hitRate=0.883 |

## 对比

| 场景 | QPS | P50 ms | P95 ms | P99 ms | 成功 | 失败 | 说明 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 详情·缓存命中 | 1378.8 | 9.438 | 21.342 | 34.842 | 11043 | 0 | 16 线程 × 8s，预热后打同一 id |
| 详情·跳过缓存 | 244.3 | 29.629 | 50.331 | 68.657 | 1230 | 0 | 8 线程 × 5s，头 `X-NLOJ-Skip-Cache: 1` 强制 MySQL |
| 题目列表 | 248.8 | 30.441 | 46.881 | 59.801 | 1249 | 0 | 8 线程 × 5s，不走 Redis |
| 穿透·首波 | 1475.8 | 3.548 | 9.804 | 41.705 | 5907 | 0 | 8 线程 × 4s，32 个不存在 id |
| 穿透·再打 | 1865.1 | 3.668 | 7.050 | 10.929 | 7466 | 0 | 同一批 id，走 `__nil__` 空值缓存 |

## 缓存命中率（热 id 场景）

`GET /problems/13` 压测前后，`GET /health` 的 `data.cache` 差值：

- 调用增量：11043
- 命中增量（L1 + Redis）：11043
- **命中率：1.000**
- 穿透场景结束后累计：total=37312，l1Hit=34514，redisHit=141，mysqlLoad=2657，hitRate=0.929

## 结论

- 同 id 详情：开缓存 QPS 约 **5.6 倍**于强制回源（1379 vs 244）；P99 **34.8ms**，低于架构目标 100ms。列表没有缓存，QPS / 延迟与强制回源详情接近。
- 不存在的 id：首波里前几个请求回源，P99 被拉到 41.7ms；同一批再打走空值短 TTL（防穿透），P99 降到 **10.9ms**，QPS 升到 1865。
- Redis 进程故障：本机 `Stop-Service Redis` 需要管理员权限，本次未真正停服务。对照用 `X-NLOJ-Skip-Cache: 1` 模拟缓存层整体失效（等价于 Redis + L1 都不走）。已启动的 `nloj_api` 对 Redis 只在启动时探测一次，即使后来 6379 断了，`health.redis` 仍可能显示 UP，读失败则回源 MySQL。
- 热路径几乎全是 L1：预热后 `redisHit` 很少，符合「进程内 60s L1，Redis 是跨进程/冷启动层」。

## 复现

```powershell
cmake --build --preset debug --target nloj_api_bench
# 另开终端启动 nloj_api，库里至少有一道题
powershell -File scripts/bench.ps1
```

复跑脚本会另写 `docs/bench-report.generated.md`，不覆盖本中文报告。
