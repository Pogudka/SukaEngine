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

# Download + extract only.
# Lua ships a Makefile, not a CMakeLists.txt, so we build its sources manually.
FetchContent_Populate(luadep)

set(LUA_SRC_DIR ${luadep_SOURCE_DIR}/src)

file(GLOB LUA_C_ALL CONFIGURE_DEPENDS "${LUA_SRC_DIR}/*.c")

# Exclude standalone interpreter/compiler targets because they contain main().
set(LUA_C_SRC "")

foreach(_f ${LUA_C_ALL})
    get_filename_component(_n ${_f} NAME)

    if(NOT _n STREQUAL "lua.c" AND
       NOT _n STREQUAL "luac.c" AND
       NOT _n STREQUAL "onelua.c")
        list(APPEND LUA_C_SRC ${_f})
    endif()
endforeach()

add_library(lua STATIC ${LUA_C_SRC})

# Lua is pure C.
set_target_properties(lua PROPERTIES
    LINKER_LANGUAGE C
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(lua PUBLIC ${LUA_SRC_DIR})

# Android is POSIX. This enables normal stdio/popen/time behavior in Lua.
target_compile_definitions(lua PRIVATE LUA_USE_POSIX)

# Make <lua.h> visible to suka.
target_include_directories(suka PRIVATE
    ${LUA_SRC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src
)

# IMPORTANT: use ONE keyword-style target_link_libraries for suka.
# Do not mix it with plain target_link_libraries(suka log android).
target_link_libraries(suka PRIVATE
    lua
    android
    log
)
