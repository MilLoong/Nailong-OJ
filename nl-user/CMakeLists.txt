# 用户模块：注册、登录、鉴权
set(NLOJ_MYSQL_ROOT "${CMAKE_SOURCE_DIR}/third_party/mysql-connector-c-6.1.11-winx64")
set(NLOJ_OPENSSL_HINT "C:/anaconda/anaconda3/Library")

if(DEFINED ENV{OPENSSL_ROOT_DIR})
    set(OPENSSL_ROOT_DIR $ENV{OPENSSL_ROOT_DIR})
elseif(EXISTS "${NLOJ_OPENSSL_HINT}/include/openssl/evp.h")
    set(OPENSSL_ROOT_DIR "${NLOJ_OPENSSL_HINT}")
endif()
find_package(OpenSSL REQUIRED)

# 密码 / JWT（无 MySQL，可供单元测试单独链接）
add_library(nl-user-crypto src/auth_crypto.cpp)
target_include_directories(nl-user-crypto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nl-user-crypto PUBLIC OpenSSL::Crypto)

add_library(nl-user src/module.cpp)
target_include_directories(nl-user PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nl-user PUBLIC nl-common nl-user-crypto)

find_path(MYSQL_INCLUDE_DIR
    NAMES mysql.h
    PATH_SUFFIXES mysql include include/mysql
    PATHS
        "${NLOJ_MYSQL_ROOT}"
        "$ENV{MYSQL_DIR}"
        "C:/Program Files/MySQL/MySQL Server 8.4"
        "C:/Program Files/MySQL/MySQL Server 8.0"
        "C:/Program Files/MySQL/MySQL Server 9.0"
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
)
if(NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_LIBRARY)
    message(FATAL_ERROR
        "未找到 MySQL 客户端库（mysql.h / libmysql）。\n"
        "可将 Connector/C 放到 third_party/mysql-connector-c-6.1.11-winx64，\n"
        "或设置环境变量 MYSQL_DIR。")
endif()

target_include_directories(nl-user PRIVATE ${MYSQL_INCLUDE_DIR})
target_link_libraries(nl-user PRIVATE ${MYSQL_LIBRARY})
if(WIN32)
    target_link_libraries(nl-user PRIVATE ws2_32)
endif()

enable_testing()
add_executable(nloj_user_crypto_test tests/test_auth_crypto.cpp)
target_link_libraries(nloj_user_crypto_test PRIVATE nl-user-crypto)
add_test(NAME user_crypto COMMAND nloj_user_crypto_test)
