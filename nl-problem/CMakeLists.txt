# 题目模块：CRUD、用例、详情缓存 JSON
set(NLOJ_JSON_HPP "${CMAKE_SOURCE_DIR}/third_party/nlohmann/json.hpp")
if(NOT EXISTS "${NLOJ_JSON_HPP}")
    find_program(NLOJ_CURL NAMES curl curl.exe)
    if(NOT NLOJ_CURL)
        message(FATAL_ERROR "缺少 nlohmann/json.hpp，且找不到 curl")
    endif()
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party/nlohmann")
    execute_process(
        COMMAND "${NLOJ_CURL}" -L --retry 3 -o "${NLOJ_JSON_HPP}"
                "https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp"
        RESULT_VARIABLE _nloj_json_rc
    )
    if(NOT _nloj_json_rc EQUAL 0)
        message(FATAL_ERROR "下载 nlohmann/json.hpp 失败")
    endif()
endif()

add_library(nl-problem src/module.cpp)
target_include_directories(nl-problem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(nl-problem PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(nl-problem PUBLIC nl-common)

enable_testing()
add_executable(nloj_problem_db_test tests/test_problem_db.cpp)
target_link_libraries(nloj_problem_db_test PRIVATE nl-problem)
add_test(NAME problem_db COMMAND nloj_problem_db_test)
if(WIN32)
    get_filename_component(_nloj_mysql_lib_dir "${NLOJ_MYSQL_LIBRARY}" DIRECTORY)
    set_tests_properties(problem_db PROPERTIES
        ENVIRONMENT "PATH=${_nloj_mysql_lib_dir};$ENV{PATH}")
endif()
