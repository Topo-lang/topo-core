cmake_minimum_required(VERSION 3.20)
project(hashlib_driver LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

add_executable(hashlib_driver main.cpp)
target_link_libraries(hashlib_driver PRIVATE hashlib)
