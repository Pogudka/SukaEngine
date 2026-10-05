cmake_minimum_required(VERSION 3.22.1)
project(sukaengine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-frtti -fexceptions)

set(SUKA_PROJECT_ROOT "/data/data/com.sukaengine.app/files")

include(FetchContent)

# Lua 5.4
FetchContent_Declare(lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG        v5.4.7
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(lua)

file(GLOB LUA_CORE_SOURCES "${lua_SOURCE_DIR}/*.c")
list(REMOVE_ITEM LUA_CORE_SOURCES
    "${lua_SOURCE_DIR}/lua.c"
    "${lua_SOURCE_DIR}/luac.c"
    "${lua_SOURCE_DIR}/onelua.c")

add_library(lua_static STATIC ${LUA_CORE_SOURCES})
set_target_properties(lua_static PROPERTIES LINKER_LANGUAGE C)
target_include_directories(lua_static PUBLIC "${lua_SOURCE_DIR}")
target_compile_options(lua_static PRIVATE -w)

# Box2D v2.4.1
FetchContent_Declare(box2d
    GIT_REPOSITORY https://github.com/erincatto/box2d.git
    GIT_TAG        v2.4.1
    GIT_SHALLOW    TRUE)
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_TESTBED    OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_DOCS       OFF CACHE BOOL "" FORCE)
set(BOX2D_SAMPLES          OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(box2d)

# Корень репозитория: android/app/src/main/cpp -> ../../../../../ -> корень
get_filename_component(SUKA_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
get_filename_component(SUKA_REPO_ROOT2 "${SUKA_REPO_ROOT}/.." ABSOLUTE)

add_library(suka SHARED
    android_bridge.cpp)

target_include_directories(suka PRIVATE
    "${SUKA_REPO_ROOT2}/src"
    "${lua_SOURCE_DIR}"
)

target_compile_definitions(suka PRIVATE
    PROJECT_ROOT="${SUKA_PROJECT_ROOT}"
)

# Box2D: пробуем оба варианта имени таргета (Box2D или box2d)
if(TARGET Box2D)
    target_link_libraries(suka android log lua_static Box2D)
elseif(TARGET box2d)
    target_link_libraries(suka android log lua_static box2d)
else()
    message(WARNING "Box2D target not found, linking may fail")
    target_link_libraries(suka android log lua_static)
endif()

target_compile_options(suka PRIVATE
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Debug>:-O0 -g>
)
