# Tests that load the whole plugin: saved sessions from every released version must load
# and sound the same in this one.
add_executable(drenched_plugin_tests
    Main.cpp
    SessionTests.cpp)

target_link_libraries(drenched_plugin_tests PRIVATE Drenched Catch2::Catch2)
target_include_directories(drenched_plugin_tests PRIVATE
    $<TARGET_PROPERTY:Drenched,INCLUDE_DIRECTORIES>
    ${PROJECT_SOURCE_DIR}/source/plugin
    ${PROJECT_SOURCE_DIR}/tests)
target_compile_definitions(drenched_plugin_tests PRIVATE
    $<TARGET_PROPERTY:Drenched,COMPILE_DEFINITIONS>
    DRENCHED_SESSIONS_DIR="${PROJECT_SOURCE_DIR}/tests/sessions")

catch_discover_tests(drenched_plugin_tests)
