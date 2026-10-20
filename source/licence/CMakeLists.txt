# Licence numbers and activation codes. Plain C++20 with no JUCE dependency,
# so it can be unit tested on its own.

add_library(drenched_licence STATIC
    src/Licence.cpp)

target_include_directories(drenched_licence PUBLIC include)

target_compile_features(drenched_licence PUBLIC cxx_std_20)

target_link_libraries(drenched_licence PRIVATE drenched_monocypher)

set_target_properties(drenched_licence PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(drenched_licence PRIVATE /W4 /permissive-)
else()
    target_compile_options(drenched_licence PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()
