cmake_minimum_required(VERSION 3.20)
project(shared_lib LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

# Build as static library (for standalone C++ testing)
add_library(hashlib STATIC src/hashlib.cpp)
target_include_directories(hashlib PUBLIC include)

# Driver
add_executable(hashlib_driver driver/main.cpp)
target_link_libraries(hashlib_driver PRIVATE hashlib)
