# 题目模块：CRUD、用例（后续再加）
add_library(nl-problem src/module.cpp)
target_include_directories(nl-problem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nl-problem PUBLIC nl-common)
