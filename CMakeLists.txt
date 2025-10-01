cmake_minimum_required(VERSION 3.20)
project(cpp_concurrency_video)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(cpp_concurrency_video main.cpp)

if (MSVC)
    target_compile_options(cpp_concurrency_video PRIVATE /W4 /permissive-)
else()
    target_compile_options(cpp_concurrency_video PRIVATE -Wall -Wextra -Wpedantic)
endif()