# 提交模块：创建提交、发 MQ（后续再加）
add_library(nl-submit src/module.cpp)
target_include_directories(nl-submit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nl-submit PUBLIC nl-common nl-problem)
