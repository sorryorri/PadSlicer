cmake_minimum_required(VERSION 3.22)

# Build for Apple Silicon and Intel Macs, running on macOS 11 and later, so it can be shared
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")

project(PADSLICER VERSION 0.1.0)

# Pull JUCE in at configure time. To use a local checkout instead, configure with
# -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(PadSlicer
    COMPANY_NAME "@sorryorri"                   # Ableton lists the plugin in a folder with this name
    BUNDLE_ID com.sorryorri.padslicer
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE                # Installs to ~/Library/Audio/Plug-Ins so Ableton finds it
    PLUGIN_MANUFACTURER_CODE Orri               # Four chars, at least one upper-case. Keep it: hosts use it to find the plugin in saved sets
    PLUGIN_CODE Pslc                            # Four chars, exactly one upper-case
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "PadSlicer"
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_sources(PadSlicer
    PRIVATE
        PluginEditor.cpp
        PluginProcessor.cpp
        Retro.cpp
        Knight.cpp
        Effects.cpp
        Transients.cpp)

target_compile_definitions(PadSlicer
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(PadSlicer
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
