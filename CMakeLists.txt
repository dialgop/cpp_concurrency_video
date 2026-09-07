cmake_minimum_required(VERSION 3.20)
project(cpp_concurrency_video)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
include_directories(include)

function(add_demo name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE Threads::Threads)
    if (MSVC)
        target_compile_options(${name} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# Standalone examples: one C++ concurrency primitive per file.
add_demo(example_thread_basics examples/thread_basics.cpp)
add_demo(example_jthread_stop_token examples/jthread_stop_token.cpp)
add_demo(example_mutex_lockguard examples/mutex_lockguard.cpp)
add_demo(example_condition_variable_queue examples/condition_variable_queue.cpp)
add_demo(example_promise_future examples/promise_future.cpp)
add_demo(example_async_future examples/async_future.cpp)
add_demo(example_packaged_task examples/packaged_task.cpp)

# Composed project stages: each reuses components from include/.
add_demo(stage1_delay_line stages/stage1_delay_line.cpp)
add_demo(stage2_fusion stages/stage2_fusion.cpp)
