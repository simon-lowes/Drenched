# Measures the bundled recordings into library/library.json. Not part of the
# normal build: cmake --build build --target drenched_analyse_library
juce_add_console_app(drenched_analyse_library PRODUCT_NAME "drenched_analyse_library")

target_sources(drenched_analyse_library PRIVATE AnalyseLibrary.cpp)

target_compile_definitions(drenched_analyse_library PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(drenched_analyse_library
    PRIVATE
        drenched_engine
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

set_target_properties(drenched_analyse_library PROPERTIES EXCLUDE_FROM_ALL TRUE)
