cmake_minimum_required(VERSION 3.22.1)
project(suka)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SUKA_SOURCES)

if(NOT SUKA_SOURCES)
    message(FATAL_ERROR "No .cpp sources found in ${CMAKE_CURRENT_SOURCE_DIR}. Expected android_bridge.cpp")
endif()

add_library(suka SHARED ${SUKA_SOURCES})

target_include_directories(suka PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src
)

target_link_libraries(suka log android)
