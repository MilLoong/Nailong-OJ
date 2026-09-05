#include "http_json.h"

namespace nloj::api {

std::string extract_bearer(const std::string& authorization) {
    const std::string prefix = "Bearer ";
    if (authorization.size() <= prefix.size()) {
        return {};
    }
    if (authorization.compare(0, prefix.size(), prefix) != 0) {
        return {};
    }
    return authorization.substr(prefix.size());
}

int parse_i64(const std::string& raw, std::int64_t& out) {
    if (raw.empty()) {
        return 0;
    }
    try {
        std::size_t n = 0;
        out = std::stoll(raw, &n);
        if (n != raw.size()) {
            return 0;
        }
    } catch (...) {
        return 0;
    }
    return 1;
}

int parse_page_query(const std::string& page_num_raw,
                     const std::string& page_size_raw,
                     std::int64_t& page_num,
                     std::int64_t& page_size) {
    page_num = 1;
    page_size = 20;
    if (!page_num_raw.empty()) {
        if (!parse_i64(page_num_raw, page_num)) {
            return 0;
        }
    }
    if (!page_size_raw.empty()) {
        if (!parse_i64(page_size_raw, page_size)) {
            return 0;
        }
    }
    if (page_num < 1 || page_size < 1 || page_size > 100) {
        return 0;
    }
    return 1;
}

std::string json_ok(const nlohmann::json& data) {
    nlohmann::json body;
    body["code"] = 0;
    body["message"] = "ok";
    body["data"] = data;
    return body.dump();
}

std::string json_err(int code, const std::string& message) {
    nlohmann::json body;
    body["code"] = code;
    body["message"] = message;
    body["data"] = nullptr;
    return body.dump();
}

nlohmann::json int_or_null(int v) {
    if (v < 0) {
        return nullptr;
    }
    return v;
}

}  // namespace nloj::api
