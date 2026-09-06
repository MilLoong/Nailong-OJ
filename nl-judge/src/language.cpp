#include "nloj/judge/language.h"

#include <cstdlib>

namespace nloj::judge {

int language_supported(const std::string& language) {
    return language == "CPP"
        || language == "C"
        || language == "PYTHON"
        || language == "JAVA" ? 1 : 0;
}

int language_allows_interactive(const std::string& language) {
    return language == "CPP" || language == "C" ? 1 : 0;
}

const char* language_source_name(const std::string& language) {
    if (language == "C") {
        return "main.c";
    }
    if (language == "PYTHON") {
        return "main.py";
    }
    if (language == "JAVA") {
        return "Main.java";
    }
    return "main.cpp";
}

std::string language_compile_cmd(const std::string& language, int posix) {
    const char* out = posix ? "main" : "main.exe";
    if (language == "CPP") {
        return std::string("g++ -O2 -std=c++17 -o ") + out + " main.cpp";
    }
    if (language == "C") {
        return std::string("gcc -O2 -o ") + out + " main.c";
    }
    if (language == "JAVA") {
        return "javac Main.java";
    }
    return "";
}

std::vector<std::string> language_run_argv(const std::string& language, int posix) {
    std::vector<std::string> argv;
    if (language == "PYTHON") {
        if (posix) {
            argv.push_back("python3");
        } else if (command_exists("python")) {
            argv.push_back("python");
        } else {
            argv.push_back("python3");
        }
        argv.push_back("main.py");
        return argv;
    }
    if (language == "JAVA") {
        argv.push_back("java");
        argv.push_back("Main");
        return argv;
    }
    argv.push_back(posix ? "./main" : "main.exe");
    return argv;
}

int split_comm_sources(const std::string& code, std::string& alice, std::string& bob) {
    // 用标记切开两份源码：===NLOJ_FILE:alice=== / ===NLOJ_FILE:bob===
    const std::string a_mark = "===NLOJ_FILE:alice===";
    const std::string b_mark = "===NLOJ_FILE:bob===";
    const std::size_t a = code.find(a_mark);
    const std::size_t b = code.find(b_mark);
    if (a == std::string::npos || b == std::string::npos || a == b) {
        return 0;
    }
    const std::size_t a_start = a + a_mark.size();
    const std::size_t b_start = b + b_mark.size();
    if (a < b) {
        alice = code.substr(a_start, b - a_start);
        bob = code.substr(b_start);
    } else {
        bob = code.substr(b_start, a - b_start);
        alice = code.substr(a_start);
    }
    auto trim_lead = [](std::string& s) {
        std::size_t i = 0;
        while (i < s.size() && (s[i] == '\r' || s[i] == '\n' || s[i] == ' ')) {
            ++i;
        }
        s.erase(0, i);
    };
    trim_lead(alice);
    trim_lead(bob);
    return alice.empty() || bob.empty() ? 0 : 1;
}

int command_exists(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return 0;
    }
#ifdef _WIN32
    const std::string cmd = std::string("where ") + name + " >nul 2>&1";
#else
    const std::string cmd = std::string("command -v ") + name + " >/dev/null 2>&1";
#endif
    return std::system(cmd.c_str()) == 0 ? 1 : 0;
}

}  // namespace nloj::judge
