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
