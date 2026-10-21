cmake_minimum_required(VERSION 3.25)

project(Drenched VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

# 32-bit x86 builds (Windows Win32, Linux i686) are for older hosts and bridges.
# MSVC targets SSE2 there already; GCC and Clang default to x87 maths, which is
# slower, rounds differently and leaves PFFFT on its scalar fallback.
if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86|x86_64|AMD64)$")
    add_compile_options(-msse2 -mfpmath=sse)
endif()

option(DRENCHED_BUILD_TESTS "Build the unit tests" ON)
option(DRENCHED_BUILD_CLAP "Build the CLAP format" ON)

include(cmake/Dependencies.cmake)

add_subdirectory(source/engine)
add_subdirectory(source/licence)
add_subdirectory(source/plugin)
add_subdirectory(tools/library)

if(DRENCHED_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
