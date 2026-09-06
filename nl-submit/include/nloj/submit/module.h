#pragma once

#include "nloj/common/error.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nloj::submit {

const char* module_name();

/**
 * @brief 提交详情，对应 GET /api/v1/submissions/{id} 的 data。
 */
struct SubmissionDetail {
    std::int64_t id;           ///< 提交主键
    std::int64_t user_id;      ///< 提交用户
    std::int64_t problem_id;   ///< 题目
    std::string language;      ///< CPP | C | PYTHON | JAVA
    std::string code;          ///< 源代码（列表接口可不填）
    std::string status;        ///< PENDING | JUDGING | AC | ...
    int time_used;             ///< ms；未出结果可用 -1 表示 null
    int memory_used;           ///< KB；未出结果可用 -1 表示 null
    std::string judge_info;    ///< 判题说明，可空
    std::string create_time;
};

/**
 * @brief 我的提交分页，对应 GET /api/v1/submissions 的 data。
 */
struct SubmissionPage {
    std::int64_t page_num;
    std::int64_t page_size;
    std::int64_t total;
    std::vector<SubmissionDetail> records;  ///< 可不含完整 code
};

/**
 * @brief 提交代码。对应 POST /api/v1/submissions。
 * @param user_id 来自 token，不要用客户端乱传的用户 id
 * @return 成功返回 submission id；失败返回 -1，原因写入 @p err
 */
std::int64_t create_submission(std::int64_t user_id,
                               std::int64_t problem_id,
                               const std::string& language,
                               const std::string& code,
                               nloj::common::AppError* err = nullptr);

/**
 * @brief 提交详情。对应 GET /api/v1/submissions/{id}。
 * @param viewer_user_id 用于校验本人或 admin
 * @return 失败时 id==0
 */
SubmissionDetail get_submission(std::int64_t id,
                                std::int64_t viewer_user_id,
                                const std::string& viewer_role);

/**
 * @brief 我的提交列表。对应 GET /api/v1/submissions。
 * @param problem_id 0 表示不按题过滤
 * @param status 空表示不限
 */
SubmissionPage list_my_submissions(std::int64_t user_id,
                                   std::int64_t page_num,
                                   std::int64_t page_size,
                                   std::int64_t problem_id,
                                   const std::string& status);

}  // namespace nloj::submit
