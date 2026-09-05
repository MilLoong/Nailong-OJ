#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace nloj::api {

// 从 Authorization 头取出 Bearer token。没有或格式不对返回空串。
std::string extract_bearer(const std::string& authorization);

// Query 分页。空串表示缺省（pageNum=1, pageSize=20）。
// 合法返回 1；非法（非数字、pageNum<1、pageSize 不在 1..100）返回 0。
int parse_page_query(const std::string& page_num_raw,
                     const std::string& page_size_raw,
                     std::int64_t& page_num,
                     std::int64_t& page_size);

// 整段字符串转 int64。必须整串都是数字。成功 1。
int parse_i64(const std::string& raw, std::int64_t& out);

// 统一成功包。data 可以是对象、数组、数字或 null。
std::string json_ok(const nlohmann::json& data);

// 统一错误包。data 固定为 null。
std::string json_err(int code, const std::string& message);

// 未出结果时领域用 -1 表示 null。
nlohmann::json int_or_null(int v);

}  // namespace nloj::api
