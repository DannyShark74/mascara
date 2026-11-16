cmake_minimum_required(VERSION 3.22)
project(Mascara VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MASCARA_BUILD_TESTS "Build the offline engine test" OFF)
set(MASCARA_JUCE_DIR "" CACHE PATH "Local JUCE checkout. If empty, JUCE is downloaded.")

if(MASCARA_JUCE_DIR)
    add_subdirectory(${MASCARA_JUCE_DIR} juce EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(APPLE)
    set(MASCARA_FORMATS VST3 AU)
else()
    set(MASCARA_FORMATS VST3)
endif()

juce_add_plugin(Mascara
    PRODUCT_NAME "Mascara"
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "Danny Shark"
    BUNDLE_ID com.dannyshark.mascara
    PLUGIN_MANUFACTURER_CODE Dnsh
    PLUGIN_CODE Masc
    PLUGIN_NAME "Mascara"
    DESCRIPTION "Monophonic voice pitch and formant manipulation"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Pitch Shift
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    FORMATS ${MASCARA_FORMATS})

target_sources(Mascara PRIVATE
    source/Engine.cpp
    source/PluginProcessor.cpp
    source/PluginEditor.cpp)
target_include_directories(Mascara PRIVATE source)
target_compile_definitions(Mascara PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(Mascara
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

if(MASCARA_BUILD_TESTS)
    add_executable(MascaraTest tools/test.cpp source/Engine.cpp)
    target_include_directories(MascaraTest PRIVATE source)
endif()

if(MASCARA_BUILD_TESTS)
    juce_add_console_app(MascaraHostCheck PRODUCT_NAME "MascaraHostCheck")
    target_sources(MascaraHostCheck PRIVATE tools/hostcheck.cpp source/Engine.cpp source/PluginProcessor.cpp source/PluginEditor.cpp)
    target_include_directories(MascaraHostCheck PRIVATE source)
    target_compile_definitions(MascaraHostCheck PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JucePlugin_Build_Standalone=0
        JucePlugin_Name="Mascara" JucePlugin_IsSynth=0 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0)
    target_link_libraries(MascaraHostCheck PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)
endif()
