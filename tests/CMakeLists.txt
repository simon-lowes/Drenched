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
catch_discover_tests(drenched_tests)
add_subdirectory(plugin)

# SOFA files from libmysofa's own test set, read where FetchContent put them.
target_compile_definitions(drenched_tests PRIVATE DRENCHED_SOFA_TEST_DIR="${mysofa_SOURCE_DIR}/tests")

# What the engine costs to build and play; run it by hand (see Benchmark.cpp).
add_executable(drenched_bench Benchmark.cpp)
target_link_libraries(drenched_bench PRIVATE drenched_engine)
