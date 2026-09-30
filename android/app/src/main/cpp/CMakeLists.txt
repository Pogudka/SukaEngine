cmake_minimum_required(VERSION 3.22.1)
project(suka)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(suka SHARED android_bridge.cpp)

target_include_directories(suka PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src)

target_link_libraries(suka log android)