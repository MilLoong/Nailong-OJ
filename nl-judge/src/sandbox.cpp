#include "nloj/judge/sandbox.h"
#include "nloj/judge/language.h"

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
constexpr int kSpjTimeoutMs = 10000;

// 沙箱内逐用例计时/测内存用的 runner 源码（Linux，由沙箱内 g++ 编译）。
// 宿主侧不会执行它；Docker 与 Linux 本机降级都跑这份逻辑。
// 用法：
//   runner std TIME CASES PROG [ARGS...]
//   runner interactive TIME CASES ./main ./interactor
//   runner comm TIME CASES ./alice ./bob ./manager
constexpr const char* kRunnerSource = R"SANDBOX(
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

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

void kill_wait(pid_t pid) {
    if (pid > 0) {
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
    }
}

int mem_from_ru(const struct rusage& ru) {
    return ru.ru_maxrss > 0 ? static_cast<int>(ru.ru_maxrss) : -1;
}

void emit(int k, const char* verdict, int time_ms, int mem_kb) {
    std::printf("case %d verdict %s time_ms %d mem_kb %d\n", k, verdict, time_ms, mem_kb);
    std::fflush(stdout);
}

int run_std(int k, int time_limit_ms, char** prog) {
    const int in_fd = open_case("in", k, O_RDONLY);
    const int out_fd = open_case("out", k, O_WRONLY | O_CREAT | O_TRUNC);
    const int err_fd = open_case("err", k, O_WRONLY | O_CREAT | O_TRUNC);
    if (in_fd < 0 || out_fd < 0 || err_fd < 0) {
        if (in_fd >= 0) {
            close(in_fd);
        }
        if (out_fd >= 0) {
            close(out_fd);
        }
        if (err_fd >= 0) {
            close(err_fd);
        }
        emit(k, "SE", 0, -1);
        return 0;
    }
    const pid_t pid = fork();
    if (pid < 0) {
        close(in_fd);
        close(out_fd);
        close(err_fd);
        emit(k, "SE", 0, -1);
        return 0;
    }
    if (pid == 0) {
        dup2(in_fd, 0);
        dup2(out_fd, 1);
        dup2(err_fd, 2);
        close(in_fd);
        close(out_fd);
        close(err_fd);
        execvp(prog[0], prog);
        _exit(127);
    }
    close(in_fd);
    close(out_fd);
    close(err_fd);

    const long long start_ms = now_ms();
    struct rusage ru;
    std::memset(&ru, 0, sizeof(ru));
    int status = 0;
    int killed = 0;
    while (now_ms() - start_ms < time_limit_ms) {
        const pid_t w = wait4(pid, &status, WNOHANG, &ru);
        if (w == pid) {
            break;
        }
        if (w < 0) {
            killed = 2;
            break;
        }
        usleep(2000);
    }
    if (now_ms() - start_ms >= time_limit_ms && killed == 0) {
        kill(pid, SIGKILL);
        wait4(pid, &status, 0, &ru);
        killed = 1;
    }
    const int time_ms = static_cast<int>(now_ms() - start_ms);
    const int mem_kb = mem_from_ru(ru);
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
    emit(k, verdict, time_ms, mem_kb);
    return std::strcmp(verdict, "OK") == 0 ? 1 : 0;
}

int run_interactive(int k, int time_limit_ms, char* sol, char* inter) {
    int to_sol[2];
    int to_inter[2];
    if (pipe(to_sol) != 0 || pipe(to_inter) != 0) {
        emit(k, "SE", 0, -1);
        return 0;
    }
    const int err_fd = open_case("err", k, O_WRONLY | O_CREAT | O_TRUNC);
    char inpath[64];
    std::snprintf(inpath, sizeof(inpath), "in_%d.txt", k);
    const pid_t sol_pid = fork();
    if (sol_pid < 0) {
        emit(k, "SE", 0, -1);
        return 0;
    }
    if (sol_pid == 0) {
        dup2(to_sol[0], 0);
        dup2(to_inter[1], 1);
        if (err_fd >= 0) {
            dup2(err_fd, 2);
        }
        close(to_sol[0]);
        close(to_sol[1]);
        close(to_inter[0]);
        close(to_inter[1]);
        if (err_fd >= 0) {
            close(err_fd);
        }
        execl(sol, sol, static_cast<char*>(nullptr));
        _exit(127);
    }
    const pid_t inter_pid = fork();
    if (inter_pid < 0) {
        kill_wait(sol_pid);
        emit(k, "SE", 0, -1);
        return 0;
    }
    if (inter_pid == 0) {
        dup2(to_inter[0], 0);
        dup2(to_sol[1], 1);
        if (err_fd >= 0) {
            dup2(err_fd, 2);
        }
        close(to_sol[0]);
        close(to_sol[1]);
        close(to_inter[0]);
        close(to_inter[1]);
        if (err_fd >= 0) {
            close(err_fd);
        }
        execl(inter, inter, inpath, static_cast<char*>(nullptr));
        _exit(127);
    }
    close(to_sol[0]);
    close(to_sol[1]);
    close(to_inter[0]);
    close(to_inter[1]);
    if (err_fd >= 0) {
        close(err_fd);
    }

    const long long start = now_ms();
    int sol_done = 0;
    int inter_done = 0;
    int sol_status = 0;
    int inter_status = 0;
    struct rusage sol_ru;
    std::memset(&sol_ru, 0, sizeof(sol_ru));
    int timed_out = 0;
    while (!sol_done || !inter_done) {
        if (now_ms() - start > time_limit_ms) {
            timed_out = 1;
            break;
        }
        if (!sol_done) {
            const pid_t w = wait4(sol_pid, &sol_status, WNOHANG, &sol_ru);
            if (w == sol_pid) {
                sol_done = 1;
            }
        }
        if (!inter_done) {
            const pid_t w = waitpid(inter_pid, &inter_status, WNOHANG);
            if (w == inter_pid) {
                inter_done = 1;
            }
        }
        if (!sol_done || !inter_done) {
            usleep(10000);
        }
    }
    if (timed_out) {
        if (!sol_done) {
            kill_wait(sol_pid);
        }
        if (!inter_done) {
            kill_wait(inter_pid);
        }
        emit(k, "TLE", time_limit_ms, mem_from_ru(sol_ru));
        return 0;
    }
    const int time_ms = static_cast<int>(now_ms() - start);
    if (WIFSIGNALED(sol_status) && WTERMSIG(sol_status) == SIGKILL) {
        emit(k, "MLE", time_ms, mem_from_ru(sol_ru));
        return 0;
    }
    if (!WIFEXITED(sol_status) || WEXITSTATUS(sol_status) != 0) {
        emit(k, "RE", time_ms, mem_from_ru(sol_ru));
        return 0;
    }
    if (!WIFEXITED(inter_status)) {
        emit(k, "RE", time_ms, mem_from_ru(sol_ru));
        return 0;
    }
    if (WEXITSTATUS(inter_status) != 0) {
        emit(k, "WA", time_ms, mem_from_ru(sol_ru));
        return 0;
    }
    emit(k, "OK", time_ms, mem_from_ru(sol_ru));
    return 1;
}

int run_comm(int k, int time_limit_ms, char* alice, char* bob, char* manager) {
    char alice_in[] = "alice_in.fifo";
    char alice_out[] = "alice_out.fifo";
    char bob_in[] = "bob_in.fifo";
    char bob_out[] = "bob_out.fifo";
    unlink(alice_in);
    unlink(alice_out);
    unlink(bob_in);
    unlink(bob_out);
    if (mkfifo(alice_in, 0666) != 0 || mkfifo(alice_out, 0666) != 0
     || mkfifo(bob_in, 0666) != 0 || mkfifo(bob_out, 0666) != 0) {
        emit(k, "SE", 0, -1);
        return 0;
    }
    const int ai = open(alice_in, O_RDWR);
    const int ao = open(alice_out, O_RDWR);
    const int bi = open(bob_in, O_RDWR);
    const int bo = open(bob_out, O_RDWR);
    if (ai < 0 || ao < 0 || bi < 0 || bo < 0) {
        emit(k, "SE", 0, -1);
        return 0;
    }
    const int err_fd = open_case("err", k, O_WRONLY | O_CREAT | O_TRUNC);
    char inpath[64];
    std::snprintf(inpath, sizeof(inpath), "in_%d.txt", k);

    const pid_t alice_pid = fork();
    if (alice_pid == 0) {
        dup2(ai, 0);
        dup2(ao, 1);
        if (err_fd >= 0) {
            dup2(err_fd, 2);
        }
        execl(alice, alice, static_cast<char*>(nullptr));
        _exit(127);
    }
    const pid_t bob_pid = fork();
    if (bob_pid == 0) {
        dup2(bi, 0);
        dup2(bo, 1);
        if (err_fd >= 0) {
            dup2(err_fd, 2);
        }
        execl(bob, bob, static_cast<char*>(nullptr));
        _exit(127);
    }
    const pid_t mgr_pid = fork();
    if (mgr_pid == 0) {
        if (err_fd >= 0) {
            dup2(err_fd, 2);
        }
        execl(manager, manager, inpath, alice_in, alice_out, bob_in, bob_out,
              static_cast<char*>(nullptr));
        _exit(127);
    }
    if (alice_pid < 0 || bob_pid < 0 || mgr_pid < 0) {
        kill_wait(alice_pid);
        kill_wait(bob_pid);
        kill_wait(mgr_pid);
        emit(k, "SE", 0, -1);
        return 0;
    }

    const long long start = now_ms();
    int alice_done = 0;
    int bob_done = 0;
    int mgr_done = 0;
    int alice_st = 0;
    int bob_st = 0;
    int mgr_st = 0;
    struct rusage alice_ru;
    struct rusage bob_ru;
    std::memset(&alice_ru, 0, sizeof(alice_ru));
    std::memset(&bob_ru, 0, sizeof(bob_ru));
    int timed_out = 0;
    while (!alice_done || !bob_done || !mgr_done) {
        if (now_ms() - start > time_limit_ms) {
            timed_out = 1;
            break;
        }
        if (!alice_done) {
            if (wait4(alice_pid, &alice_st, WNOHANG, &alice_ru) == alice_pid) {
                alice_done = 1;
            }
        }
        if (!bob_done) {
            if (wait4(bob_pid, &bob_st, WNOHANG, &bob_ru) == bob_pid) {
                bob_done = 1;
            }
        }
        if (!mgr_done) {
            if (waitpid(mgr_pid, &mgr_st, WNOHANG) == mgr_pid) {
                mgr_done = 1;
            }
        }
        if (!alice_done || !bob_done || !mgr_done) {
            usleep(10000);
        }
    }
    if (timed_out) {
        if (!alice_done) {
            kill_wait(alice_pid);
        }
        if (!bob_done) {
            kill_wait(bob_pid);
        }
        if (!mgr_done) {
            kill_wait(mgr_pid);
        }
        emit(k, "TLE", time_limit_ms, -1);
        return 0;
    }
    close(ai);
    close(ao);
    close(bi);
    close(bo);
    if (err_fd >= 0) {
        close(err_fd);
    }
    const int time_ms = static_cast<int>(now_ms() - start);
    const int mem = alice_ru.ru_maxrss > bob_ru.ru_maxrss
        ? static_cast<int>(alice_ru.ru_maxrss)
        : static_cast<int>(bob_ru.ru_maxrss);
    if ((WIFSIGNALED(alice_st) && WTERMSIG(alice_st) == SIGKILL)
     || (WIFSIGNALED(bob_st) && WTERMSIG(bob_st) == SIGKILL)) {
        emit(k, "MLE", time_ms, mem > 0 ? mem : -1);
        return 0;
    }
    if (!WIFEXITED(alice_st) || WEXITSTATUS(alice_st) != 0
     || !WIFEXITED(bob_st) || WEXITSTATUS(bob_st) != 0) {
        emit(k, "RE", time_ms, mem > 0 ? mem : -1);
        return 0;
    }
    if (!WIFEXITED(mgr_st)) {
        emit(k, "RE", time_ms, mem > 0 ? mem : -1);
        return 0;
    }
    if (WEXITSTATUS(mgr_st) != 0) {
        emit(k, "WA", time_ms, mem > 0 ? mem : -1);
        return 0;
    }
    emit(k, "OK", time_ms, mem > 0 ? mem : -1);
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: runner MODE time_ms cases PROG...\n");
        return 2;
    }
    const char* mode = argv[1];
    const int time_limit_ms = std::atoi(argv[2]);
    const int case_count = std::atoi(argv[3]);
    if (time_limit_ms <= 0 || case_count <= 0) {
        return 2;
    }
    for (int k = 1; k <= case_count; ++k) {
        int ok = 0;
        if (std::strcmp(mode, "std") == 0) {
            ok = run_std(k, time_limit_ms, argv + 4);
        } else if (std::strcmp(mode, "interactive") == 0 && argc >= 6) {
            ok = run_interactive(k, time_limit_ms, argv[4], argv[5]);
        } else if (std::strcmp(mode, "comm") == 0 && argc >= 7) {
            ok = run_comm(k, time_limit_ms, argv[4], argv[5], argv[6]);
        } else {
            emit(k, "SE", 0, -1);
            return 3;
        }
        if (!ok) {
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

int docker_image_exists(const std::string& image) {
#ifdef _WIN32
    const std::string cmd = "docker image inspect " + image + " >nul 2>&1";
#else
    const std::string cmd = "docker image inspect " + image + " >/dev/null 2>&1";
#endif
    return std::system(cmd.c_str()) == 0 ? 1 : 0;
}

int docker_available() {
#ifdef _WIN32
    const int daemon = std::system("docker version >nul 2>&1");
#else
    const int daemon = std::system("docker version >/dev/null 2>&1");
#endif
    if (daemon != 0) {
        return 0;
    }
    if (docker_image_exists("nloj-judge:bookworm") || docker_image_exists("gcc:13-bookworm")) {
        return 1;
    }
    return 0;
}

std::string sandbox_image() {
    if (docker_image_exists("nloj-judge:bookworm")) {
        return "nloj-judge:bookworm";
    }
    return "gcc:13-bookworm";
}

int image_has_language(const std::string& image, const std::string& language) {
    if (language == "CPP" || language == "C") {
        return 1;
    }
    return image.find("nloj-judge") != std::string::npos ? 1 : 0;
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

std::string case_file(const char* kind, int k) {
    return std::string(kind) + "_" + std::to_string(k) + ".txt";
}

std::string join_argv(const std::vector<std::string>& argv) {
    std::string s;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i > 0) {
            s += ' ';
        }
        s += argv[i];
    }
    return s;
}

std::string problem_type_of(const SandboxJudgeRequest& request) {
    return request.problem_type.empty() ? "STANDARD" : request.problem_type;
}

std::string judge_mode_of(const SandboxJudgeRequest& request) {
    return request.judge_mode.empty() ? "EXACT" : request.judge_mode;
}

struct JobFiles {
    std::string extra_compile;
    std::string runner_cmd;
};

int write_job_files(const SandboxJudgeRequest& request,
                    const std::filesystem::path& root,
                    JobFiles& job,
                    std::string* err) {
    // 写 runner / 用户源 / 用例 / extra -> 拼编译附加命令与 runner 启动行
    const std::string type = problem_type_of(request);
    const std::string mode = judge_mode_of(request);
    const int case_count = static_cast<int>(request.inputs.size());
    if (!write_file(root / "runner.cpp", kRunnerSource)) {
        return 0;
    }
    for (std::size_t i = 0; i < request.inputs.size(); ++i) {
        const int k = static_cast<int>(i + 1);
        if (!write_file(root / case_file("in", k), request.inputs[i])) {
            return 0;
        }
        if (i < request.expecteds.size()
         && !write_file(root / case_file("ans", k), request.expecteds[i])) {
            return 0;
        }
    }

    if (type == "STANDARD") {
        const char* src = language_source_name(request.language);
        if (!write_file(root / src, request.code)) {
            return 0;
        }
        const std::string uc = language_compile_cmd(request.language, 1);
        if (!uc.empty()) {
            job.extra_compile += " && " + uc;
        }
        if (mode == "SPJ") {
            if (request.extra_code.empty()) {
                if (err != nullptr) {
                    *err = "SPJ requires extra_code";
                }
                return 0;
            }
            if (!write_file(root / "checker.cpp", request.extra_code)) {
                return 0;
            }
        }
        job.runner_cmd = "./runner std "
                        + std::to_string(request.time_limit_ms) + " "
                        + std::to_string(case_count) + " "
                        + join_argv(language_run_argv(request.language, 1));
        return 1;
    }

    if (type == "INTERACTIVE") {
        if (!language_allows_interactive(request.language)) {
            if (err != nullptr) {
                *err = "interactive problems only support CPP/C";
            }
            return 0;
        }
        if (request.extra_code.empty()) {
            if (err != nullptr) {
                *err = "interactive requires interactor extra_code";
            }
            return 0;
        }
        const char* src = language_source_name(request.language);
        if (!write_file(root / src, request.code)
         || !write_file(root / "interactor.cpp", request.extra_code)) {
            return 0;
        }
        job.extra_compile += " && " + language_compile_cmd(request.language, 1);
        job.extra_compile += " && g++ -O2 -std=c++17 -o interactor interactor.cpp";
        job.runner_cmd = "./runner interactive "
                        + std::to_string(request.time_limit_ms) + " "
                        + std::to_string(case_count)
                        + " ./main ./interactor";
        return 1;
    }

    if (type == "COMMUNICATION") {
        if (!language_allows_interactive(request.language)) {
            if (err != nullptr) {
                *err = "communication problems only support CPP/C";
            }
            return 0;
        }
        std::string alice;
        std::string bob;
        if (!split_comm_sources(request.code, alice, bob)) {
            if (err != nullptr) {
                *err = "communication code needs ===NLOJ_FILE:alice=== and ===NLOJ_FILE:bob===";
            }
            return 0;
        }
        if (request.extra_code.empty()) {
            if (err != nullptr) {
                *err = "communication requires manager extra_code";
            }
            return 0;
        }
        if (request.language == "C") {
            if (!write_file(root / "alice.c", alice) || !write_file(root / "bob.c", bob)) {
                return 0;
            }
            job.extra_compile += " && gcc -O2 -o alice alice.c && gcc -O2 -o bob bob.c";
        } else {
            if (!write_file(root / "alice.cpp", alice) || !write_file(root / "bob.cpp", bob)) {
                return 0;
            }
            job.extra_compile += " && g++ -O2 -std=c++17 -o alice alice.cpp"
                                 " && g++ -O2 -std=c++17 -o bob bob.cpp";
        }
        if (!write_file(root / "manager.cpp", request.extra_code)) {
            return 0;
        }
        job.extra_compile += " && g++ -O2 -std=c++17 -o manager manager.cpp";
        job.runner_cmd = "./runner comm "
                        + std::to_string(request.time_limit_ms) + " "
                        + std::to_string(case_count)
                        + " ./alice ./bob ./manager";
        return 1;
    }

    if (err != nullptr) {
        *err = "unknown problem type";
    }
    return 0;
}

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
            continue;
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

int apply_spj_local(const std::filesystem::path& root,
                    int posix,
                    std::vector<SandboxCaseResult>& cases,
                    std::string* err) {
    const std::filesystem::path exe = root / (posix ? "checker" : "checker.exe");
    const auto stdout_path = root / "spj_out.txt";
    const auto stderr_path = root / "spj_err.txt";
    for (auto& one : cases) {
        if (one.verdict != "OK") {
            continue;
        }
        const std::string cmd = quote_path(exe.string()) + " "
                               + quote_path((root / case_file("in", one.index)).string()) + " "
                               + quote_path((root / case_file("out", one.index)).string()) + " "
                               + quote_path((root / case_file("ans", one.index)).string());
        const CmdResult r = run_cmd(cmd, stdout_path, stderr_path, kSpjTimeoutMs, root.string());
        if (r.timed_out) {
            if (err != nullptr) {
                *err = "spj timeout";
            }
            return 0;
        }
        if (r.exit_code != 0) {
            one.verdict = "WA";
        }
    }
    return 1;
}

int run_host_spj(const std::filesystem::path& root,
                 std::vector<SandboxCaseResult>& cases,
                 std::string* err) {
    // SPJ 是出题人代码，信任侧在宿主编译运行；用户输出已在 out_k / stdout_text
#ifdef _WIN32
    const int posix = 0;
    const std::string compile = "g++ -O2 -std=c++17 -o checker.exe checker.cpp";
#else
    const int posix = 1;
    const std::string compile = "g++ -O2 -std=c++17 -o checker checker.cpp";
#endif
    const auto stdout_path = root / "spj_compile_out.txt";
    const auto stderr_path = root / "spj_compile_err.txt";
    const CmdResult cr = run_cmd(compile, stdout_path, stderr_path, kCompileTimeoutMs, root.string());
    if (cr.timed_out || cr.exit_code != 0) {
        if (err != nullptr) {
            *err = read_file_limited(stderr_path, 4096);
            if (err -> empty()) {
                *err = "spj compile failed";
            }
        }
        return 0;
    }
    for (auto& one : cases) {
        if (one.verdict == "OK" && !one.stdout_text.empty()) {
            write_file(root / case_file("out", one.index), one.stdout_text);
        }
    }
    return apply_spj_local(root, posix, cases, err);
}

std::string docker_run_prefix(const std::string& work_dir,
                              const std::string& image,
                              int mem_kb,
                              int read_only) {
    const int limit = mem_kb > 0 ? mem_kb : 262144;
    std::string cmd = "docker run --rm --network=none --memory="
                     + std::to_string(limit) + "k --memory-swap="
                     + std::to_string(limit) + "k --pids-limit=64 --cpus=1 ";
    if (read_only) {
        cmd += "--read-only ";
    }
    cmd += "-v " + docker_bind(work_dir, "/work") + " -w /work " + image + " ";
    return cmd;
}

class DockerJudgeSandbox : public JudgeSandbox {
public:
    DockerJudgeSandbox() : work_dir_(make_work_dir()), image_(sandbox_image()) {}

    ~DockerJudgeSandbox() override {
        std::error_code ec;
        std::filesystem::remove_all(work_dir_, ec);
    }

    SandboxJudgeResult judge(const SandboxJudgeRequest& request) override {
        // 写作业文件 -> 容器内编译 runner+用户(+checker) -> 单容器跑用例 -> SPJ
        SandboxJudgeResult out;
        out.status = "SYSTEM_ERROR";
        out.error_text = "sandbox run failed";
        if (!language_supported(request.language)) {
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
        JobFiles job;
        std::string prep_err;
        if (!write_job_files(request, root, job, &prep_err)) {
            out.status = "CE";
            out.error_text = prep_err.empty() ? "prepare job failed" : prep_err;
            return out;
        }

        const int compile_mem_kb = request.memory_limit_kb > 524288 ? request.memory_limit_kb : 524288;
        const std::string compile_cmd = docker_run_prefix(work_dir_, image_, compile_mem_kb, 0)
                                       + "bash -c \"g++ -O2 -std=c++17 -o runner runner.cpp"
                                       + job.extra_compile
                                       + "\"";
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

        const int mem_kb = request.memory_limit_kb > 0 ? request.memory_limit_kb : 262144;
        const std::string run_cmd_text = docker_run_prefix(work_dir_, image_, mem_kb, 1)
                                        + "bash -c \""
                                        + job.runner_cmd
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
        if (problem_type_of(request) == "STANDARD" && judge_mode_of(request) == "SPJ") {
            std::string spj_err;
            if (!run_host_spj(root, out.cases, &spj_err)) {
                out.status = "SYSTEM_ERROR";
                out.error_text = spj_err.empty() ? "spj failed" : spj_err;
                out.cases.clear();
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

class LocalProcessSandbox : public JudgeSandbox {
public:
    LocalProcessSandbox() : work_dir_(make_work_dir()) {}

    ~LocalProcessSandbox() override {
        std::error_code ec;
        std::filesystem::remove_all(work_dir_, ec);
    }

    SandboxJudgeResult judge(const SandboxJudgeRequest& request) override {
        // 本机编译运行（无 cgroup）。交互/通信在 Windows 上需要 Docker。
        SandboxJudgeResult out;
        out.status = "SYSTEM_ERROR";
        out.error_text = "sandbox run failed";
        if (!language_supported(request.language)) {
            out.status = "CE";
            out.error_text = "unsupported language";
            return out;
        }
        if (request.code.empty() || request.inputs.empty() || request.time_limit_ms <= 0) {
            return out;
        }

        const std::string type = problem_type_of(request);
#ifdef _WIN32
        if (type == "INTERACTIVE" || type == "COMMUNICATION") {
            out.error_text = "interactive/communication require Docker or Linux";
            return out;
        }
#endif

        const std::filesystem::path root(work_dir_);
        const auto stdout_path = root / "sandbox_stdout.txt";
        const auto stderr_path = root / "sandbox_stderr.txt";
        const int case_count = static_cast<int>(request.inputs.size());
        JobFiles job;
        std::string prep_err;
        if (!write_job_files(request, root, job, &prep_err)) {
            out.status = "CE";
            out.error_text = prep_err.empty() ? "prepare job failed" : prep_err;
            return out;
        }

#ifndef _WIN32
        const std::string compile_cmd = "g++ -O2 -std=c++17 -o runner runner.cpp"
                                       + job.extra_compile;
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
        const std::string run_cmd_text = job.runner_cmd;
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
        if (type == "STANDARD" && judge_mode_of(request) == "SPJ") {
            std::string spj_err;
            if (!run_host_spj(root, out.cases, &spj_err)) {
                out.status = "SYSTEM_ERROR";
                out.error_text = spj_err.empty() ? "spj failed" : spj_err;
                out.cases.clear();
                return out;
            }
        }
        out.status = "OK";
        return out;
#else
        // Windows：逐用例跑（无隔离演示）；SPJ 用本机 g++ 编 checker
        const std::string user_compile = language_compile_cmd(request.language, 0);
        if (!user_compile.empty()) {
            const CmdResult cr = run_cmd(user_compile, stdout_path, stderr_path, kCompileTimeoutMs, work_dir_);
            if (cr.timed_out) {
                out.error_text = "compile timeout";
                return out;
            }
            if (cr.exit_code != 0) {
                out.status = "CE";
                out.error_text = read_file_limited(stderr_path, 4096);
                return out;
            }
        }
        const std::vector<std::string> run_argv = language_run_argv(request.language, 0);
        for (int k = 1; k <= case_count; ++k) {
            const std::string cmd = "cmd /c " + join_argv(run_argv)
                                   + " < " + case_file("in", k);
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
            write_file(root / case_file("out", k), one.stdout_text);
            out.cases.push_back(std::move(one));
        }
        if (judge_mode_of(request) == "SPJ") {
            std::string spj_err;
            if (!run_host_spj(root, out.cases, &spj_err)) {
                out.status = "SYSTEM_ERROR";
                out.error_text = spj_err.empty() ? "spj failed" : spj_err;
                out.cases.clear();
                return out;
            }
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
    return make_sandbox_for("CPP");
}

std::unique_ptr<JudgeSandbox> make_sandbox_for(const std::string& language) {
    if (docker_available() && image_has_language(sandbox_image(), language)) {
        return std::unique_ptr<JudgeSandbox>(new DockerJudgeSandbox());
    }
    return std::unique_ptr<JudgeSandbox>(new LocalProcessSandbox());
}

}  // namespace nloj::judge
