cmake_minimum_required(VERSION 3.25)

project(Drenched VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

option(DRENCHED_BUILD_TESTS "Build the unit tests" ON)
option(DRENCHED_BUILD_CLAP "Build the CLAP format" ON)

include(cmake/Dependencies.cmake)

add_subdirectory(source/engine)
add_subdirectory(source/licence)
add_subdirectory(source/plugin)

if(DRENCHED_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
