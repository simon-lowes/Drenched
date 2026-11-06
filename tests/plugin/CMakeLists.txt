# Tests that load the whole plugin: saved sessions from every released version must load
# and sound the same in this one, and features that live in the plugin rather than the engine.
add_executable(drenched_plugin_tests
    Main.cpp
    ${PROJECT_SOURCE_DIR}/tests/AllocationCounter.cpp
    SessionTests.cpp
    MatchTests.cpp
    ChannelLayoutTests.cpp
    SceneTests.cpp
    DisplayTests.cpp
    ReportTests.cpp
    DamagedInputTests.cpp
    ThreadStressTests.cpp
    HostQuirksTests.cpp
    LargeFileTests.cpp
    MovedFileTests.cpp
    DropTests.cpp
    CaptureSaveTests.cpp
    RecordingCacheTests.cpp
    ClickTests.cpp
    AudioThreadTests.cpp
    HostTextTests.cpp
    HeldControlsTests.cpp
    ParameterWatchTests.cpp
    MessageLoopTests.cpp
    PageMessageTests.cpp
    CrashReportTests.cpp
    WebViewTests.cpp)

target_link_libraries(drenched_plugin_tests PRIVATE Drenched Catch2::Catch2)
target_include_directories(drenched_plugin_tests PRIVATE
    $<TARGET_PROPERTY:Drenched,INCLUDE_DIRECTORIES>
    ${PROJECT_SOURCE_DIR}/source/plugin
    ${PROJECT_SOURCE_DIR}/tests)
target_compile_definitions(drenched_plugin_tests PRIVATE
    $<TARGET_PROPERTY:Drenched,COMPILE_DEFINITIONS>
    DRENCHED_SESSIONS_DIR="${PROJECT_SOURCE_DIR}/tests/sessions"
    DRENCHED_UI_TESTS_DIR="${PROJECT_SOURCE_DIR}/tests/ui"
    DRENCHED_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")

# A hang (for example waiting on a message loop) fails in two minutes, not CTest's 25.
catch_discover_tests(drenched_plugin_tests ADD_TAGS_AS_LABELS PROPERTIES TIMEOUT 120)

# The trial and the licence: LicenceManager with a fake clock, file and activation server,
# and the plugin muting its reverb once the trial is over. A beta build leaves all of that
# out (DRENCHED_BETA), so these tests exist only in a build configured the way a release
# is, with -DDRENCHED_BETA=OFF, where they test the very code that ships: the same Drenched
# target, not a copy compiled differently. CI builds and runs just this target in such a
# build (the licence job in .github/workflows/build.yml). They sign activation codes with
# the published test key, so that build also needs -DDRENCHED_ALLOW_TEST_LICENCE=ON.
if(NOT DRENCHED_BETA)
    add_executable(drenched_licence_tests
        LicenceMain.cpp
        LicenceManagerTests.cpp
        LicenceMuteTests.cpp)

    target_link_libraries(drenched_licence_tests PRIVATE Drenched drenched_licence drenched_monocypher Catch2::Catch2)
    target_include_directories(drenched_licence_tests PRIVATE
        $<TARGET_PROPERTY:Drenched,INCLUDE_DIRECTORIES>
        ${PROJECT_SOURCE_DIR}/source/plugin
        ${PROJECT_SOURCE_DIR}/tests)
    target_compile_definitions(drenched_licence_tests PRIVATE
        $<TARGET_PROPERTY:Drenched,COMPILE_DEFINITIONS>)

    catch_discover_tests(drenched_licence_tests PROPERTIES TIMEOUT 120 LABELS licence)
endif()
