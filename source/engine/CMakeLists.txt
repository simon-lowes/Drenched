# The DSP engine. Plain C++20 with no JUCE dependency, so it can be unit
# tested, benchmarked and reused outside the plugin.

add_library(drenched_engine STATIC
    src/DirectConvolver.cpp)

target_include_directories(drenched_engine PUBLIC include)

target_compile_features(drenched_engine PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(drenched_engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(drenched_engine PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()
