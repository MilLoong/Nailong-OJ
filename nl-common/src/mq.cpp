#include "nloj/common/mq.h"

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#endif

#include <rabbitmq-c/amqp.h>
#include <rabbitmq-c/tcp_socket.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

namespace nloj::common {
namespace {

constexpr const char* kHost = "127.0.0.1";
constexpr int kPort = 5672;
constexpr const char* kUser = "nloj";
constexpr const char* kPass = "nloj123456";
constexpr const char* kVhost = "/";
constexpr const char* kExchange = "nloj.judge.exchange";
constexpr const char* kQueue = "nloj.judge.queue";
constexpr const char* kRouting = "nloj.judge.submit";
constexpr int kConnectTimeoutSec = 1;  // 连不上尽快降级，别卡死测试

std::mutex g_mu;
std::condition_variable g_cv;
std::queue<JudgeTaskMessage> g_queue;

int g_probed = 0;      // 是否已尝试连过 Broker
int g_use_rabbit = 0;  // 1=RabbitMQ，0=进程内
amqp_connection_state_t g_conn = nullptr;

// RPC 是否成功。
int rpc_ok(amqp_rpc_reply_t r) {
    return r.reply_type == AMQP_RESPONSE_NORMAL ? 1 : 0;
}

void log_rpc(const char* where, amqp_rpc_reply_t r) {
    if (rpc_ok(r)) {
        return;
    }
    std::cerr << where;
    if (r.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION) {
        std::cerr << ": " << amqp_error_string2(r.library_error);
    }
    std::cerr << std::endl;
}

// opened=1 表示 TCP/登录已成功，先关 channel/connection 再销毁。
void drop_conn(int opened) {
    if (g_conn == nullptr) {
        return;
    }
    if (opened) {
        amqp_channel_close(g_conn, 1, AMQP_REPLY_SUCCESS);
        amqp_connection_close(g_conn, AMQP_REPLY_SUCCESS);
    }
    amqp_destroy_connection(g_conn);
    g_conn = nullptr;
}

std::string encode_json(const JudgeTaskMessage& msg) {
    const std::string json = "{\"submissionId\":"
                            + std::to_string(msg.submission_id)
                            + ",\"problemId\":"
                            + std::to_string(msg.problem_id)
                            + ",\"language\":\""
                            + msg.language
                            + "\"}";
    return json;
}

// 跳过空白，返回新下标。
size_t skip_ws(const std::string& s, size_t i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) {
        ++i;
    }
    return i;
}

int parse_int_field(const std::string& json, const char* key, std::int64_t& out) {
    const std::string pat = std::string("\"") + key + "\":";
    const auto pos = json.find(pat);
    if (pos == std::string::npos) {
        return 0;
    }
    size_t i = skip_ws(json, pos + pat.size());
    try {
        size_t n = 0;
        out = std::stoll(json.substr(i), &n);
        if (n == 0) {
            return 0;
        }
    } catch (...) {
        return 0;
    }
    return 1;
}

int parse_string_field(const std::string& json, const char* key, std::string& out) {
    const std::string pat = std::string("\"") + key + "\":";
    const auto pos = json.find(pat);
    if (pos == std::string::npos) {
        return 0;
    }
    size_t i = skip_ws(json, pos + pat.size());
    if (i >= json.size() || json[i] != '"') {
        return 0;
    }
    ++i;
    const auto end = json.find('"', i);
    if (end == std::string::npos) {
        return 0;
    }
    out = json.substr(i, end - i);
    return out.empty() ? 0 : 1;
}

int parse_json(const std::string& json, JudgeTaskMessage& out) {
    JudgeTaskMessage tmp;
    if (!parse_int_field(json, "submissionId", tmp.submission_id)) {
        return 0;
    }
    if (!parse_int_field(json, "problemId", tmp.problem_id)) {
        return 0;
    }
    if (!parse_string_field(json, "language", tmp.language)) {
        return 0;
    }
    if (tmp.submission_id <= 0 || tmp.problem_id <= 0) {
        return 0;
    }
    out = std::move(tmp);
    return 1;
}

// 声明 exchange / queue / bind。失败返回 0。
int declare_topology() {
    amqp_exchange_declare(
        g_conn, 1, amqp_cstring_bytes(kExchange), amqp_cstring_bytes("direct"),
        0, 1, 0, 0, amqp_empty_table
    );
    amqp_rpc_reply_t r = amqp_get_rpc_reply(g_conn);
    if (!rpc_ok(r)) {
        log_rpc("amqp exchange.declare", r);
        return 0;
    }

    amqp_queue_declare(
        g_conn, 1, amqp_cstring_bytes(kQueue),
        0, 1, 0, 0, amqp_empty_table
    );
    r = amqp_get_rpc_reply(g_conn);
    if (!rpc_ok(r)) {
        log_rpc("amqp queue.declare", r);
        return 0;
    }

    amqp_queue_bind(
        g_conn, 1, amqp_cstring_bytes(kQueue), amqp_cstring_bytes(kExchange),
        amqp_cstring_bytes(kRouting), amqp_empty_table
    );
    r = amqp_get_rpc_reply(g_conn);
    if (!rpc_ok(r)) {
        log_rpc("amqp queue.bind", r);
        return 0;
    }
    return 1;
}

// 5672 是否在听。连不上立刻降级，避免 Windows 上 noblock 仍要等很久。
int broker_reachable() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return 0;
    }
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        return 0;
    }
    u_long nonblock = 1;
    ioctlsocket(sock, FIONBIO, &nonblock);

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(kPort));
    inet_pton(AF_INET, kHost, &addr.sin_addr);
    connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    fd_set wset;
    FD_ZERO(&wset);
    FD_SET(sock, &wset);
    timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 200000;
    const int sel = select(0, nullptr, &wset, nullptr, &tv);
    int ok = 0;
    if (sel > 0) {
        int err = 0;
        int err_len = sizeof(err);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &err_len);
        if (err == 0) {
            ok = 1;
        }
    }
    closesocket(sock);
    return ok;
#else
    return 1;
#endif
}

// 连 Broker 并声明拓扑。调用方已持锁。
int try_connect_rabbit() {
    if (!broker_reachable()) {
        return 0;
    }
    g_conn = amqp_new_connection();
    amqp_socket_t* socket = amqp_tcp_socket_new(g_conn);
    if (socket == nullptr) {
        drop_conn(0);
        return 0;
    }

    struct timeval timeout;
    timeout.tv_sec = kConnectTimeoutSec;
    timeout.tv_usec = 0;
    if (amqp_socket_open_noblock(socket, kHost, kPort, &timeout) != AMQP_STATUS_OK) {
        drop_conn(0);
        return 0;
    }

    amqp_rpc_reply_t login = amqp_login(
        g_conn, kVhost, 0, 131072, 0, AMQP_SASL_METHOD_PLAIN, kUser, kPass
    );
    if (!rpc_ok(login)) {
        log_rpc("amqp login", login);
        drop_conn(1);
        return 0;
    }

    amqp_channel_open(g_conn, 1);
    amqp_rpc_reply_t ch = amqp_get_rpc_reply(g_conn);
    if (!rpc_ok(ch)) {
        log_rpc("amqp channel.open", ch);
        drop_conn(1);
        return 0;
    }

    if (!declare_topology()) {
        drop_conn(1);
        return 0;
    }
    return 1;
}

void ensure_backend() {
    if (g_probed) {
        return;
    }
    g_probed = 1;
    if (try_connect_rabbit()) {
        g_use_rabbit = 1;
        return;
    }
    g_use_rabbit = 0;
    std::cout << "RabbitMQ unavailable, fallback to in-process queue" << std::endl;
}

void fallback_inprocess() {
    drop_conn(1);
    g_use_rabbit = 0;
}

// 调用方已持锁。有消息返回 1。
int rabbit_try_pop(JudgeTaskMessage& out) {
    amqp_rpc_reply_t got = amqp_basic_get(
        g_conn, 1, amqp_cstring_bytes(kQueue), 0
    );
    if (!rpc_ok(got)) {
        log_rpc("amqp basic.get", got);
        fallback_inprocess();
        return 0;
    }
    if (got.reply.id == AMQP_BASIC_GET_EMPTY_METHOD) {
        amqp_maybe_release_buffers(g_conn);
        return 0;
    }
    if (got.reply.id != AMQP_BASIC_GET_OK_METHOD || got.reply.decoded == nullptr) {
        log_rpc("amqp basic.get unexpected", got);
        fallback_inprocess();
        return 0;
    }

    const auto* ok = static_cast<amqp_basic_get_ok_t*>(got.reply.decoded);
    const std::uint64_t delivery_tag = ok -> delivery_tag;

    amqp_message_t message;
    std::memset(&message, 0, sizeof(message));
    amqp_rpc_reply_t body = amqp_read_message(
        g_conn, 1, &message, 0
    );
    if (!rpc_ok(body)) {
        log_rpc("amqp read_message", body);
        fallback_inprocess();
        return 0;
    }

    std::string json;
    if (message.body.bytes != nullptr && message.body.len > 0) {
        json.assign(
            static_cast<const char*>(message.body.bytes),
            message.body.len
        );
    }
    amqp_destroy_message(&message);

    amqp_basic_ack(
        g_conn, 1, delivery_tag, 0
    );
    amqp_maybe_release_buffers(g_conn);

    if (!parse_json(json, out)) {
        return 0;  // 毒消息已 ACK，当作没取到
    }
    return 1;
}

int rabbit_publish(const JudgeTaskMessage& msg) {
    const std::string json = encode_json(msg);
    amqp_bytes_t body;
    body.len = json.size();
    body.bytes = const_cast<char*>(json.data());

    amqp_basic_properties_t props;
    std::memset(&props, 0, sizeof(props));
    props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
    props.content_type = amqp_cstring_bytes("application/json");
    props.delivery_mode = 2;  // persistent

    const int pub = amqp_basic_publish(
        g_conn, 1, amqp_cstring_bytes(kExchange), amqp_cstring_bytes(kRouting),
        0, 0, &props, body
    );
    if (pub != AMQP_STATUS_OK) {
        std::cerr << "amqp basic.publish: " << amqp_error_string2(pub) << std::endl;
        fallback_inprocess();
        return 0;
    }
    return 1;
}

}  // namespace

bool publish_judge_task(const JudgeTaskMessage& msg) {
    // 校验字段 → 探测 Broker → basic.publish 或入进程内队列

    if (msg.submission_id <= 0 || msg.problem_id <= 0 || msg.language.empty()) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_mu);
    ensure_backend();
    if (g_use_rabbit) {
        if (rabbit_publish(msg)) {
            return 1;
        }
        // publish 失败已降级，这条改走进程内，保证提交链路不丢
    }
    g_queue.push(msg);
    g_cv.notify_one();  // 唤醒可能在 wait_pop 上阻塞的消费者
    return 1;
}

bool try_pop_judge_task(JudgeTaskMessage& out) {
    // 探测 Broker → basic.get 或从进程内队列弹出

    std::lock_guard<std::mutex> lock(g_mu);
    ensure_backend();
    if (g_use_rabbit) {
        return rabbit_try_pop(out) ? 1 : 0;
    }
    if (g_queue.empty()) {
        return 0;
    }
    out = std::move(g_queue.front());
    g_queue.pop();
    return 1;
}

void wait_pop_judge_task(JudgeTaskMessage& out) {
    // RabbitMQ：轮询 basic.get；进程内：condition_variable 等待

    while (1) {
        std::unique_lock<std::mutex> lock(g_mu);
        ensure_backend();
        if (g_use_rabbit) {
            if (rabbit_try_pop(out)) {
                return;
            }
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        // unique_lock 才能交给 condition_variable：wait 时会临时解锁，被唤醒后再加锁
        g_cv.wait(lock, [] {
            return !g_queue.empty();
        });
        out = std::move(g_queue.front());
        g_queue.pop();
        return;
    }
}

bool mq_using_rabbit() {
    std::lock_guard<std::mutex> lock(g_mu);
    ensure_backend();
    return g_use_rabbit ? 1 : 0;
}

}  // namespace nloj::common
