cmake_minimum_required(VERSION 3.22)

# Universal binary on macOS (Apple Silicon + Intel). Override with -DCMAKE_OSX_ARCHITECTURES=arm64
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(GHOSTCUT VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(GHOSTCUT_BUILD_TESTS "Build the GhostcutTests console app" ON)
option(GHOSTCUT_COPY_PLUGIN "Copy built plugins into the system plugin folders" OFF)
set(GHOSTCUT_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (leave empty to download JUCE 8.0.15)")

# ---------------------------------------------------------------- JUCE
if(GHOSTCUT_JUCE_DIR)
    add_subdirectory(${GHOSTCUT_JUCE_DIR} JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------- shared sources
set(GHOSTCUT_CORE_SOURCES
    Source/Core/Fragment.h
    Source/Core/SourceSnapshot.h
    Source/Core/AudioThreadExchange.h
    Source/Core/Session.h
    Source/Core/Session.cpp
    Source/Audio/AudioImporter.h
    Source/Audio/AudioImporter.cpp
    Source/Audio/AudioExporter.h
    Source/Audio/AudioExporter.cpp
    Source/Audio/SamplerEngine.h
    Source/Audio/SamplerEngine.cpp
    Source/Analysis/PhraseSegmenter.h
    Source/Analysis/PhraseSegmenter.cpp)

set(GHOSTCUT_UI_SOURCES
    Source/UI/GhostLookAndFeel.h
    Source/UI/GhostLookAndFeel.cpp
    Source/UI/WaveformView.h
    Source/UI/WaveformView.cpp
    Source/UI/FragmentBrowser.h
    Source/UI/FragmentBrowser.cpp
    Source/UI/DiscoverPanel.h
    Source/UI/DiscoverPanel.cpp)

set(GHOSTCUT_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1      # JUCE's built-in MP3 decoder (MP3 patents have expired)
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

# ---------------------------------------------------------------- plugin
if(APPLE)
    set(GHOSTCUT_FORMATS VST3 AU Standalone)
else()
    set(GHOSTCUT_FORMATS VST3 Standalone)
endif()

juce_add_plugin(GHOSTCUT
    PRODUCT_NAME                "GHOSTCUT"
    VERSION                     ${PROJECT_VERSION}
    COMPANY_NAME                "TRIPLE SEVEN AUDIO"
    COMPANY_WEBSITE             "https://triplesevenaudio.com"
    BUNDLE_ID                   com.triplesevenaudio.ghostcut
    PLUGIN_MANUFACTURER_CODE    T7Au
    PLUGIN_CODE                 Ghct
    FORMATS                     ${GHOSTCUT_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             Instrument Sampler
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD     ${GHOSTCUT_COPY_PLUGIN})

target_sources(GHOSTCUT PRIVATE
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
    ${GHOSTCUT_CORE_SOURCES}
    ${GHOSTCUT_UI_SOURCES})

target_compile_definitions(GHOSTCUT PUBLIC ${GHOSTCUT_COMMON_DEFINITIONS})

target_link_libraries(GHOSTCUT
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- tests
if(GHOSTCUT_BUILD_TESTS)
    juce_add_console_app(GhostcutTests PRODUCT_NAME "GhostcutTests")
    target_sources(GhostcutTests PRIVATE Tests/TestMain.cpp ${GHOSTCUT_CORE_SOURCES})
    target_compile_definitions(GhostcutTests PRIVATE ${GHOSTCUT_COMMON_DEFINITIONS}
        JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_UNIT_TESTS=1)
    target_include_directories(GhostcutTests PRIVATE Source)
    target_link_libraries(GhostcutTests
        PRIVATE
            juce::juce_audio_basics
            juce::juce_audio_formats
            juce::juce_data_structures
            juce::juce_events
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
