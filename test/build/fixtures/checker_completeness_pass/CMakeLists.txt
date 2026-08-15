cmake_minimum_required(VERSION 3.20)
project(checker_completeness_pass LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

add_executable(checker_completeness_pass
    src/engine.cpp
    src/main.cpp
)
target_include_directories(checker_completeness_pass PRIVATE include)
