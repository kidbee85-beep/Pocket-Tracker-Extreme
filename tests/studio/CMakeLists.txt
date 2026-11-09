cmake_minimum_required(VERSION 3.22)
project(studio-tests C CXX)
set(CMAKE_CXX_STANDARD 17)
add_subdirectory(../../native/studio mutable)
add_executable(studio-test test.cpp)
target_link_libraries(studio-test PRIVATE pt-mutable)

target_compile_options(studio-test PRIVATE -UNDEBUG)
