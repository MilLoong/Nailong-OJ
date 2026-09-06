#pragma once

namespace nloj::common {

// 领域错误。HTTP 层用 http_code / http_message 映射到统一响应。
enum class AppError {
    Ok = 0,
    InvalidArgument,  // 参数不合法
    UsernameTaken,    // 用户名已占用
    WrongPassword,    // 账号或密码不对
    NotFound,         // 资源不存在
    Forbidden,        // 无权限
    Database,         // MySQL 等基础设施
    Internal,         // 其它操作失败
};

// 写入可选错误指针；err 为空则忽略。
void set_error(AppError* err, AppError value);

// 对应 api.md：0 / 40000 / 40100 / 40101 / 40400 / 50000 / 50001。
int http_code(AppError err);

// 给客户端看的短中文。
const char* http_message(AppError err);

}  // namespace nloj::common
