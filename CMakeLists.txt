cmake_minimum_required(VERSION 3.28.0)
project(main LANGUAGES CXX)

set(CMAKE_CXX_COMPILER g++)

add_executable(main default.cpp)

target_link_libraries(main "avutil")
target_link_libraries(main "avcodec")
target_link_libraries(main "opus")