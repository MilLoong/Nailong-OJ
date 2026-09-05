#include "nloj/common/redis.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace nloj::common {
namespace {

constexpr const char* kHost = "127.0.0.1";
constexpr int kPort = 6379;

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kInvalid = INVALID_SOCKET;
#else
using Socket = int;
constexpr Socket kInvalid = -1;
#endif

std::mutex g_mu;
int g_probed = 0;
int g_use_redis = 0;
Socket g_sock = kInvalid;

void close_sock() {
    if (g_sock == kInvalid) {
        return;
    }
#ifdef _WIN32
    closesocket(g_sock);
#else
    close(g_sock);
#endif
    g_sock = kInvalid;
}

int send_all(const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
#ifdef _WIN32
        const int n = send(
            g_sock, data.data() + sent, static_cast<int>(data.size() - sent), 0
        );
#else
        const int n = static_cast<int>(send(
            g_sock, data.data() + sent, data.size() - sent, 0
        ));
#endif
        if (n <= 0) {
            return 0;
        }
        sent += static_cast<std::size_t>(n);
    }
    return 1;
}

int recv_n(char* buf, int n) {
    int got = 0;
    while (got < n) {
#ifdef _WIN32
        const int r = recv(g_sock, buf + got, n - got, 0);
#else
        const int r = static_cast<int>(recv(g_sock, buf + got, n - got, 0));
#endif
        if (r <= 0) {
            return 0;
        }
        got += r;
    }
    return 1;
}

int recv_line(std::string& out) {
    out.clear();
    char ch = 0;
    for (;;) {
        if (!recv_n(&ch, 1)) {
            return 0;
        }
        if (ch == '\r') {
            if (!recv_n(&ch, 1) || ch != '\n') {
                return 0;
            }
            return 1;
        }
        out.push_back(ch);
        if (out.size() > 16 * 1024 * 1024) {
            return 0;
        }
    }
}

std::string encode_array(const std::vector<std::string>& args) {
    std::ostringstream o;
    o << '*' << args.size() << "\r\n";
    for (const auto& a : args) {
        o << '$' << a.size() << "\r\n" << a << "\r\n";
    }
    return o.str();
}

struct Reply {
    int ok;             // 协议读成功
    int is_null;        // bulk/nil
    int is_status_ok;   // +OK
    std::int64_t num;
    std::string bulk;
    std::vector<std::string> elems;  // 数组（KEYS）
};

int read_reply(Reply& out) {
    out.ok = 0;
    out.is_null = 0;
    out.is_status_ok = 0;
    out.num = 0;
    out.bulk.clear();
    out.elems.clear();

    std::string line;
    if (!recv_line(line) || line.empty()) {
        return 0;
    }
    const char kind = line[0];
    const std::string rest = line.substr(1);
    if (kind == '+') {
        out.ok = 1;
        out.is_status_ok = (rest == "OK") ? 1 : 0;
        return 1;
    }
    if (kind == '-') {
        return 0;
    }
    if (kind == ':') {
        out.ok = 1;
        out.num = std::stoll(rest);
        return 1;
    }
    if (kind == '$') {
        const int len = std::stoi(rest);
        if (len < 0) {
            out.ok = 1;
            out.is_null = 1;
            return 1;
        }
        std::string body(static_cast<std::size_t>(len), '\0');
        if (len > 0 && !recv_n(body.data(), len)) {
            return 0;
        }
        char crlf[2];
        if (!recv_n(crlf, 2) || crlf[0] != '\r' || crlf[1] != '\n') {
            return 0;
        }
        out.ok = 1;
        out.bulk = std::move(body);
        return 1;
    }
    if (kind == '*') {
        const int n = std::stoi(rest);
        if (n < 0) {
            out.ok = 1;
            out.is_null = 1;
            return 1;
        }
        for (int i = 0; i < n; ++i) {
            Reply one;
            if (!read_reply(one)) {
                return 0;
            }
            if (!one.is_null && !one.bulk.empty()) {
                out.elems.push_back(one.bulk);
            }
        }
        out.ok = 1;
        return 1;
    }
    return 0;
}

int try_connect() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return 0;
    }
#endif
    Socket sock;
#ifdef _WIN32
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        return 0;
    }
    u_long nonblock = 1;
    ioctlsocket(sock, FIONBIO, &nonblock);
#else
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return 0;
    }
#endif

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(kPort));
    inet_pton(AF_INET, kHost, &addr.sin_addr);
    connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

#ifdef _WIN32
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
    if (!ok) {
        closesocket(sock);
        return 0;
    }
    nonblock = 0;
    ioctlsocket(sock, FIONBIO, &nonblock);
#else
    (void)0;
#endif
    g_sock = sock;
    return 1;
}

void ensure_backend() {
    if (g_probed) {
        return;
    }
    g_probed = 1;
    if (try_connect()) {
        g_use_redis = 1;
        return;
    }
    g_use_redis = 0;
    std::cout << "Redis unavailable, problem cache disabled" << std::endl;
}

int exec(const std::vector<std::string>& args, Reply& reply) {
    ensure_backend();
    if (!g_use_redis || g_sock == kInvalid) {
        return 0;
    }
    if (!send_all(encode_array(args)) || !read_reply(reply) || !reply.ok) {
        g_use_redis = 0;
        close_sock();
        return 0;
    }
    return 1;
}

}  // namespace

int redis_using() {
    std::lock_guard<std::mutex> lock(g_mu);
    ensure_backend();
    return g_use_redis;
}

int redis_get(const std::string& key, std::string& out) {
    if (key.empty()) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_mu);
    Reply reply;
    if (!exec({"GET", key}, reply)) {
        return 0;
    }
    if (reply.is_null) {
        return 0;
    }
    out = reply.bulk;
    return 1;
}

int redis_set_ex(const std::string& key, const std::string& value, int ttl_sec) {
    if (key.empty() || ttl_sec <= 0) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_mu);
    Reply reply;
    if (!exec({"SET", key, value, "EX", std::to_string(ttl_sec)}, reply)) {
        return 0;
    }
    return reply.is_status_ok;
}

int redis_set_nx_ex(const std::string& key, const std::string& value, int ttl_sec) {
    if (key.empty() || ttl_sec <= 0) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_mu);
    Reply reply;
    if (!exec({"SET", key, value, "EX", std::to_string(ttl_sec), "NX"}, reply)) {
        return 0;
    }
    if (reply.is_null) {
        return 0;
    }
    return reply.is_status_ok;
}

int redis_del(const std::string& key) {
    if (key.empty()) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_mu);
    Reply reply;
    if (!exec({"DEL", key}, reply)) {
        return 0;
    }
    return 1;
}

int redis_keys(const std::string& pattern, std::vector<std::string>& out) {
    if (pattern.empty()) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_mu);
    Reply reply;
    if (!exec({"KEYS", pattern}, reply)) {
        return 0;
    }
    out = reply.elems;
    return 1;
}

}  // namespace nloj::common
