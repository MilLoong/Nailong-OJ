# 提交模块：创建提交、查询/列表、发判题 MQ
add_library(nl-submit src/module.cpp)
target_include_directories(nl-submit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nl-submit PUBLIC nl-common nl-problem)

enable_testing()
add_executable(nloj_submit_db_test tests/test_submit_db.cpp)
target_link_libraries(nloj_submit_db_test PRIVATE nl-submit)
add_test(NAME submit_db COMMAND nloj_submit_db_test)
if(WIN32)
    get_filename_component(_nloj_mysql_lib_dir "${NLOJ_MYSQL_LIBRARY}" DIRECTORY)
    set_tests_properties(submit_db PROPERTIES
        ENVIRONMENT "PATH=${_nloj_mysql_lib_dir};$ENV{PATH}")
endif()
