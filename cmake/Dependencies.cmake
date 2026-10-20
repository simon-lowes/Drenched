# Third-party dependencies, all pinned. Every one is licensed for closed-source
# commercial use (see NOTICE.md). Never add GPL code here.

include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# JUCE: plugin wrappers, GUI, platform layer. Used under the JUCE Starter or a
# paid JUCE licence, never the AGPL option.
FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(juce)

if(DRENCHED_BUILD_CLAP)
    # CLAP wrapper for JUCE plugins (MIT).
    FetchContent_Declare(clap_juce_extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG        7adee3a1bd4684d4caa5601100e364abccff4b4d
        GIT_SUBMODULES_RECURSE TRUE)
    FetchContent_MakeAvailable(clap_juce_extensions)
endif()

if(DRENCHED_BUILD_TESTS)
    # Catch2 v3 (BSL-1.0).
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.9.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)
endif()

# PFFFT (BSD-style FFTPACK licence): the SIMD real FFT behind the partitioned
# convolver. Only pffft.c and pffft_common.c are compiled; the project's own
# CMakeLists (benchmarks, other FFT libraries) is deliberately not used.
FetchContent_Declare(pffft
    GIT_REPOSITORY https://github.com/marton78/pffft.git
    GIT_TAG        aa16fd3db58de4ba5dae8b0438440bb9da46b6fa
    SOURCE_SUBDIR  do-not-add-subdirectory)
FetchContent_MakeAvailable(pffft)

add_library(drenched_pffft STATIC
    ${pffft_SOURCE_DIR}/src/pffft.c
    ${pffft_SOURCE_DIR}/src/pffft_common.c)
target_include_directories(drenched_pffft PUBLIC ${pffft_SOURCE_DIR}/include PRIVATE ${pffft_SOURCE_DIR}/include/pffft)
target_compile_definitions(drenched_pffft PRIVATE _USE_MATH_DEFINES PUBLIC PFFFT_STATIC_DEFINE)
set_target_properties(drenched_pffft PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_definitions(drenched_pffft PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# r8brain-free-src (MIT): high-quality offline resampling of loaded impulse
# responses to the session rate. Its FFT is Takuya Ooura's (free for any use).
# Only r8bbase.cpp is compiled; its benchmarks and DLL are not used.
FetchContent_Declare(r8brain
    GIT_REPOSITORY https://github.com/avaneev/r8brain-free-src.git
    GIT_TAG        3c930bf6825c0cfea4a813210c83b1a650c820b5 # version-6.5
    SOURCE_SUBDIR  do-not-add-subdirectory)
FetchContent_MakeAvailable(r8brain)

add_library(drenched_r8brain STATIC ${r8brain_SOURCE_DIR}/r8bbase.cpp)
target_include_directories(drenched_r8brain SYSTEM PUBLIC ${r8brain_SOURCE_DIR})
target_compile_features(drenched_r8brain PUBLIC cxx_std_20)
set_target_properties(drenched_r8brain PROPERTIES POSITION_INDEPENDENT_CODE ON)
find_package(Threads REQUIRED)
target_link_libraries(drenched_r8brain PUBLIC Threads::Threads)
if(WIN32)
    # r8brain's headers include windows.h; keep its min and max macros out of our code.
    target_compile_definitions(drenched_r8brain PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# Monocypher 4.0.2 (BSD-2-Clause or CC0-1.0): checks the Ed25519 signature on
# activation codes. Only the core and its Ed25519 (SHA-512) option are compiled.
FetchContent_Declare(monocypher
    GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git
    GIT_TAG        0d85f98c9d9b0227e42cf795cb527dff372b40a4 # 4.0.2
    SOURCE_SUBDIR  do-not-add-subdirectory)
FetchContent_MakeAvailable(monocypher)

add_library(drenched_monocypher STATIC
    ${monocypher_SOURCE_DIR}/src/monocypher.c
    ${monocypher_SOURCE_DIR}/src/optional/monocypher-ed25519.c)
target_include_directories(drenched_monocypher SYSTEM PUBLIC
    ${monocypher_SOURCE_DIR}/src
    ${monocypher_SOURCE_DIR}/src/optional)
set_target_properties(drenched_monocypher PROPERTIES POSITION_INDEPENDENT_CODE ON)
