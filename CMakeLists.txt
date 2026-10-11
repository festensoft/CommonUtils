cmake_minimum_required(VERSION 4.1)

# Project definition
project(CommonUtils
   VERSION 1.0.0
   DESCRIPTION "Good Common library used all over the place"
   LANGUAGES CXX
)

# Enable CTest from the top-level so ctest can discover subdirectory tests.
include(CTest)

# Keep <source>/compile_commands.json pointing at the active preset's
# database so clangd (and anything else) finds it with no per-preset config.
if (PROJECT_IS_TOP_LEVEL AND CMAKE_EXPORT_COMPILE_COMMANDS)
    file(CREATE_LINK "${CMAKE_BINARY_DIR}/compile_commands.json"
         "${CMAKE_SOURCE_DIR}/compile_commands.json" SYMBOLIC)
endif()

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Warnings for this project's own targets only, delivered through an
# INTERFACE target that CommonUtils, the tests and the examples link
# PRIVATE-ly -- vendored FetchContent dependencies never see them. Kept out
# of CMAKE_CXX_FLAGS on purpose: presets that set their own CMAKE_CXX_FLAGS
# (the sanitizer presets) would otherwise silently replace them.
add_library(CommonUtils_warnings INTERFACE)
target_compile_options(CommonUtils_warnings INTERFACE
   -Wall
   -Wextra
   -Wpedantic
   -Wshadow
   -Wnon-virtual-dtor
   -Wold-style-cast
   -Wcast-align
   -Wconversion
   -Wsign-conversion
   -Wnull-dereference
   -Wdouble-promotion
   -Wformat=2
   # GCC-only; clang has no equivalent.
   $<$<CXX_COMPILER_ID:GNU>:-Wuseless-cast>
   # GCC 14+ only; an unknown -W flag is a hard error on older GCC.
   $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_GREATER_EQUAL:$<CXX_COMPILER_VERSION>,14>>:-Wnrvo>
)

# Run clang-tidy (using .clang-tidy) on this project's own sources as part
# of the build. This is a cached value so CMake presets can flip it to ON
# without editing this file; the default remains OFF for normal builds.
# CMAKE_CXX_CLANG_TIDY is set only after the vendored dependencies are
# available (below), so spdlog/googletest are never linted.
set(ENABLE_CLANG_TIDY OFF CACHE BOOL
    "Run clang-tidy on CommonUtils' own sources during the build"
)
if (ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
endif()

# Put every binary from this build in one directory (bin/), so tests and
# examples find the shared library next to them.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Dependencies: use a system/package-provided copy if find_package() locates
# one, otherwise fetch it (FIND_PACKAGE_ARGS, CMake 3.24+).
include(FetchContent)

# EXCLUDE_FROM_ALL: only the spdlog::spdlog_header_only INTERFACE target is
# used, so don't let spdlog's own CMakeLists add its compiled library to the
# default build.
FetchContent_Declare(spdlog
   GIT_REPOSITORY https://github.com/gabime/spdlog.git
   GIT_TAG        v1.15.1
   SYSTEM
   EXCLUDE_FROM_ALL
   FIND_PACKAGE_ARGS
)
FetchContent_MakeAvailable(spdlog)

if (BUILD_TESTING)
    FetchContent_Declare(googletest
       GIT_REPOSITORY https://github.com/google/googletest.git
       GIT_TAG        v1.18.0
       SYSTEM
       FIND_PACKAGE_ARGS NAMES GTest
    )
    FetchContent_MakeAvailable(googletest)
endif()

# Lint only this project's targets: set after the dependencies above are
# available so vendored code is never linted.
if (ENABLE_CLANG_TIDY)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

file(GLOB CommonUtil_source CONFIGURE_DEPENDS "src/*.cpp")

# StackTrace has one implementation file per platform (StackTrace_Posix.cpp /
# StackTrace_Win32.cpp), both implementing the same CommonUtils::StackTrace
# API declared in StackTrace.h. Exclude whichever one doesn't match this
# platform from the build.
if (WIN32)
    list(REMOVE_ITEM CommonUtil_source "${CMAKE_CURRENT_SOURCE_DIR}/src/StackTrace_Posix.cpp")
else()
    list(REMOVE_ITEM CommonUtil_source "${CMAKE_CURRENT_SOURCE_DIR}/src/StackTrace_Win32.cpp")
endif()

add_library(CommonUtils SHARED ${CommonUtil_source})
# Header-only spdlog: no spdlog .cpp files to compile/export, and no
# spdlog symbols leak into CommonUtils' own link interface. PRIVATE (not
# PUBLIC) because spdlog never appears in include/CommonUtils/ -- it's a
# genuine implementation detail confined to src/, so consumers get neither
# its include path nor its header-only compile-time cost.
target_link_libraries(CommonUtils PRIVATE spdlog::spdlog_header_only CommonUtils_warnings)
target_include_directories(CommonUtils PUBLIC include/CommonUtils)

if (WIN32)
    target_link_libraries(CommonUtils PRIVATE dbghelp)
endif()


if (BUILD_TESTING)
    add_subdirectory(tests)
endif()

option(COMMONUTILS_BUILD_EXAMPLES "Build the example programs" ON)
if (COMMONUTILS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
