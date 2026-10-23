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
    AnalysisTests.cpp
    ClapCaptureTests.cpp
    SweepCaptureTests.cpp
    SurroundTests.cpp
    SurroundFileTests.cpp
    AmbisonicTests.cpp
    LicenceTests.cpp)

target_link_libraries(drenched_tests PRIVATE drenched_engine drenched_licence drenched_monocypher Catch2::Catch2WithMain)

include(CTest)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(drenched_tests)
add_subdirectory(plugin)
