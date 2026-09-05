// 压测客户端：多线程打已启动的 nloj_api，输出一行 QPS / 延迟分位。
// 不进 CTest。用法见 scripts/bench.ps1。
#include "httplib.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct BenchOpt {
    std::string host = "127.0.0.1";
    int port = 8080;
    std::string path = "/api/v1/health";
    int threads = 8;
    int seconds = 5;
    int skip_cache = 0;
    int rotate_from = 0;
    int rotate_span = 0;
};

struct ThreadStat {
    std::int64_t ok = 0;
    std::int64_t err = 0;
    std::vector<std::int64_t> us;
};

int parse_int(const char* raw, int fallback) {
    if (raw == nullptr || raw[0] == '\0') {
        return fallback;
    }
    return std::atoi(raw);
}

int parse_args(int argc, char** argv, BenchOpt& opt) {
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--host" && i + 1 < argc) {
            opt.host = argv[++i];
        } else if (key == "--port" && i + 1 < argc) {
            opt.port = parse_int(argv[++i], opt.port);
        } else if (key == "--path" && i + 1 < argc) {
            opt.path = argv[++i];
        } else if (key == "--threads" && i + 1 < argc) {
            opt.threads = parse_int(argv[++i], opt.threads);
        } else if (key == "--seconds" && i + 1 < argc) {
            opt.seconds = parse_int(argv[++i], opt.seconds);
        } else if (key == "--skip-cache") {
            opt.skip_cache = 1;
        } else if (key == "--rotate-from" && i + 1 < argc) {
            opt.rotate_from = parse_int(argv[++i], 0);
        } else if (key == "--rotate-span" && i + 1 < argc) {
            opt.rotate_span = parse_int(argv[++i], 0);
        } else {
            std::cerr << "unknown arg: " << key << '\n';
            return 0;
        }
    }
    if (opt.threads < 1) {
        opt.threads = 1;
    }
    if (opt.seconds < 1) {
        opt.seconds = 1;
    }
    if (opt.port < 1) {
        return 0;
    }
    return 1;
}

std::string request_path(const BenchOpt& opt, std::int64_t seq) {
    if (opt.rotate_span <= 0) {
        return opt.path;
    }
    const int id = opt.rotate_from + static_cast<int>(seq % opt.rotate_span);
    return opt.path + "/" + std::to_string(id);
}

void worker_loop(const BenchOpt& opt,
                 const std::chrono::steady_clock::time_point deadline,
                 ThreadStat& st) {
    httplib::Client cli(opt.host, opt.port);
    cli.set_keep_alive(true);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(5, 0);

    httplib::Headers headers;
    if (opt.skip_cache) {
        headers.emplace("X-NLOJ-Skip-Cache", "1");
    }

    std::int64_t seq = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        const std::string path = request_path(opt, seq);
        ++seq;
        const auto t0 = std::chrono::steady_clock::now();
        httplib::Result res;
        if (opt.skip_cache) {
            res = cli.Get(path, headers);
        } else {
            res = cli.Get(path);
        }
        const auto t1 = std::chrono::steady_clock::now();
        const std::int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        if (res && res -> status == 200) {
            ++st.ok;
            if (st.us.size() < 200000) {
                st.us.push_back(us);
            }
        } else {
            ++st.err;
        }
    }
}

std::int64_t percentile_us(std::vector<std::int64_t>& samples, int p) {
    if (samples.empty()) {
        return 0;
    }
    const std::size_t idx = static_cast<std::size_t>(samples.size() - 1) * static_cast<std::size_t>(p)
                           / 100;
    return samples[idx];
}

double us_to_ms(std::int64_t us) {
    return static_cast<double>(us) / 1000.0;
}

}  // namespace

int main(int argc, char** argv) {
    BenchOpt opt;
    if (!parse_args(argc, argv, opt)) {
        std::cerr << "nloj_api_bench --host 127.0.0.1 --port 8080 --path /api/v1/problems/1 "
                     "--threads 8 --seconds 5 [--skip-cache] [--rotate-from N --rotate-span N]\n";
        return EXIT_FAILURE;
    }

    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::seconds(opt.seconds);
    std::vector<ThreadStat> stats(static_cast<std::size_t>(opt.threads));
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(opt.threads));
    for (int i = 0; i < opt.threads; ++i) {
        workers.emplace_back(worker_loop, opt, deadline, std::ref(stats[static_cast<std::size_t>(i)]));
    }
    for (auto& th : workers) {
        th.join();
    }
    const auto end = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(end - start).count();

    ThreadStat all;
    for (auto& one : stats) {
        all.ok += one.ok;
        all.err += one.err;
        all.us.insert(all.us.end(), one.us.begin(), one.us.end());
    }
    std::sort(all.us.begin(), all.us.end());
    const double qps = elapsed > 0.0 ? static_cast<double>(all.ok) / elapsed : 0.0;

    std::cout.setf(std::ios::fixed);
    std::cout.precision(3);
    std::cout << "ok=" << all.ok
              << " err=" << all.err
              << " qps=" << qps
              << " p50_ms=" << us_to_ms(percentile_us(all.us, 50))
              << " p95_ms=" << us_to_ms(percentile_us(all.us, 95))
              << " p99_ms=" << us_to_ms(percentile_us(all.us, 99))
              << '\n';
    return all.err > 0 && all.ok == 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
