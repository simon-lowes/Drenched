set(DRENCHED_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND DRENCHED_FORMATS AU)
endif()

# Pro Tools is 64-bit only and runs on macOS and Windows only.
if(DRENCHED_BUILD_AAX)
    if((APPLE OR WIN32) AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        if(DRENCHED_AAX_SDK)
            juce_set_aax_sdk_path("${DRENCHED_AAX_SDK}")
        endif()

        list(APPEND DRENCHED_FORMATS AAX)
    else()
        message(STATUS "AAX is built for 64-bit macOS and Windows only; skipping it here.")
    endif()
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

# The recorded spaces: library/library.json and one FLAC file per space (see library/README.md).
file(GLOB DRENCHED_LIBRARY_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/library/library.json"
    "${CMAKE_SOURCE_DIR}/library/ir/*.flac")

juce_add_binary_data(DrenchedLibrary
    NAMESPACE DrenchedLibrary
    HEADER_NAME DrenchedLibrary.h
    SOURCES ${DRENCHED_LIBRARY_FILES})

set_target_properties(DrenchedLibrary PROPERTIES POSITION_INDEPENDENT_CODE ON)

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
    # macOS gives the standalone app no input without a reason to show the person
    # (and may quit it); plug-ins use their host's permission.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Drenched plays your input through the reverb, and records rooms when you capture one."
    NEEDS_WEB_BROWSER TRUE
    NEEDS_WEBVIEW2 TRUE
    NEEDS_CURL TRUE
    VST3_CATEGORIES Fx Reverb
    AU_MAIN_TYPE kAudioUnitType_Effect
    AAX_CATEGORY AAX_ePlugInCategory_Reverb
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Drenched PRIVATE
    Library.cpp
    LicenceManager.cpp
    PluginProcessor.cpp
    PluginEditor.cpp
    WebView.cpp)

target_compile_definitions(Drenched PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(WIN32)
    # Chromium-based WebView2, linked statically (the loader), so the plugin needs no extra DLL.
    # Most Windows 10 and 11 PCs have the Evergreen runtime it talks to; without it the
    # window says how to get it (WebView.h), and so does the installer.
    target_compile_definitions(Drenched PUBLIC JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1)
endif()

# The activation server the plugin talks to. The local default suits development
# (npx wrangler dev in server/); release builds set the deployed address.
set(DRENCHED_LICENCE_SERVER "http://127.0.0.1:8787" CACHE STRING "Activation server address")
target_compile_definitions(Drenched PRIVATE DRENCHED_LICENCE_SERVER="${DRENCHED_LICENCE_SERVER}")

# Beta builds, for testers before sales start, have no trial or licence check.
# Release builds turn this off.
option(DRENCHED_BETA "Beta build: no trial or licence check" ON)
target_compile_definitions(Drenched PRIVATE DRENCHED_BETA=$<BOOL:${DRENCHED_BETA}>)

target_link_libraries(Drenched
    PRIVATE
        drenched_engine
        drenched_licence
        DrenchedLibrary
        DrenchedUI
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    # The 32-bit standalone app gets 4 GB of address space on 64-bit Windows, not 2.
    # (A plug-in gets whatever its host was linked with.)
    target_link_options(Drenched_Standalone PRIVATE /LARGEADDRESSAWARE)
endif()

if(APPLE AND TARGET Drenched_VST3)
    # JUCE signs the VST3 bundle (ad hoc) before writing its moduleinfo.json, which breaks
    # the bundle's seal, so macOS sees it as damaged. Sign it again once it is complete
    # (only if its signature no longer checks out).
    add_custom_command(TARGET Drenched_VST3 POST_BUILD
        COMMAND sh -c "codesign --verify \"$0\" 2>/dev/null || codesign --force --sign - \"$0\"" "$<TARGET_BUNDLE_DIR:Drenched_VST3>"
        VERBATIM)
endif()

if(DRENCHED_BUILD_CLAP)
    clap_juce_extensions_plugin(TARGET Drenched
        CLAP_ID "com.drenched.drenched"
        CLAP_FEATURES audio-effect reverb stereo surround)
endif()
