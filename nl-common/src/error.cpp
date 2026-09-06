#include "nloj/common/error.h"

namespace nloj::common {

void set_error(AppError* err, AppError value) {
    if (err != nullptr) {
        *err = value;
    }
}

int http_code(AppError err) {
    switch (err) {
        case AppError::Ok:
            return 0;
        case AppError::InvalidArgument:
        case AppError::UsernameTaken:
            return 40000;
        case AppError::WrongPassword:
            return 40100;
        case AppError::Forbidden:
            return 40101;
        case AppError::NotFound:
            return 40400;
        case AppError::Database:
            return 50000;
        case AppError::Internal:
            return 50001;
    }
    return 50001;
}

const char* http_message(AppError err) {
    switch (err) {
        case AppError::Ok:
            return "ok";
        case AppError::InvalidArgument:
            return "请求参数错误";
        case AppError::UsernameTaken:
            return "用户名已存在";
        case AppError::WrongPassword:
            return "用户名或密码错误";
        case AppError::NotFound:
            return "资源不存在";
        case AppError::Forbidden:
            return "无权限";
        case AppError::Database:
            return "系统内部异常";
        case AppError::Internal:
            return "操作失败";
    }
    return "操作失败";
}

}  // namespace nloj::common
