#include "nloj/common/module.h"
#include "nloj/judge/module.h"
#include "nloj/problem/module.h"
#include "nloj/submit/module.h"
#include "nloj/user/module.h"

#include <iostream>

int main() {
    std::cout << "nloj skeleton\n"
              << "  " << nloj::common::module_name() << '\n'
              << "  " << nloj::user::module_name() << '\n'
              << "  " << nloj::problem::module_name() << '\n'
              << "  " << nloj::submit::module_name() << '\n'
              << "  " << nloj::judge::module_name() << '\n';
    return 0;
}
