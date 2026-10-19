set(DRENCHED_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND DRENCHED_FORMATS AU)
endif()

juce_add_plugin(Drenched
    PRODUCT_NAME "Drenched"
    COMPANY_NAME "Drenched"
    BUNDLE_ID "com.drenched.drenched"
    PLUGIN_MANUFACTURER_CODE Drnc
    PLUGIN_CODE Drn1
    FORMATS ${DRENCHED_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Reverb
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Drenched PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp)

target_compile_definitions(Drenched PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Drenched
    PRIVATE
        drenched_engine
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(DRENCHED_BUILD_CLAP)
    clap_juce_extensions_plugin(TARGET Drenched
        CLAP_ID "com.drenched.drenched"
        CLAP_FEATURES audio-effect reverb stereo surround)
endif()
