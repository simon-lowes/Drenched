add_executable(drenched_tests
    AllocationCounter.cpp
    MixTests.cpp
    DirectConvolverTests.cpp
    PartitionedConvolverTests.cpp
    ImpulseResponseSynthTests.cpp
    ConvolutionReverbTests.cpp
    LivingTailTests.cpp
    RecordedResponseTests.cpp
    SweepCaptureTests.cpp)

target_link_libraries(drenched_tests PRIVATE drenched_engine Catch2::Catch2WithMain)

include(CTest)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(drenched_tests)
