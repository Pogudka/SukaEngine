cmake_minimum_required(VERSION 3.22.1)
project(moben LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Путь до корня репозитория:
# android/app/src/main/cpp -> ../../../../../ -> корень репо
get_filename_component(MOBEN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)

message(STATUS "MOBEN_ROOT=${MOBEN_ROOT}")

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# -------------------------
# Lua 5.4.6
# -------------------------
FetchContent_Declare(
    lua
    URL https://www.lua.org/ftp/lua-5.4.6.tar.gz
)

FetchContent_Populate(lua)

file(GLOB LUA_ALL_SOURCES CONFIGURE_DEPENDS "${lua_SOURCE_DIR}/src/*.c")
list(FILTER LUA_ALL_SOURCES EXCLUDE REGEX "(^|/)(lua|luac|onelua)\\.c$")

add_library(lua_static STATIC ${LUA_ALL_SOURCES})

target_include_directories(lua_static PUBLIC "${lua_SOURCE_DIR}/src")
target_compile_definitions(lua_static PUBLIC LUA_USE_POSIX)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(lua_static PRIVATE -w)
endif()

# -------------------------
# Box2D v2.4.1
# -------------------------
FetchContent_Declare(
    box2d
    URL https://github.com/erincatto/box2d/archive/refs/tags/v2.4.1.zip
)

set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(box2d)

# -------------------------
# miniaudio
# -------------------------
FetchContent_Declare(
    miniaudio
    URL https://github.com/mackron/miniaudio/archive/refs/tags/0.11.21.zip
)

FetchContent_Populate(miniaudio)

# -------------------------
# Основная библиотека
# -------------------------
add_library(
    moben
    SHARED

    # android_bridge.cpp лежит рядом с этим CMakeLists.txt
    "${CMAKE_CURRENT_SOURCE_DIR}/android_bridge.cpp"

    # Sound.cpp лежит в корне репо в src/
    "${MOBEN_ROOT}/src/Sound.cpp"
)

target_include_directories(
    moben
    PRIVATE
    "${MOBEN_ROOT}/src"
    "${lua_SOURCE_DIR}/src"
    "${box2d_SOURCE_DIR}/include"
    "${miniaudio_SOURCE_DIR}"
)

target_link_libraries(
    moben
    PRIVATE
    lua_static
    box2d
)

target_compile_definitions(
    moben
    PRIVATE
    PROJECT_ROOT=\"/data/data/com.moben.app/files\"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(moben PRIVATE -frtti -fexceptions)
endif()

find_package(Threads REQUIRED)
target_link_libraries(moben PRIVATE Threads::Threads)

if(ANDROID)
    find_library(log-lib log)
    find_library(android-lib android)
    find_library(OpenSLES-lib OpenSLES)
    find_library(atomic-lib atomic)

    target_link_libraries(
        moben
        PRIVATE
        ${log-lib}
        ${android-lib}
        ${OpenSLES-lib}
    )

    if(atomic-lib)
        target_link_libraries(moben PRIVATE ${atomic-lib})
    endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR ANDROID)
    target_link_libraries(moben PRIVATE m)
endif()
