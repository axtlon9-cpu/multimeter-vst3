cmake_minimum_required(VERSION 3.22)
project(MultimeterVST3 VERSION 1.0.0)

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Multimeter
  COMPANY_NAME "Axtlon Audio"
  BUNDLE_ID "com.axtlonaudio.multimeter"
  PLUGIN_MANUFACTURER_CODE Axtl
  PLUGIN_CODE Mlt1
  FORMATS VST3
  PRODUCT_NAME "Multimeter"
  DESCRIPTION "Stereo audio level and correlation meter"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Fx|Analyzer)

juce_generate_juce_header(Multimeter)

target_sources(Multimeter PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginProcessor.h
  Source/PluginEditor.cpp
  Source/PluginEditor.h)

target_compile_features(Multimeter PRIVATE cxx_std_17)
target_compile_definitions(Multimeter PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Multimeter PRIVATE
  juce::juce_audio_utils
  juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags
         juce::juce_recommended_lto_flags
         juce::juce_recommended_warning_flags)
