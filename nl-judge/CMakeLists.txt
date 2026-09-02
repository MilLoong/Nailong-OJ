# 判题模块：消费 MQ、沙箱、比对（后续再加）
add_library(nl-judge src/module.cpp)
target_include_directories(nl-judge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nl-judge PUBLIC nl-common nl-submit nl-problem)
