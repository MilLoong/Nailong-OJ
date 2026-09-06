#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <httplib.h>

namespace nloj::api {

/**
 * @brief 注册全部 HTTP 路由（统一 JSON、鉴权、题目/提交）。
 */
void register_http_routes(httplib::Server& svr);

/**
 * @brief 阻塞消费判题队列。调用方放到独立线程。
 */
void judge_worker_loop();

/**
 * @brief 是否在 API 进程内嵌 Worker。
 *
 * 环境变量 NLOJ_EMBED_WORKER=0 时关闭（改由 nloj_judge_node 消费）。
 */
int embed_judge_worker_enabled();

}  // namespace nloj::api
