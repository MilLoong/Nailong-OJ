# C++20 多模块：nl-common（MySQL / RabbitMQ）+ 领域模块 + nl-api（httplib）。
cmake_minimum_required(VERSION 3.22)
project(nloj VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive-)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(nl-common)
add_subdirectory(nl-user)
add_subdirectory(nl-problem)
add_subdirectory(nl-submit)
add_subdirectory(nl-judge)
add_subdirectory(nl-api)
