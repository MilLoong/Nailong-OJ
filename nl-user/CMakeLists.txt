# 用户模块：注册、登录、鉴权
set(NLOJ_OPENSSL_HINT "C:/anaconda/anaconda3/Library")

if(DEFINED ENV{OPENSSL_ROOT_DIR})
    set(OPENSSL_ROOT_DIR $ENV{OPENSSL_ROOT_DIR})
elseif(EXISTS "${NLOJ_OPENSSL_HINT}/include/openssl/evp.h")
    set(OPENSSL_ROOT_DIR "${NLOJ_OPENSSL_HINT}")
endif()
find_package(OpenSSL REQUIRED)

# 密码 / JWT（无 MySQL，可供单元测试单独链接）
add_library(nl-user-crypto src/auth_crypto.cpp)
target_include_directories(nl-user-crypto PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
)
target_link_libraries(nl-user-crypto PUBLIC OpenSSL::Crypto)

add_library(nl-user src/module.cpp)
target_include_directories(nl-user PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nl-user PUBLIC nl-common nl-user-crypto)

enable_testing()
add_executable(nloj_user_crypto_test tests/test_auth_crypto.cpp)
target_link_libraries(nloj_user_crypto_test PRIVATE nl-user-crypto)
add_test(NAME user_crypto COMMAND nloj_user_crypto_test)

# 注册登录联调（需本机 MySQL：nloj / nloj123456 / nloj_db）
add_executable(nloj_user_auth_db_test tests/test_auth_db.cpp)
target_link_libraries(nloj_user_auth_db_test PRIVATE nl-user)
add_test(NAME user_auth_db COMMAND nloj_user_auth_db_test)
if(WIN32)
    # 运行时找到 libmysql.dll（与链接库同目录）
    get_filename_component(_nloj_mysql_lib_dir "${NLOJ_MYSQL_LIBRARY}" DIRECTORY)
    set_tests_properties(user_auth_db PROPERTIES
        ENVIRONMENT "PATH=${_nloj_mysql_lib_dir};$ENV{PATH}")
endif()
