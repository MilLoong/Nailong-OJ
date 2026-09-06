# 判题模块：消费 MQ、沙箱、比对、独立节点
add_library(nl-judge src/module.cpp src/sandbox.cpp src/node.cpp src/language.cpp)
target_include_directories(nl-judge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nl-judge PUBLIC nl-common nl-submit nl-problem)

add_executable(nloj_judge_node src/judge_main.cpp)
target_link_libraries(nloj_judge_node PRIVATE nl-judge)

enable_testing()
add_executable(nloj_judge_db_test tests/test_judge_db.cpp)
target_link_libraries(nloj_judge_db_test PRIVATE nl-judge)
add_test(NAME judge_db COMMAND nloj_judge_db_test)

add_executable(nloj_judge_language_test tests/test_language.cpp)
target_link_libraries(nloj_judge_language_test PRIVATE nl-judge)
add_test(NAME judge_language COMMAND nloj_judge_language_test)
if(WIN32)
    get_filename_component(_nloj_mysql_lib_dir "${NLOJ_MYSQL_LIBRARY}" DIRECTORY)
    set_tests_properties(judge_db PROPERTIES
        ENVIRONMENT "PATH=${_nloj_mysql_lib_dir};$ENV{PATH}")
    set_tests_properties(judge_language PROPERTIES
        ENVIRONMENT "PATH=${_nloj_mysql_lib_dir};$ENV{PATH}")
endif()
