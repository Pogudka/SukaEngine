cmake_minimum_required(VERSION 3.22.1)
project(sukaengine LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Общие стандарты и флаги
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# RTTI и exceptions обязательны: ядро использует dynamic_cast и unique_ptr/throw
add_compile_options(-frtti -fexceptions)

# Корень данных приложения на устройстве (туда restructure() кладёт assets/ и projects/)
set(SUKA_PROJECT_ROOT "/data/data/com.sukaengine.app/files")

include(FetchContent)

# ---------------------------------------------------------------------------
# Lua 5.4 ( собираем из исходников как статическую C-библиотеку )
# ---------------------------------------------------------------------------
FetchContent_Declare(lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG        v5.4.7
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(lua)

file(GLOB LUA_CORE_SOURCES "${lua_SOURCE_DIR}/*.c")
# убираем точки входа с main()
list(REMOVE_ITEM LUA_CORE_SOURCES
    "${lua_SOURCE_DIR}/lua.c"
    "${lua_SOURCE_DIR}/luac.c"
    "${lua_SOURCE_DIR}/onelua.c")

add_library(lua_static STATIC ${LUA_CORE_SOURCES})
set_target_properties(lua_static PROPERTIES LINKER_LANGUAGE C)
target_include_directories(lua_static PUBLIC "${lua_SOURCE_DIR}")
target_compile_options(lua_static PRIVATE -w)   # чужой C-код без варнингов

# ---------------------------------------------------------------------------
# Box2D v2.4.1 (настоящая физика: контакты, трение, качение, сон тел)
# ---------------------------------------------------------------------------
FetchContent_Declare(box2d
    GIT_REPOSITORY https://github.com/erincatto/box2d.git
    GIT_TAG        v2.4.1
    GIT_SHALLOW    TRUE)
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_TESTBED    OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_DOCS       OFF CACHE BOOL "" FORCE)
set(BOX2D_SAMPLES          OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(box2d)

# ---------------------------------------------------------------------------
# Нативная библиотека движка
# ---------------------------------------------------------------------------
# Репозиторий: android/app/src/main/cpp  ->  корень репо на 4 уровня выше
get_filename_component(SUKA_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../" ABSOLUTE)

add_library(suka SHARED
    android_bridge.cpp)

target_include_directories(suka PRIVATE
    "${SUKA_REPO_ROOT}/src"          # все header-only модули ядра (GameApp.hpp и т.д.)
    "${lua_SOURCE_DIR}"
)

target_compile_definitions(suka PRIVATE
    PROJECT_ROOT="${SUKA_PROJECT_ROOT}"
)

target_link_libraries(suka
    android
    log
    lua_static
    Box2D
)

# Оптимизации для релиза, без них физика и рендер-строки медленные
target_compile_options(suka PRIVATE
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Debug>:-O0 -g>
)
