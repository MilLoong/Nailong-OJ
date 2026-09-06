#include "nloj/judge/sandbox.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
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
constexpr int kOutputCapBytes = 256 * 1024;  // 单个用例输出读取上限

// 沙箱内逐用例计时/测内存用的 runner 源码（Linux，由沙箱内 g++ 编译）。
// 宿主侧不会执行它；Docker 与 Linux 本机降级都跑这份逻辑。
constexpr const char* kRunnerSource = R"SANDBOX(
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// 判题 runner：按顺序 fork ./main 并喂入 in_k.txt，wait4 记录每个用例的
// 真实耗时与 ru_maxrss（KB），每跑完一个用例输出一行：
//   case <k> verdict <OK|TLE|MLE|RE|SE> time_ms <t> mem_kb <m>
// 遇到首个非 OK 用例即停止，不再跑剩余用例。
// 说明：外部 SIGKILL（容器 OOM 等）按 MLE；超时由 runner 自己杀，按 TLE。

namespace {

long long now_ms() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        clock::now().time_since_epoch()).count();
}

int open_case(const char* kind, int k, int flags) {
    char path[64];
    std::snprintf(path, sizeof(path), "%s_%d.txt", kind, k);
    return open(path, flags, 0644);
}

void close3(int a, int b, int c) {
    if (a >= 0) {
        close(a);
    }
    if (b >= 0) {
        close(b);
    }
    if (c >= 0) {
        close(c);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: runner time_limit_ms case_count\n");
        return 2;
    }
    const int time_limit_ms = std::atoi(argv[1]);
    const int case_count = std::atoi(argv[2]);
    if (time_limit_ms <= 0 || case_count <= 0) {
        std::fprintf(stderr, "runner: bad args\n");
        return 2;
    }
    for (int k = 1; k <= case_count; ++k) {
        const int in_fd = open_case("in", k, O_RDONLY);
        const int out_fd = open_case("out", k, O_WRONLY | O_CREAT | O_TRUNC);
        const int err_fd = open_case("err", k, O_WRONLY | O_CREAT | O_TRUNC);
        if (in_fd < 0 || out_fd < 0 || err_fd < 0) {
            std::printf("case %d verdict SE time_ms 0 mem_kb -1\n", k);
            std::fflush(stdout);
            return 3;
        }
        const pid_t pid = fork();
        if (pid < 0) {
            close3(in_fd, out_fd, err_fd);
            std::printf("case %d verdict SE time_ms 0 mem_kb -1\n", k);
            std::fflush(stdout);
            return 3;
        }
        if (pid == 0) {
            dup2(in_fd, 0);
            dup2(out_fd, 1);
            dup2(err_fd, 2);
            close3(in_fd, out_fd, err_fd);
            execl("./main", "main", nullptr);  // 编译产物与 runner 同目录
            _exit(127);
        }
        close3(in_fd, out_fd, err_fd);

        const long long start_ms = now_ms();
        struct rusage ru;
        std::memset(&ru, 0, sizeof(ru));
        int status = 0;
        int killed = 0;  // 1=超时由 runner 杀 2=wait4 出错
        for (;;) {
            const pid_t w = wait4(pid, &status, WNOHANG, &ru);
            if (w == pid) {
                break;
            }
            if (w < 0) {
                killed = 2;
                break;
            }
            if (now_ms() - start_ms >= time_limit_ms) {
                kill(pid, SIGKILL);
                killed = 1;
                while (wait4(pid, &status, 0, &ru) != pid) {
                }
                break;
            }
            usleep(2000);
        }

        const int elapsed_ms = static_cast<int>(now_ms() - start_ms);
        const int mem_kb = ru.ru_maxrss > 0 ? static_cast<int>(ru.ru_maxrss) : -1;
        const char* verdict = "SE";
        if (killed == 1) {
            verdict = "TLE";
        } else if (killed == 2) {
            verdict = "SE";
        } else if (WIFEXITED(status)) {
            verdict = WEXITSTATUS(status) == 0 ? "OK" : "RE";
        } else if (WIFSIGNALED(status)) {
            verdict = WTERMSIG(status) == SIGKILL ? "MLE" : "RE";
        }
        std::printf("case %d verdict %s time_ms %d mem_kb %d\n", k, verdict, elapsed_ms, mem_kb);
        std::fflush(stdout);
        if (std::strcmp(verdict, "OK") != 0) {
            return 0;
        }
    }
    return 0;
}
)SANDBOX";

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
    // daemon 通且镜像已拉，否则退回本机 g++
#ifdef _WIN32
    const int daemon = std::system("docker version >nul 2>&1");
    if (daemon != 0) {
        return 0;
    }
    const int image = std::system("docker image inspect gcc:13-bookworm >nul 2>&1");
#else
    const int daemon = std::system("docker version >/dev/null 2>&1");
    if (daemon != 0) {
        return 0;
    }
    const int image = std::system("docker image inspect gcc:13-bookworm >/dev/null 2>&1");
#endif
    return (image == 0) ? 1 : 0;
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

// 单个用例的文件名，如 in_1.txt / out_1.txt。runner 与宿主侧共用这一命名。
std::string case_file(const char* kind, int k) {
    return std::string(kind) + "_" + std::to_string(k) + ".txt";
}

// 解析 runner 输出的一行：case k verdict V time_ms t mem_kb m。
int parse_runner_line(const std::string& line, SandboxCaseResult& out) {
    char verdict[16] = {0};
    int k = 0;
    int time_ms = -1;
    int mem_kb = -1;
    const int n = std::sscanf(line.c_str(), "case %d verdict %15s time_ms %d mem_kb %d",
                              &k, verdict, &time_ms, &mem_kb);
    if (n < 4 || k <= 0) {
        return 0;
    }
    out.index = k;
    out.verdict = verdict;
    out.time_used_ms = time_ms;
    out.memory_used_kb = mem_kb;
    return 1;
}

// 汇总 runner 输出：逐行解析，并回填 OK 用例的实际输出（宿主侧做 WA 比对）。
// runner 遇到首个非 OK 用例即停，因此结果个数可能小于用例总数。
int collect_runner_results(const std::filesystem::path& dir,
                           const std::string& runner_text,
                           int case_count,
                           std::vector<SandboxCaseResult>& out) {
    out.clear();
    std::istringstream in(runner_text);
    std::string line;
    int seen = 0;
    while (std::getline(in, line)) {
        SandboxCaseResult one;
        if (!parse_runner_line(line, one) || one.index != seen + 1) {
            continue;  // 非结果行或乱序，忽略
        }
        if (one.index > case_count) {
            break;
        }
        seen = one.index;
        if (one.verdict == "OK") {
            one.stdout_text = read_file_limited(dir / case_file("out", one.index), kOutputCapBytes);
        }
        out.push_back(std::move(one));
    }
    return static_cast<int>(out.size());
}

// Docker 沙箱：docker run --network=none + 内存/pids 限制。
class DockerJudgeSandbox : public JudgeSandbox {
public:
    DockerJudgeSandbox() : work_dir_(make_work_dir()), image_(sandbox_image()) {}

    ~DockerJudgeSandbox() override {
        std::error_code ec;
        std::filesystem::remove_all(work_dir_, ec);
    }

    SandboxJudgeResult judge(const SandboxJudgeRequest& request) override {
        // 语言校验 → 写 main/runner/输入 → 容器内编译 → 单容器跑完全部用例 → 汇总

        SandboxJudgeResult out;
        out.status = "SYSTEM_ERROR";
        out.error_text = "sandbox run failed";
        if (request.language != "CPP") {
            out.status = "CE";
            out.error_text = "unsupported language";
            return out;
        }
        if (request.code.empty() || request.inputs.empty() || request.time_limit_ms <= 0) {
            return out;
        }

        const std::filesystem::path root(work_dir_);
        const auto stdout_path = root / "sandbox_stdout.txt";
        const auto stderr_path = root / "sandbox_stderr.txt";
        const int case_count = static_cast<int>(request.inputs.size());
        if (!write_file(root / "main.cpp", request.code)
         || !write_file(root / "runner.cpp", kRunnerSource)) {
            return out;
        }
        for (std::size_t i = 0; i < request.inputs.size(); ++i) {
            const int k = static_cast<int>(i + 1);
            if (!write_file(root / case_file("in", k), request.inputs[i])) {
                return out;
            }
        }

        // 容器内编译：先编译 runner，再编译用户代码（main 编译失败即 CE）
        const int compile_mem_kb = request.memory_limit_kb > 524288 ? request.memory_limit_kb : 524288;
        const std::string compile_cmd = "docker run --rm --network=none --memory="
                                       + std::to_string(compile_mem_kb) + "k --memory-swap="
                                       + std::to_string(compile_mem_kb) + "k --pids-limit=64 --cpus=1 -v "
                                       + docker_bind(work_dir_, "/work")
                                       + " -w /work "
                                       + image_
                                       + " bash -c \"g++ -O2 -std=c++17 -o runner runner.cpp && g++ -O2 -std=c++17 -o main main.cpp\"";
        const CmdResult cr = run_cmd(compile_cmd, stdout_path, stderr_path, kCompileTimeoutMs, "");
        const std::string compile_err = read_file_limited(stderr_path, 4096);
        if (cr.timed_out) {
            out.error_text = "compile timeout";
            return out;
        }
        if (cr.exit_code != 0) {
            out.status = "CE";
            out.error_text = compile_err;
            return out;
        }

        // 单容器跑完全部用例：runner 在容器内逐用例计时并 wait4 记录 ru_maxrss
        const int mem_kb = request.memory_limit_kb > 0 ? request.memory_limit_kb : 262144;
        const std::string run_cmd_text = "docker run --rm --network=none --memory="
                                        + std::to_string(mem_kb) + "k --memory-swap="
                                        + std::to_string(mem_kb) + "k --pids-limit=64 --cpus=1 --read-only -v "
                                        + docker_bind(work_dir_, "/work")
                                        + " -w /work "
                                        + image_
                                        + " bash -c \"./runner "
                                        + std::to_string(request.time_limit_ms) + " " + std::to_string(case_count)
                                        + "\"";
        const int host_timeout = kDockerOverheadMs + request.time_limit_ms * case_count + 30000;
        const CmdResult rr = run_cmd(run_cmd_text, stdout_path, stderr_path, host_timeout, "");
        if (rr.timed_out) {
            out.error_text = "run timeout";
            return out;
        }
        const std::string runner_text = read_file_limited(stdout_path, 1024 * 1024);
        if (collect_runner_results(root, runner_text, case_count, out.cases) == 0) {
            if (rr.exit_code == 137) {
                // 容器整体 OOM（runner 都来不及输出），按 MLE 处理
                SandboxCaseResult mle;
                mle.index = 1;
                mle.verdict = "MLE";
                mle.time_used_ms = request.time_limit_ms;
                mle.memory_used_kb = -1;
                out.cases.push_back(std::move(mle));
            } else {
                out.error_text = read_file_limited(stderr_path, 4096);
                if (out.error_text.empty()) {
                    out.error_text = "sandbox run failed";
                }
                return out;
            }
        }
        out.status = "OK";
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

    SandboxJudgeResult judge(const SandboxJudgeRequest& request) override {
        // 语言校验 → 写 main/runner/输入 → 本机 g++ 编译 → 跑完全部用例 → 汇总
        // Windows 降级：无 getrusage 测不到内存，memory 记 -1，时间含少量进程开销

        SandboxJudgeResult out;
        out.status = "SYSTEM_ERROR";
        out.error_text = "sandbox run failed";
        if (request.language != "CPP") {
            out.status = "CE";
            out.error_text = "unsupported language";
            return out;
        }
        if (request.code.empty() || request.inputs.empty() || request.time_limit_ms <= 0) {
            return out;
        }

        const std::filesystem::path root(work_dir_);
        const auto stdout_path = root / "sandbox_stdout.txt";
        const auto stderr_path = root / "sandbox_stderr.txt";
        const int case_count = static_cast<int>(request.inputs.size());
        if (!write_file(root / "main.cpp", request.code)) {
            return out;
        }
#ifndef _WIN32
        if (!write_file(root / "runner.cpp", kRunnerSource)) {
            return out;
        }
#endif
        for (std::size_t i = 0; i < request.inputs.size(); ++i) {
            const int k = static_cast<int>(i + 1);
            if (!write_file(root / case_file("in", k), request.inputs[i])) {
                return out;
            }
        }

#ifdef _WIN32
        const std::string compile_cmd = "g++ -O2 -std=c++17 -o main.exe main.cpp";
#else
        const std::string compile_cmd = "g++ -O2 -std=c++17 -o main main.cpp";
#endif
        const CmdResult cr = run_cmd(compile_cmd, stdout_path, stderr_path, kCompileTimeoutMs, work_dir_);
        if (cr.timed_out) {
            out.error_text = "compile timeout";
            return out;
        }
        if (cr.exit_code != 0) {
            out.status = "CE";
            out.error_text = read_file_limited(stderr_path, 4096);
            return out;
        }

#ifndef _WIN32
        // Linux 本机降级复用 runner：与容器内同一套计时/内存逻辑（便于 CI 覆盖）
        const std::string runner_compile = "g++ -O2 -std=c++17 -o runner runner.cpp";
        const CmdResult rc = run_cmd(runner_compile, stdout_path, stderr_path, kCompileTimeoutMs, work_dir_);
        if (rc.exit_code != 0) {
            out.error_text = read_file_limited(stderr_path, 4096);
            return out;
        }
        const std::string run_cmd_text = "./runner "
                                        + std::to_string(request.time_limit_ms) + " " + std::to_string(case_count);
        const int host_timeout = request.time_limit_ms * case_count + 30000;
        const CmdResult rr = run_cmd(run_cmd_text, stdout_path, stderr_path, host_timeout, work_dir_);
        if (rr.timed_out) {
            out.error_text = "run timeout";
            return out;
        }
        const std::string runner_text = read_file_limited(stdout_path, 1024 * 1024);
        if (collect_runner_results(root, runner_text, case_count, out.cases) == 0) {
            out.error_text = read_file_limited(stderr_path, 4096);
            if (out.error_text.empty()) {
                out.error_text = "sandbox run failed";
            }
            return out;
        }
        out.status = "OK";
        return out;
#else
        // Windows：逐用例跑（无隔离演示，输出按用例落盘后回读）
        for (int k = 1; k <= case_count; ++k) {
            const std::string cmd = "cmd /c main.exe < in_" + std::to_string(k) + ".txt";
            const int host_timeout = request.time_limit_ms + 500;
            const CmdResult r = run_cmd(cmd, stdout_path, stderr_path, host_timeout, work_dir_);
            SandboxCaseResult one;
            one.index = k;
            one.time_used_ms = r.elapsed_ms;
            one.memory_used_kb = -1;
            if (r.timed_out) {
                one.verdict = "TLE";
                out.cases.push_back(std::move(one));
                break;
            }
            if (r.exit_code != 0) {
                one.verdict = "RE";
                out.cases.push_back(std::move(one));
                break;
            }
            one.verdict = "OK";
            one.stdout_text = read_file_limited(stdout_path, kOutputCapBytes);
            out.cases.push_back(std::move(one));
        }
        out.status = "OK";
        return out;
#endif
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
