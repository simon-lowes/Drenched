# The DSP engine. Plain C++20 with no JUCE dependency, so it can be unit
# tested, benchmarked and reused outside the plugin.

add_library(drenched_engine STATIC
    src/DirectConvolver.cpp
    src/DirectSound.cpp
    src/PartitionedConvolver.cpp
    src/ImpulseResponseSynth.cpp
    src/ConvolutionReverb.cpp
    src/KernelBuilder.cpp
    src/LivingTail.cpp
    src/RecordedResponse.cpp
    src/ResponseEnvelope.cpp
    src/SweepCapture.cpp
    src/SurroundResponse.cpp)

target_include_directories(drenched_engine PUBLIC include)

target_compile_features(drenched_engine PUBLIC cxx_std_20)

target_link_libraries(drenched_engine PRIVATE drenched_pffft drenched_r8brain)

find_package(Threads REQUIRED)
target_link_libraries(drenched_engine PUBLIC Threads::Threads)

set_target_properties(drenched_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(drenched_engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(drenched_engine PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()
