#include "nloj/submit/module.h"
#include "nloj/common/mq.h"
#include "nloj/common/mysql.h"

namespace {

// 提交代码上限（64KB）。防超大请求撑爆 DB / 判题沙箱。
constexpr std::size_t kMaxCodeBytes = 64 * 1024;

}  // namespace

namespace nloj::submit {

const char* module_name() {
    return "nl-submit";
}

std::int64_t create_submission(std::int64_t user_id,
                               std::int64_t problem_id,
                               const std::string& language,
                               const std::string& code,
                               nloj::common::AppError* err) {
    // 校验题目存在且可见 → INSERT PENDING → 发判题消息 → 返回 submission_id

    // 基本校验（语言放行 CPP/C/PYTHON/JAVA；代码超限按参数错误拒绝）
    if (user_id <= 0 || problem_id <= 0 || code.empty()
     || code.size() > kMaxCodeBytes
     || (language != "CPP" && language != "C"
      && language != "PYTHON" && language != "JAVA")) {
        nloj::common::set_error(err, nloj::common::AppError::InvalidArgument);
        return -1;  // 代码超限也算参数错误
    }

    nloj::common::MysqlConn conn;
    if (!conn.ok()) {
        nloj::common::set_error(err, nloj::common::AppError::Database);
        return -1;
    }

    // 校验题目存在且可见
    const std::string select_sql = "SELECT id FROM problem WHERE id="
                                  + std::to_string(problem_id)
                                  + " AND deleted=0 AND visible=1 LIMIT 1";
    MYSQL_RES* result = nloj::common::query_select(conn.get(), select_sql);
    if (result == nullptr) {
        nloj::common::set_error(err, nloj::common::AppError::Database);
        return -1;
    }
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row == nullptr || row[0] == nullptr) {
        mysql_free_result(result);
        nloj::common::set_error(err, nloj::common::AppError::NotFound);
        return -1;  // 题目不存在或不可见
    }
    mysql_free_result(result);

    // INSERT PENDING（status 默认也可，这里显式写出）
    const std::string escaped_language = nloj::common::escape_sql(conn.get(), language);
    const std::string escaped_code = nloj::common::escape_sql(conn.get(), code);
    const std::string insert_sql = "INSERT INTO submission (user_id, problem_id, language, code, status) VALUES ("
                                  + std::to_string(user_id) + ", "
                                  + std::to_string(problem_id) + ", '"
                                  + escaped_language + "', '"
                                  + escaped_code + "', 'PENDING')";
    if (!nloj::common::query_exec(conn.get(), insert_sql)) {
        nloj::common::set_error(err, nloj::common::AppError::Database);
        return -1;
    }

    const std::int64_t submission_id = static_cast<std::int64_t>(mysql_insert_id(conn.get()));

    nloj::common::JudgeTaskMessage task;
    task.submission_id = submission_id;
    task.problem_id = problem_id;
    task.language = language;
    if (!nloj::common::publish_judge_task(task)) {
        nloj::common::set_error(err, nloj::common::AppError::Internal);
        return -1;
    }

    nloj::common::set_error(err, nloj::common::AppError::Ok);
    return submission_id;
}

SubmissionDetail get_submission(std::int64_t id,
                                std::int64_t viewer_user_id,
                                const std::string& viewer_role) {
    // 按 id 查 submission → 校验本人或 admin → 填 SubmissionDetail

    if (id <= 0 || viewer_user_id <= 0) {
        return {};
    }

    // mysql 初始化、连接
    nloj::common::MysqlConn conn;
    if (!conn.ok()) {
        return {};
    }

    // 按 id 查 submission
    const std::string select_sql = "SELECT id, user_id, problem_id, "
                                   "language, code, status, "
                                   "time_used, memory_used, "
                                   "judge_info, create_time "
                                   "FROM submission WHERE id="
                                  + std::to_string(id)
                                  + " LIMIT 1";
    MYSQL_RES* result = nloj::common::query_select(conn.get(), select_sql);
    if (result == nullptr) {
        return {};
    }
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row == nullptr || row[0] == nullptr || row[1] == nullptr) {
        mysql_free_result(result);
        return {};  // 提交不存在
    }

    // 校验本人或 admin（viewer_* 来自 JWT，不要信客户端乱传的用户 id）
    const std::int64_t owner_id = std::stoll(row[1]);
    if (viewer_user_id != owner_id && viewer_role != "admin") {
        mysql_free_result(result);
        return {};  // 既不是提交者，也不是管理员
    }

    // 填 SubmissionDetail（time_used / memory_used / judge_info 可空）
    SubmissionDetail detail;
    detail.id = std::stoll(row[0]);
    detail.user_id = owner_id;
    detail.problem_id = row[2] != nullptr ? std::stoll(row[2]) : 0;
    detail.language = row[3] != nullptr ? row[3] : "";
    detail.code = row[4] != nullptr ? row[4] : "";
    detail.status = row[5] != nullptr ? row[5] : "";
    detail.time_used = row[6] != nullptr ? std::stoi(row[6]) : -1;
    detail.memory_used = row[7] != nullptr ? std::stoi(row[7]) : -1;
    detail.judge_info = row[8] != nullptr ? row[8] : "";
    detail.create_time = row[9] != nullptr ? row[9] : "";
    mysql_free_result(result);
    return detail;
}

SubmissionPage list_my_submissions(std::int64_t user_id,
                                   std::int64_t page_num,
                                   std::int64_t page_size,
                                   std::int64_t problem_id,
                                   const std::string& status) {
    // 按 user_id 与可选过滤 COUNT → SELECT 分页 → 填 SubmissionPage

    // 校验分页（页码从 1 起，每页最多 100）
    if (user_id <= 0 || page_num < 1 || page_size < 1 || page_size > 100) {
        return {};
    }

    // mysql 初始化、连接
    nloj::common::MysqlConn conn;
    if (!conn.ok()) {
        return {};
    }

    // 拼过滤条件（user_id 必有；problem_id==0 / status 空表示不限）
    std::string where_sql = " WHERE user_id=" + std::to_string(user_id);
    if (problem_id > 0) {
        where_sql += " AND problem_id=" + std::to_string(problem_id);
    }
    if (!status.empty()) {
        const std::string escaped_status = nloj::common::escape_sql(conn.get(), status);
        where_sql += " AND status='" + escaped_status + "'";
    }

    // COUNT total
    const std::string count_sql = "SELECT COUNT(*) FROM submission"
                                 + where_sql;
    MYSQL_RES* count_result = nloj::common::query_select(conn.get(), count_sql);
    if (count_result == nullptr) {
        return {};
    }
    MYSQL_ROW count_row = mysql_fetch_row(count_result);
    if (count_row == nullptr || count_row[0] == nullptr) {
        mysql_free_result(count_result);
        return {};
    }
    const std::int64_t total = std::stoll(count_row[0]);
    mysql_free_result(count_result);

    // SELECT 分页列表（不含完整 code）
    const std::int64_t offset = (page_num - 1) * page_size;
    const std::string list_sql = "SELECT id, user_id, problem_id, language, status, "
                                 "time_used, memory_used, judge_info, create_time FROM submission"
                                + where_sql
                                + " ORDER BY id DESC LIMIT "
                                + std::to_string(page_size)
                                + " OFFSET "
                                + std::to_string(offset);
    MYSQL_RES* list_result = nloj::common::query_select(conn.get(), list_sql);
    if (list_result == nullptr) {
        return {};
    }

    // 填 SubmissionPage
    SubmissionPage page;
    page.page_num = page_num;
    page.page_size = page_size;
    page.total = total;
    while (MYSQL_ROW row = mysql_fetch_row(list_result)) {
        if (row[0] == nullptr || row[1] == nullptr) {
            continue;
        }
        SubmissionDetail item;
        item.id = std::stoll(row[0]);
        item.user_id = std::stoll(row[1]);
        item.problem_id = row[2] != nullptr ? std::stoll(row[2]) : 0;
        item.language = row[3] != nullptr ? row[3] : "";
        item.status = row[4] != nullptr ? row[4] : "";
        item.time_used = row[5] != nullptr ? std::stoi(row[5]) : -1;
        item.memory_used = row[6] != nullptr ? std::stoi(row[6]) : -1;
        item.judge_info = row[7] != nullptr ? row[7] : "";
        item.create_time = row[8] != nullptr ? row[8] : "";
        page.records.push_back(item);
    }
    mysql_free_result(list_result);
    return page;
}

}  // namespace nloj::submit
