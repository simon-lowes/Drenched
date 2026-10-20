set(DRENCHED_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND DRENCHED_FORMATS AU)
endif()

# The interface: one web page with twelve skins, embedded in the binary and served to the
# web view from memory. Every file under source/ui ships; names must stay unique across
# the tree because the editor looks resources up by file name.
file(GLOB_RECURSE DRENCHED_UI_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/source/ui/index.html"
    "${CMAKE_SOURCE_DIR}/source/ui/css/*"
    "${CMAKE_SOURCE_DIR}/source/ui/js/*"
    "${CMAKE_SOURCE_DIR}/source/ui/fonts/*.woff2"
    "${CMAKE_SOURCE_DIR}/source/ui/spaces/*")

juce_add_binary_data(DrenchedUI
    NAMESPACE DrenchedUI
    HEADER_NAME DrenchedUI.h
    SOURCES ${DRENCHED_UI_FILES})

set_target_properties(DrenchedUI PROPERTIES POSITION_INDEPENDENT_CODE ON)

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
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    NEEDS_WEB_BROWSER TRUE
    NEEDS_WEBVIEW2 TRUE
    VST3_CATEGORIES Fx Reverb
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Drenched PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp)

target_compile_definitions(Drenched PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(WIN32)
    # Chromium-based WebView2, linked statically (the loader), so the plugin needs no extra DLL.
    # The Evergreen runtime it talks to ships with Windows 10 and 11.
    target_compile_definitions(Drenched PUBLIC JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1)
endif()

target_link_libraries(Drenched
    PRIVATE
        drenched_engine
        DrenchedUI
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(DRENCHED_BUILD_CLAP)
    clap_juce_extensions_plugin(TARGET Drenched
        CLAP_ID "com.drenched.drenched"
        CLAP_FEATURES audio-effect reverb stereo surround)
endif()
