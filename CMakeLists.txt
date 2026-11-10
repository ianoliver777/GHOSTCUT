cmake_minimum_required(VERSION 3.22)

# Universal binary on macOS (Apple Silicon + Intel). Override with -DCMAKE_OSX_ARCHITECTURES=arm64
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(GHOSTCUT VERSION 0.9.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(GHOSTCUT_BUILD_TESTS "Build the GhostcutTests console app" ON)
option(GHOSTCUT_COPY_PLUGIN "Copy built plugins into the system plugin folders" OFF)
set(GHOSTCUT_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (leave empty to download JUCE 8.0.15)")
option(GHOSTCUT_WITH_ONNX "Bundle ONNX Runtime for ML vocal separation" ON)
set(GHOSTCUT_ORT_DIR "" CACHE PATH "Path to an extracted ONNX Runtime release (leave empty to download)")
set(GHOSTCUT_ORT_VERSION "1.20.1")

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

# ---------------------------------------------------------------- ONNX Runtime (MIT)
# Only the C API header is used at compile time; the shared library is loaded at run
# time from inside the plugin bundle (see Source/Analysis/OnnxRuntime.cpp).
if(GHOSTCUT_WITH_ONNX)
    if(NOT GHOSTCUT_ORT_DIR)
        if(APPLE)
            set(_ort_pkg "onnxruntime-osx-universal2-${GHOSTCUT_ORT_VERSION}.tgz")
        elseif(WIN32)
            set(_ort_pkg "onnxruntime-win-x64-${GHOSTCUT_ORT_VERSION}.zip")
        else()
            set(_ort_pkg "onnxruntime-linux-x64-${GHOSTCUT_ORT_VERSION}.tgz")
        endif()
        include(FetchContent)
        FetchContent_Declare(onnxruntime
            URL https://github.com/microsoft/onnxruntime/releases/download/v${GHOSTCUT_ORT_VERSION}/${_ort_pkg}
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            SOURCE_SUBDIR _no_cmake_here_)
        FetchContent_MakeAvailable(onnxruntime)
        set(GHOSTCUT_ORT_DIR ${onnxruntime_SOURCE_DIR})
    endif()
    if(APPLE)
        file(GLOB _ort_libs "${GHOSTCUT_ORT_DIR}/lib/libonnxruntime.*.*.*.dylib")
        set(GHOSTCUT_ORT_RUNTIME_NAME "libonnxruntime.dylib")
    elseif(WIN32)
        set(_ort_libs "${GHOSTCUT_ORT_DIR}/lib/onnxruntime.dll")
        set(GHOSTCUT_ORT_RUNTIME_NAME "onnxruntime.dll")
    else()
        file(GLOB _ort_libs "${GHOSTCUT_ORT_DIR}/lib/libonnxruntime.so.*.*.*")
        set(GHOSTCUT_ORT_RUNTIME_NAME "libonnxruntime.so")
    endif()
    list(GET _ort_libs 0 GHOSTCUT_ORT_RUNTIME)
    message(STATUS "GHOSTCUT: ONNX Runtime ${GHOSTCUT_ORT_RUNTIME}")
endif()

# Copies the ORT runtime next to a target's binary (Frameworks/ inside mac bundles).
function(ghostcut_bundle_ort target)
    if(NOT GHOSTCUT_WITH_ONNX OR NOT TARGET ${target})
        return()
    endif()
    get_target_property(_bundle ${target} BUNDLE)
    if(APPLE AND _bundle)
        set(_dest "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Frameworks")
    else()
        set(_dest "$<TARGET_FILE_DIR:${target}>")
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_dest}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GHOSTCUT_ORT_RUNTIME}" "${_dest}/${GHOSTCUT_ORT_RUNTIME_NAME}"
        VERBATIM)
    if(APPLE AND _bundle)
        # adding a file invalidates JUCE's ad-hoc signature: sign again (Phase 6 replaces this with Developer ID)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND codesign --force --sign - "${_dest}/${GHOSTCUT_ORT_RUNTIME_NAME}"
            COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:${target}>"
            VERBATIM)
    endif()
endfunction()

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
    Source/Analysis/PhraseSegmenter.cpp
    Source/Analysis/FeatureExtractor.h
    Source/Analysis/FeatureExtractor.cpp
    Source/Analysis/FragmentRanker.h
    Source/Analysis/FragmentRanker.cpp
    Source/Analysis/SearchQuery.h
    Source/Analysis/SearchQuery.cpp
    Source/Analysis/QueryInterpreter.h
    Source/Analysis/ReferenceMatcher.h
    Source/Analysis/ReferenceMatcher.cpp
    Source/Analysis/Stft.h
    Source/Analysis/Stft.cpp
    Source/Analysis/OnnxRuntime.h
    Source/Analysis/OnnxRuntime.cpp
    Source/Analysis/VocalSeparator.h
    Source/Analysis/VocalSeparator.cpp
    Source/Generation/Random.h
    Source/Generation/ScaleQuantizer.h
    Source/Generation/ScaleQuantizer.cpp
    Source/Generation/Pattern.h
    Source/Generation/Pattern.cpp
    Source/Generation/PatternGenerator.h
    Source/Generation/PatternGenerator.cpp
    Source/Generation/ChopRenderer.h
    Source/Generation/ChopRenderer.cpp
    Source/Generation/PatternExport.h
    Source/Generation/PatternExport.cpp
    Source/DSP/Biquad.h
    Source/DSP/Effects.h
    Source/DSP/Effects.cpp)

set(GHOSTCUT_UI_SOURCES
    Source/UI/GhostLookAndFeel.h
    Source/UI/GhostLookAndFeel.cpp
    Source/UI/WaveformView.h
    Source/UI/WaveformView.cpp
    Source/UI/FragmentBrowser.h
    Source/UI/FragmentBrowser.cpp
    Source/UI/DiscoverPanel.h
    Source/UI/DiscoverPanel.cpp
    Source/UI/PatternView.h
    Source/UI/PatternView.cpp
    Source/UI/GeneratePanel.h
    Source/UI/GeneratePanel.cpp
    Source/UI/SequencePanel.h
    Source/UI/SequencePanel.cpp
    Source/UI/EffectsPanel.h
    Source/UI/EffectsPanel.cpp
    Source/UI/PresetBar.h
    Source/UI/PresetBar.cpp)

set(GHOSTCUT_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1      # JUCE's built-in MP3 decoder (MP3 patents have expired)
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    GHOSTCUT_HAS_ORT=$<BOOL:${GHOSTCUT_WITH_ONNX}>)

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
    Source/FxParameters.h
    Source/FxParameters.cpp
    Source/Presets/PresetManager.h
    Source/Presets/PresetManager.cpp
    ${GHOSTCUT_CORE_SOURCES}
    ${GHOSTCUT_UI_SOURCES})

target_compile_definitions(GHOSTCUT PUBLIC ${GHOSTCUT_COMMON_DEFINITIONS})
target_include_directories(GHOSTCUT PRIVATE ThirdParty/signalsmith-linear/include)
if(MSVC)
    target_compile_options(GHOSTCUT PRIVATE /bigobj)   # Signalsmith template depth
endif()
if(APPLE)
    # Signalsmith Linear uses Accelerate for its FFT on Apple platforms
    target_compile_definitions(GHOSTCUT PRIVATE SIGNALSMITH_USE_ACCELERATE ACCELERATE_NEW_LAPACK)
    target_link_libraries(GHOSTCUT PRIVATE "-framework Accelerate")
endif()
if(GHOSTCUT_WITH_ONNX)
    target_include_directories(GHOSTCUT PRIVATE "${GHOSTCUT_ORT_DIR}/include")
endif()

target_link_libraries(GHOSTCUT
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_cryptography
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- tests
if(GHOSTCUT_BUILD_TESTS)
    juce_add_console_app(GhostcutTests PRODUCT_NAME "GhostcutTests")
    target_sources(GhostcutTests PRIVATE Tests/TestMain.cpp ${GHOSTCUT_CORE_SOURCES}
        Source/FxParameters.cpp Source/Presets/PresetManager.cpp)
    target_compile_definitions(GhostcutTests PRIVATE ${GHOSTCUT_COMMON_DEFINITIONS}
        JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_UNIT_TESTS=1)
    target_include_directories(GhostcutTests PRIVATE Source ThirdParty/signalsmith-linear/include)
    if(MSVC)
        target_compile_options(GhostcutTests PRIVATE /bigobj)
    endif()
    if(APPLE)
        target_compile_definitions(GhostcutTests PRIVATE SIGNALSMITH_USE_ACCELERATE ACCELERATE_NEW_LAPACK)
        target_link_libraries(GhostcutTests PRIVATE "-framework Accelerate")
    endif()
    if(GHOSTCUT_WITH_ONNX)
        target_include_directories(GhostcutTests PRIVATE "${GHOSTCUT_ORT_DIR}/include")
    endif()
    target_link_libraries(GhostcutTests
        PRIVATE
            juce::juce_audio_basics
            juce::juce_audio_formats
            juce::juce_data_structures
            juce::juce_events
            juce::juce_cryptography
            juce::juce_dsp
            juce::juce_audio_processors
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------- bundle ONNX Runtime
foreach(_t GHOSTCUT_VST3 GHOSTCUT_AU GHOSTCUT_Standalone GhostcutTests)
    ghostcut_bundle_ort(${_t})
endforeach()
