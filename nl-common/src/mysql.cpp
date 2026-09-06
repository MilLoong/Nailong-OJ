#include "nloj/common/mysql.h"
#include "nloj/common/config.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

namespace nloj::common {
namespace {

void log_mysql_error(MYSQL* conn) {
    std::cerr << mysql_error(conn) << std::endl;
}

int login_mysql(MYSQL* mysql) {
    const AppConfig& cfg = app_config();
    if (!mysql_real_connect(
            mysql,
            cfg.mysql_host.c_str(),
            cfg.mysql_user.c_str(),
            cfg.mysql_password.c_str(),
            cfg.mysql_database.c_str(),
            static_cast<unsigned int>(cfg.mysql_port),
            nullptr,
            0
        )) {
        log_mysql_error(mysql);
        return 0;
    }
    mysql_set_character_set(mysql, "utf8mb4");
    return 1;
}

MYSQL* open_pooled() {
    MYSQL* c = mysql_init(nullptr);
    if (c == nullptr) {
        return nullptr;
    }
    if (!login_mysql(c)) {
        mysql_close(c);
        return nullptr;
    }
    return c;
}

class MysqlPool {
public:
    MYSQL* acquire() {
        const int max_size = app_config().mysql_pool_size;
        std::unique_lock<std::mutex> lock(mu_);
        while (!idle_.empty()) {
            MYSQL* c = idle_.back();
            idle_.pop_back();
            lock.unlock();
            if (mysql_ping(c) == 0) {
                return c;
            }
            mysql_close(c);
            lock.lock();
            --live_;
        }
        if (live_ >= max_size) {
            // 池满再开一条溢出连接，归还时直接关掉
            ++overflow_;
            lock.unlock();
            MYSQL* extra = open_pooled();
            if (extra == nullptr) {
                lock.lock();
                --overflow_;
                return nullptr;
            }
            return extra;
        }
        ++live_;
        lock.unlock();
        MYSQL* c = open_pooled();
        if (c == nullptr) {
            lock.lock();
            --live_;
            return nullptr;
        }
        return c;
    }

    void release(MYSQL* c) {
        if (c == nullptr) {
            return;
        }
        const int max_size = app_config().mysql_pool_size;
        std::lock_guard<std::mutex> lock(mu_);
        if (overflow_ > 0) {
            --overflow_;
            mysql_close(c);
            return;
        }
        if (static_cast<int>(idle_.size()) >= max_size) {
            mysql_close(c);
            --live_;
            return;
        }
        idle_.push_back(c);
    }

    ~MysqlPool() {
        std::lock_guard<std::mutex> lock(mu_);
        for (MYSQL* c : idle_) {
            mysql_close(c);
        }
        idle_.clear();
        live_ = 0;
        overflow_ = 0;
    }

private:
    std::mutex mu_;
    std::vector<MYSQL*> idle_;
    int live_ = 0;
    int overflow_ = 0;
};

MysqlPool& pool() {
    static MysqlPool p;
    return p;
}

}  // namespace

std::string escape_sql(MYSQL* conn, const std::string& raw) {
    std::string escaped(raw.size() * 2 + 1, '\0');
    const auto n = mysql_real_escape_string(
        conn, escaped.data(), raw.c_str(), static_cast<unsigned long>(raw.size()));
    escaped.resize(n);
    return escaped;
}

MYSQL_RES* query_select(MYSQL* conn, const std::string& sql) {
    if (mysql_query(conn, sql.c_str()) != 0) {
        log_mysql_error(conn);
        return nullptr;
    }
    MYSQL_RES* result = mysql_store_result(conn);
    if (result == nullptr) {
        log_mysql_error(conn);
    }
    return result;
}

bool query_exec(MYSQL* conn, const std::string& sql) {
    if (mysql_query(conn, sql.c_str()) != 0) {
        log_mysql_error(conn);
        return 0;
    }
    return 1;
}

int query_stmt(MYSQL* conn,
               const char* sql,
               const std::vector<std::string>& args,
               std::vector<std::vector<std::string>>* rows,
               std::uint64_t* insert_id) {
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (stmt == nullptr) {
        return 0;
    }
    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
        std::cerr << mysql_stmt_error(stmt) << std::endl;
        mysql_stmt_close(stmt);
        return 0;
    }

    std::vector<MYSQL_BIND> binds(args.size());
    std::vector<unsigned long> lens(args.size());
    if (!args.empty()) {
        std::memset(binds.data(), 0, binds.size() * sizeof(MYSQL_BIND));
        for (std::size_t i = 0; i < args.size(); ++i) {
            lens[i] = static_cast<unsigned long>(args[i].size());
            binds[i].buffer_type = MYSQL_TYPE_STRING;
            binds[i].buffer = const_cast<char*>(args[i].data());
            binds[i].buffer_length = lens[i];
            binds[i].length = &lens[i];
        }
        if (mysql_stmt_bind_param(stmt, binds.data()) != 0) {
            std::cerr << mysql_stmt_error(stmt) << std::endl;
            mysql_stmt_close(stmt);
            return 0;
        }
    }

    if (mysql_stmt_execute(stmt) != 0) {
        std::cerr << mysql_stmt_error(stmt) << std::endl;
        mysql_stmt_close(stmt);
        return 0;
    }

    if (insert_id != nullptr) {
        *insert_id = mysql_stmt_insert_id(stmt);
    }

    if (rows == nullptr) {
        mysql_stmt_close(stmt);
        return 1;
    }

    MYSQL_RES* meta = mysql_stmt_result_metadata(stmt);
    if (meta == nullptr) {
        mysql_stmt_close(stmt);
        return 1;
    }
    const unsigned cols = mysql_num_fields(meta);
    struct ColBuf {
        std::vector<char> buf;
        unsigned long len;
        my_bool is_null;
        my_bool error;
    };
    std::vector<ColBuf> cols_buf(cols);
    std::vector<MYSQL_BIND> out_bind(cols);
    std::memset(out_bind.data(), 0, out_bind.size() * sizeof(MYSQL_BIND));
    for (unsigned i = 0; i < cols; ++i) {
        cols_buf[i].buf.assign(4096, '\0');
        out_bind[i].buffer_type = MYSQL_TYPE_STRING;
        out_bind[i].buffer = cols_buf[i].buf.data();
        out_bind[i].buffer_length = static_cast<unsigned long>(cols_buf[i].buf.size());
        out_bind[i].length = &cols_buf[i].len;
        out_bind[i].is_null = &cols_buf[i].is_null;
        out_bind[i].error = &cols_buf[i].error;
    }
    if (mysql_stmt_bind_result(stmt, out_bind.data()) != 0) {
        std::cerr << mysql_stmt_error(stmt) << std::endl;
        mysql_free_result(meta);
        mysql_stmt_close(stmt);
        return 0;
    }
    if (mysql_stmt_store_result(stmt) != 0) {
        std::cerr << mysql_stmt_error(stmt) << std::endl;
        mysql_free_result(meta);
        mysql_stmt_close(stmt);
        return 0;
    }
    rows -> clear();
    while (mysql_stmt_fetch(stmt) == 0) {
        std::vector<std::string> row;
        row.reserve(cols);
        for (unsigned i = 0; i < cols; ++i) {
            if (cols_buf[i].is_null) {
                row.emplace_back();
            } else {
                row.emplace_back(cols_buf[i].buf.data(), cols_buf[i].len);
            }
        }
        rows -> push_back(std::move(row));
    }
    mysql_free_result(meta);
    mysql_stmt_close(stmt);
    return 1;
}

MysqlConn::MysqlConn() : conn_(pool().acquire()) {}

MysqlConn::~MysqlConn() {
    pool().release(conn_);
    conn_ = nullptr;
}

int MysqlConn::ok() const {
    return conn_ != nullptr ? 1 : 0;
}

MYSQL* MysqlConn::get() {
    return conn_;
}

bool start_mysql(MYSQL& mysql) {
    if (mysql_init(&mysql) == nullptr) {
        return 0;
    }
    if (!login_mysql(&mysql)) {
        mysql_close(&mysql);
        return 0;
    }
    return 1;
}

}  // namespace nloj::common
