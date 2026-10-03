cmake_minimum_required(VERSION 3.22.1)
project(suka)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SUKA_SOURCES)

if(NOT SUKA_SOURCES)
    message(FATAL_ERROR "No .cpp sources found in ${CMAKE_CURRENT_SOURCE_DIR}. Expected android_bridge.cpp")
endif()

add_library(suka SHARED ${SUKA_SOURCES})
# ---------------------------------------------------------------------------
# Real Lua 5.4, downloaded and built at configure time.
# No Lua sources are stored in the repo; CI fetches them from lua.org.
# ---------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    luadep
    URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
)

# Download + extract only (Lua ships a Makefile, not a CMakeLists,
# so we build its sources ourselves below).
FetchContent_Populate(luadep)

set(LUA_SRC_DIR ${luadep_SOURCE_DIR}/src)

file(GLOB LUA_C_ALL CONFIGURE_DEPENDS "${LUA_SRC_DIR}/*.c")

# Exclude the standalone interpreter/compiler (they contain main()).
set(LUA_C_SRC "")
foreach(_f ${LUA_C_ALL})
    get_filename_component(_n ${_f} NAME)
    if(NOT _n STREQUAL "lua.c" AND NOT _n STREQUAL "luac.c" AND NOT _n STREQUAL "onelua.c")
        list(APPEND LUA_C_SRC ${_f})
    endif()
endforeach()

add_library(lua STATIC ${LUA_C_SRC})

# PIC is mandatory: suka is a shared .so on Android, and a static lib linked
# into it must be position-independent.
set_target_properties(lua PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(lua PUBLIC ${LUA_SRC_DIR})

# Android is POSIX; this enables popen/io and other POSIX bits in Lua.
target_compile_definitions(lua PRIVATE LUA_USE_POSIX)

# Make <lua.h> visible to suka (belt-and-suspenders alongside the PUBLIC above).
target_include_directories(suka PRIVATE ${LUA_SRC_DIR})

# Link the Lua static lib into the engine shared lib.
target_link_libraries(suka PRIVATE lua)

target_include_directories(suka PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src
)

target_link_libraries(suka log android)
