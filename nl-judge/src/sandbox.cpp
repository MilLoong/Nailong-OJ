#include "nloj/judge/sandbox.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace nloj::judge {
namespace {

constexpr int kCompileTimeoutMs = 120000;  // 含首次拉镜像
constexpr int kDockerOverheadMs = 60000;

struct CmdResult {
    int exit_code;
    int timed_out;  // 1 超时被杀
    int elapsed_ms;
};

std::string read_file_limited(const std::filesystem::path& path, std::size_t max_n) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return "";
    }
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() > max_n) {
        data.resize(max_n);
    }
    return data;
}

int write_file(const std::filesystem::path& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return 0;
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return out.good() ? 1 : 0;
}

#ifdef _WIN32

CmdResult run_cmd(const std::string& cmd,
                  const std::filesystem::path& stdout_path,
                  const std::filesystem::path& stderr_path,
                  int timeout_ms,
                  const std::string& cwd) {
    CmdResult result;
    result.exit_code = -1;
    result.timed_out = 0;
    result.elapsed_ms = 0;

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle = TRUE;

    HANDLE out_handle = CreateFileA(
        stdout_path.string().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    HANDLE err_handle = CreateFileA(
        stderr_path.string().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (out_handle == INVALID_HANDLE_VALUE || err_handle == INVALID_HANDLE_VALUE) {
        if (out_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(out_handle);
        }
        if (err_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(err_handle);
        }
        return result;
    }

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = out_handle;
    si.hStdError = err_handle;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    std::vector<char> cmdline(cmd.begin(), cmd.end());
    cmdline.push_back('\0');

    const char* cwd_arg = cwd.empty() ? nullptr : cwd.c_str();
    const auto t0 = std::chrono::steady_clock::now();
    const BOOL ok = CreateProcessA(
        nullptr, cmdline.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, cwd_arg, &si, &pi
    );
    CloseHandle(out_handle);
    CloseHandle(err_handle);
    if (!ok) {
        return result;
    }

    const DWORD wait = WaitForSingleObject(pi.hProcess, static_cast<DWORD>(timeout_ms));
    const auto t1 = std::chrono::steady_clock::now();
    result.elapsed_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()
    );
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
        result.timed_out = 1;
        result.exit_code = -1;
    } else {
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        result.exit_code = static_cast<int>(code);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return result;
}

#else

CmdResult run_cmd(const std::string& cmd,
                  const std::filesystem::path& stdout_path,
                  const std::filesystem::path& stderr_path,
                  int timeout_ms,
                  const std::string& cwd) {
    CmdResult result;
    result.exit_code = -1;
    result.timed_out = 0;
    result.elapsed_ms = 0;
    const pid_t pid = fork();
    if (pid < 0) {
        return result;
    }
    if (pid == 0) {
        if (!cwd.empty()) {
            if (chdir(cwd.c_str()) != 0) {
                _exit(127);
            }
        }
        const int out_fd = open(stdout_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        const int err_fd = open(stderr_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd >= 0) {
            dup2(out_fd, STDOUT_FILENO);
            close(out_fd);
        }
        if (err_fd >= 0) {
            dup2(err_fd, STDERR_FILENO);
            close(err_fd);
        }
        execl("/bin/sh", "sh", "-c", cmd.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    const auto t0 = std::chrono::steady_clock::now();
    int elapsed = 0;
    int status = 0;
    while (elapsed < timeout_ms) {
        const pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) {
            break;
        }
        usleep(20000);
        elapsed += 20;
    }
    const auto t1 = std::chrono::steady_clock::now();
    result.elapsed_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()
    );
    if (elapsed >= timeout_ms) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        result.timed_out = 1;
        result.exit_code = -1;
        return result;
    }
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else {
        result.exit_code = 1;
    }
    return result;
}

#endif

int docker_available() {
    // 只关心能不能连上 daemon，输出丢掉即可
#ifdef _WIN32
    const int code = std::system("docker version >nul 2>&1");
#else
    const int code = std::system("docker version >/dev/null 2>&1");
#endif
    return (code == 0) ? 1 : 0;
}

std::string sandbox_image() {
    return "gcc:13-bookworm";
}

std::string quote_path(const std::string& p) {
    return "\"" + p + "\"";
}

std::string docker_bind(const std::string& host_dir, const std::string& container_dir) {
    std::string p = host_dir;
    for (char& c : p) {
        if (c == '\\') {
            c = '/';
        }
    }
    return quote_path(p + ":" + container_dir);
}

std::string make_work_dir() {
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir = std::filesystem::temp_directory_path()
                     / ("nloj_judge_" + std::to_string(tick));
    std::filesystem::create_directories(dir);
    return dir.string();
}

std::string rtrim_copy(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.pop_back();
    }
    return s;
}

// Docker 沙箱：docker run --network=none + 内存/pids 限制。
class DockerJudgeSandbox : public JudgeSandbox {
public:
    DockerJudgeSandbox() : work_dir_(make_work_dir()), image_(sandbox_image()) {}

    ~DockerJudgeSandbox() override {
        std::error_code ec;
        std::filesystem::remove_all(work_dir_, ec);
    }

    SandboxResult execute(const SandboxRequest& request) override {
        SandboxResult out;
        out.verdict = "SYSTEM_ERROR";
        out.time_used_ms = 0;
        out.memory_used_kb = -1;
        if (request.language != "CPP") {
            out.verdict = "CE";
            out.stderr_text = "unsupported language";
            return out;
        }

        const std::filesystem::path root(work_dir_);
        const auto stdout_path = root / "sandbox_stdout.txt";
        const auto stderr_path = root / "sandbox_stderr.txt";

        if (request.compile_only) {
            if (!write_file(root / "main.cpp", request.code)) {
                out.stderr_text = "write main.cpp failed";
                return out;
            }
            const int mem_kb = request.memory_limit_kb > 524288 ? request.memory_limit_kb : 524288;
            const std::string cmd = "docker run --rm --network=none --memory="
                                   + std::to_string(mem_kb) + "k --memory-swap="
                                   + std::to_string(mem_kb) + "k --pids-limit=64 --cpus=1 -v "
                                   + docker_bind(work_dir_, "/work")
                                   + " -w /work "
                                   + image_
                                   + " g++ -O2 -std=c++17 -o main main.cpp";
            const CmdResult r = run_cmd(cmd, stdout_path, stderr_path, kCompileTimeoutMs, "");
            out.stderr_text = read_file_limited(stderr_path, 4096);
            out.stdout_text = read_file_limited(stdout_path, 4096);
            out.time_used_ms = r.elapsed_ms;
            if (r.timed_out) {
                out.verdict = "SYSTEM_ERROR";
                out.stderr_text = "compile timeout";
                return out;
            }
            if (r.exit_code != 0) {
                out.verdict = "CE";
                return out;
            }
            out.verdict = "OK";
            return out;
        }

        if (!write_file(root / "input.txt", request.stdin_data)) {
            out.stderr_text = "write input.txt failed";
            return out;
        }
        const int sec = request.time_limit_ms <= 0 ? 1 : (request.time_limit_ms + 999) / 1000;
        const int mem_kb = request.memory_limit_kb > 0 ? request.memory_limit_kb : 262144;
        const std::string cmd = "docker run --rm --network=none --memory="
                               + std::to_string(mem_kb) + "k --memory-swap="
                               + std::to_string(mem_kb) + "k --pids-limit=64 --cpus=1 --read-only -v "
                               + docker_bind(work_dir_, "/work")
                               + " -w /work "
                               + image_
                               + " bash -c \"timeout --signal=KILL "
                               + std::to_string(sec)
                               + "s ./main < /work/input.txt\"";
        const int host_timeout = request.time_limit_ms + kDockerOverheadMs;
        const CmdResult r = run_cmd(cmd, stdout_path, stderr_path, host_timeout, "");
        out.stdout_text = read_file_limited(stdout_path, 256 * 1024);
        out.stderr_text = read_file_limited(stderr_path, 4096);
        out.time_used_ms = r.elapsed_ms;
        if (r.timed_out || r.exit_code == 124) {
            out.verdict = "TLE";
            return out;
        }
        if (r.exit_code == 137) {
            out.verdict = "MLE";
            return out;
        }
        if (r.exit_code != 0) {
            out.verdict = "RE";
            return out;
        }
        out.verdict = "OK";
        return out;
    }

private:
    std::string work_dir_;
    std::string image_;
};

// 本机 g++（无 cgroup 隔离，Docker 不可用时的演示降级）。
class LocalProcessSandbox : public JudgeSandbox {
public:
    LocalProcessSandbox() : work_dir_(make_work_dir()) {}

    ~LocalProcessSandbox() override {
        std::error_code ec;
        std::filesystem::remove_all(work_dir_, ec);
    }

    SandboxResult execute(const SandboxRequest& request) override {
        SandboxResult out;
        out.verdict = "SYSTEM_ERROR";
        out.time_used_ms = 0;
        out.memory_used_kb = -1;
        if (request.language != "CPP") {
            out.verdict = "CE";
            out.stderr_text = "unsupported language";
            return out;
        }
        const std::filesystem::path root(work_dir_);
        const auto stdout_path = root / "sandbox_stdout.txt";
        const auto stderr_path = root / "sandbox_stderr.txt";

        if (request.compile_only) {
            if (!write_file(root / "main.cpp", request.code)) {
                out.stderr_text = "write main.cpp failed";
                return out;
            }
#ifdef _WIN32
            const std::string cmd = "g++ -O2 -std=c++17 -o main.exe main.cpp";
#else
            const std::string cmd = "g++ -O2 -std=c++17 -o main main.cpp";
#endif
            const CmdResult r = run_cmd(cmd, stdout_path, stderr_path, kCompileTimeoutMs, work_dir_);
            out.stderr_text = read_file_limited(stderr_path, 4096);
            out.time_used_ms = r.elapsed_ms;
            if (r.timed_out || r.exit_code != 0) {
                out.verdict = r.timed_out ? "SYSTEM_ERROR" : "CE";
                return out;
            }
            out.verdict = "OK";
            return out;
        }

        if (!write_file(root / "input.txt", request.stdin_data)) {
            out.stderr_text = "write input.txt failed";
            return out;
        }
#ifdef _WIN32
        const std::string cmd = "cmd /c main.exe < input.txt";
#else
        const std::string cmd = "./main < input.txt";
#endif
        const int host_timeout = request.time_limit_ms > 0 ? request.time_limit_ms + 500 : 1500;
        const CmdResult r = run_cmd(cmd, stdout_path, stderr_path, host_timeout, work_dir_);
        out.stdout_text = read_file_limited(stdout_path, 256 * 1024);
        out.stderr_text = read_file_limited(stderr_path, 4096);
        out.time_used_ms = r.elapsed_ms;
        if (r.timed_out) {
            out.verdict = "TLE";
            return out;
        }
        if (r.exit_code != 0) {
            out.verdict = "RE";
            return out;
        }
        out.verdict = "OK";
        return out;
    }

private:
    std::string work_dir_;
};

}  // namespace

int judge_outputs_match(const std::string& expected, const std::string& actual) {
    auto normalize = [](std::string s) {
        std::string t;
        t.reserve(s.size());
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\r') {
                continue;
            }
            t.push_back(s[i]);
        }
        std::istringstream in(t);
        std::string line;
        std::string out;
        while (std::getline(in, line)) {
            line = rtrim_copy(line);
            out += line;
            out += '\n';
        }
        while (!out.empty() && out.back() == '\n') {
            out.pop_back();
        }
        return out;
    };
    return normalize(expected) == normalize(actual) ? 1 : 0;
}

std::unique_ptr<JudgeSandbox> make_sandbox() {
    if (docker_available()) {
        return std::unique_ptr<JudgeSandbox>(new DockerJudgeSandbox());
    }
    return std::unique_ptr<JudgeSandbox>(new LocalProcessSandbox());
}

}  // namespace nloj::judge
