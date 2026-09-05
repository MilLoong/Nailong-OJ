#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <httplib.h>

namespace nloj::api {

// 注册 api.md 全部 HTTP 路由（统一 JSON、鉴权、题目/提交）。
void register_http_routes(httplib::Server& svr);

// 阻塞消费判题队列。调用方放到独立线程。
void judge_worker_loop();

}  // namespace nloj::api
