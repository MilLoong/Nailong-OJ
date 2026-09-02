# 启动模块：HTTP 路由、配置（后续再加）
add_executable(nloj_api src/main.cpp)
target_link_libraries(nloj_api PRIVATE nl-common nl-user nl-problem nl-submit nl-judge)
