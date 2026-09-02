# 公共模块：Result、错误码、枚举（后续再加）
add_library(nl-common src/module.cpp)
target_include_directories(nl-common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(nl-common PUBLIC cxx_std_20)
