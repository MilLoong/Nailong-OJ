# 公共模块：工具类、错误码/枚举、Result/分页、第三方与基础设施封装、配置常量
# 当前已落地 MySQL 小工具（mysql.h）；其余按 Phase 推进。
# 优先本机 MySQL 9 自带客户端；否则用 third_party Connector/C。
set(NLOJ_MYSQL_ROOT "C:/MySQL/mysql-9.2.0-winx64")
if(NOT EXISTS "${NLOJ_MYSQL_ROOT}/include/mysql.h")
    set(NLOJ_MYSQL_ROOT "${CMAKE_SOURCE_DIR}/third_party/mysql-connector-c-6.1.11-winx64")
endif()

find_path(MYSQL_INCLUDE_DIR
    NAMES mysql.h
    PATH_SUFFIXES mysql include include/mysql
    PATHS
        "${NLOJ_MYSQL_ROOT}"
        "$ENV{MYSQL_DIR}"
        "C:/Program Files/MySQL/MySQL Server 8.4"
        "C:/Program Files/MySQL/MySQL Server 8.0"
        "C:/Program Files/MySQL/MySQL Server 9.0"
        /usr/include
        /usr/include/mysql
)
find_library(MYSQL_LIBRARY
    NAMES libmysql mysqlclient libmysqlclient
    PATH_SUFFIXES lib lib/vs14 lib/vs12
    PATHS
        "${NLOJ_MYSQL_ROOT}"
        "$ENV{MYSQL_DIR}"
        "C:/Program Files/MySQL/MySQL Server 8.4"
        "C:/Program Files/MySQL/MySQL Server 8.0"
        "C:/Program Files/MySQL/MySQL Server 9.0"
        /usr/lib
        /usr/lib/x86_64-linux-gnu
)
if(NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_LIBRARY)
    message(FATAL_ERROR
        "未找到 MySQL 客户端库（mysql.h / libmysql）。\n"
        "可将 Connector/C 放到 third_party/mysql-connector-c-6.1.11-winx64，\n"
        "或设置环境变量 MYSQL_DIR。")
endif()

# rabbitmq-c（不入库；本机没有则 git clone，并忽略坏掉的本地代理）
set(NLOJ_RABBITMQ_C_DIR "${CMAKE_SOURCE_DIR}/third_party/rabbitmq-c")
if(NOT EXISTS "${NLOJ_RABBITMQ_C_DIR}/CMakeLists.txt")
    find_package(Git REQUIRED)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -c http.proxy= -c https.proxy=
                clone --depth 1 --branch v0.14.0
                https://github.com/alanxz/rabbitmq-c.git
                "${NLOJ_RABBITMQ_C_DIR}"
        RESULT_VARIABLE _nloj_rmq_clone
    )
    if(NOT _nloj_rmq_clone EQUAL 0)
        message(FATAL_ERROR
            "无法克隆 rabbitmq-c 到 third_party/rabbitmq-c。\n"
            "请手动：git clone --depth 1 --branch v0.14.0 https://github.com/alanxz/rabbitmq-c.git")
    endif()
endif()
set(ENABLE_SSL_SUPPORT OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory("${NLOJ_RABBITMQ_C_DIR}" "${CMAKE_BINARY_DIR}/_deps/rabbitmq-c-build")
if(MSVC AND TARGET rabbitmq-static)
    target_compile_options(rabbitmq-static PRIVATE /W3)
endif()

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

add_library(nl-common src/module.cpp src/mysql.cpp src/mq.cpp src/redis.cpp src/config.cpp src/error.cpp)
target_include_directories(nl-common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${MYSQL_INCLUDE_DIR}
        ${CMAKE_SOURCE_DIR}/third_party
)
target_link_libraries(nl-common PUBLIC ${MYSQL_LIBRARY} rabbitmq-static)
if(WIN32)
    target_link_libraries(nl-common PUBLIC ws2_32)
endif()
target_compile_features(nl-common PUBLIC cxx_std_20)
if(MSVC)
    target_compile_definitions(nl-common PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

set(NLOJ_MYSQL_LIBRARY "${MYSQL_LIBRARY}" CACHE FILEPATH "MySQL client library" FORCE)

enable_testing()
add_executable(nloj_common_mq_test tests/test_mq.cpp)
target_link_libraries(nloj_common_mq_test PRIVATE nl-common)
add_test(NAME common_mq COMMAND nloj_common_mq_test)
add_executable(nloj_common_redis_test tests/test_redis.cpp)
target_link_libraries(nloj_common_redis_test PRIVATE nl-common)
add_test(NAME common_redis COMMAND nloj_common_redis_test)
if(WIN32)
    get_filename_component(_nloj_mysql_lib_dir "${NLOJ_MYSQL_LIBRARY}" DIRECTORY)
    set_tests_properties(common_mq PROPERTIES
        ENVIRONMENT "PATH=${_nloj_mysql_lib_dir};$ENV{PATH}")
    set_tests_properties(common_redis PROPERTIES
        ENVIRONMENT "PATH=${_nloj_mysql_lib_dir};$ENV{PATH}")
endif()
