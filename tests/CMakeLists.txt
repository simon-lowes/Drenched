add_executable(drenched_tests
    AllocationCounter.cpp
    MixTests.cpp
    DirectConvolverTests.cpp
    DirectSoundTests.cpp
    PartitionedConvolverTests.cpp
    ImpulseResponseSynthTests.cpp
    ConvolutionReverbTests.cpp
    LivingTailTests.cpp
    RecordedResponseTests.cpp
    ResponseEnvelopeTests.cpp
    ReverbCloudTests.cpp
    RoomMatchTests.cpp
    RoomDescriptionTests.cpp
    SceneNetTests.cpp
    SpeakerTrimTests.cpp
    WetEqualiserTests.cpp
    SmoothChangeTests.cpp
    AnalysisTests.cpp
    ClapCaptureTests.cpp
    SweepCaptureTests.cpp
    SurroundTests.cpp
    SurroundFileTests.cpp
    AmbisonicTests.cpp
    SofaTests.cpp
    LicenceTests.cpp)

target_link_libraries(drenched_tests PRIVATE drenched_engine drenched_licence drenched_monocypher Catch2::Catch2WithMain)

include(CTest)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(drenched_tests ADD_TAGS_AS_LABELS)

# Tests that measure seconds ([.timing], so left out above) fail when the machine is busy
# rather than when the code is slow. They get an entry of their own, labelled timing, so
# CI runs them apart from the rest (ctest -LE timing) and tries them again before failing
# (ctest -L timing --repeat until-pass:3).
add_test(NAME "Timing: CPU comparisons" COMMAND drenched_tests "[timing]")
set_tests_properties("Timing: CPU comparisons" PROPERTIES LABELS timing)

add_subdirectory(plugin)

# SOFA files from libmysofa's own test set, read where FetchContent put them; small files
# made for these tests (tests/data/README.md); and the licence vectors the activation
# server's tests read too, so the two can't drift apart.
target_compile_definitions(drenched_tests PRIVATE
    DRENCHED_SOFA_TEST_DIR="${mysofa_SOURCE_DIR}/tests"
    DRENCHED_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    DRENCHED_SCENE_MODEL="${PROJECT_SOURCE_DIR}/library/scene/places365.drsn"
    DRENCHED_LICENCE_VECTORS="${PROJECT_SOURCE_DIR}/server/test/vectors.json")

# What the engine costs to build and play; run it by hand (see Benchmark.cpp).
add_executable(drenched_bench Benchmark.cpp)
target_link_libraries(drenched_bench PRIVATE drenched_engine)
