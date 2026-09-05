# 启动模块：HTTP 路由、统一响应、判题 Worker
set(NLOJ_HTTPLIB_H "${CMAKE_SOURCE_DIR}/third_party/cpp-httplib/httplib.h")
set(NLOJ_JSON_HPP "${CMAKE_SOURCE_DIR}/third_party/nlohmann/json.hpp")

find_program(NLOJ_CURL NAMES curl curl.exe)
function(nloj_fetch_file url dest)
    if(EXISTS "${dest}")
        return()
    endif()
    if(NOT NLOJ_CURL)
        message(FATAL_ERROR "缺少 ${dest}，且找不到 curl 下载 ${url}")
    endif()
    get_filename_component(_dir "${dest}" DIRECTORY)
    file(MAKE_DIRECTORY "${_dir}")
    execute_process(
        COMMAND "${NLOJ_CURL}" -L --retry 3 -o "${dest}" "${url}"
        RESULT_VARIABLE _rc
    )
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "下载失败：${url}")
    endif()
endfunction()

nloj_fetch_file(
    "https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.18.3/httplib.h"
    "${NLOJ_HTTPLIB_H}"
)
nloj_fetch_file(
    "https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp"
    "${NLOJ_JSON_HPP}"
)

add_executable(nloj_api src/main.cpp src/http_json.cpp src/server.cpp)
target_include_directories(nloj_api PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/cpp-httplib
    ${CMAKE_SOURCE_DIR}/third_party
)
target_link_libraries(nloj_api PRIVATE nl-common nl-user nl-problem nl-submit nl-judge)
target_compile_definitions(nloj_api PRIVATE
    NLOJ_OPENAPI_PATH="${CMAKE_SOURCE_DIR}/docs/openapi.yaml"
)
add_custom_command(TARGET nloj_api POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/docs/openapi.yaml"
        "$<TARGET_FILE_DIR:nloj_api>/openapi.yaml"
)

enable_testing()
add_executable(nloj_api_http_test tests/test_http_json.cpp src/http_json.cpp)
target_include_directories(nloj_api_http_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)
add_test(NAME api_http_json COMMAND nloj_api_http_test)

# HTTP 全链路（需 MySQL；判题需 Docker 或 g++）。不链进纯 JSON 单测。
add_executable(nloj_api_e2e_test tests/test_http_e2e.cpp src/http_json.cpp src/server.cpp)
target_include_directories(nloj_api_e2e_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/cpp-httplib
    ${CMAKE_SOURCE_DIR}/third_party
)
target_link_libraries(nloj_api_e2e_test PRIVATE nl-common nl-user nl-problem nl-submit nl-judge)
target_compile_definitions(nloj_api_e2e_test PRIVATE
    NLOJ_OPENAPI_PATH="${CMAKE_SOURCE_DIR}/docs/openapi.yaml"
)
add_test(NAME api_http_e2e COMMAND nloj_api_e2e_test)
if(WIN32)
    get_filename_component(_nloj_mysql_lib_dir "${NLOJ_MYSQL_LIBRARY}" DIRECTORY)
    set_tests_properties(api_http_e2e PROPERTIES
        ENVIRONMENT "PATH=${_nloj_mysql_lib_dir};$ENV{PATH}")
endif()

# 压测客户端：打已启动的 nloj_api，不进 CTest。
add_executable(nloj_api_bench tests/bench_http.cpp)
target_include_directories(nloj_api_bench PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/cpp-httplib
)
if(WIN32)
    target_link_libraries(nloj_api_bench PRIVATE ws2_32)
endif()
