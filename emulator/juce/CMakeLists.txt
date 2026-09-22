# JUCE front panel: standalone app + VST3 (AU needs full Xcode; enable with -DPEDAL_AU=ON).
include(FetchContent)
FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.9 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)

option(PEDAL_AU "Also build the AU (requires Xcode)" OFF)
set(PEDAL_FORMATS Standalone VST3)
if(PEDAL_AU)
  list(APPEND PEDAL_FORMATS AU)
endif()

juce_add_plugin(PedalEmu
  VERSION 0.1.0
  COMPANY_NAME "guitar_pedal"
  PLUGIN_MANUFACTURER_CODE Gpdl
  PLUGIN_CODE Pdl1
  FORMATS ${PEDAL_FORMATS}
  PRODUCT_NAME "PedalEmu"
  IS_SYNTH FALSE
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "PedalEmu needs the audio input to run your guitar through the pedal."
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE)

file(GLOB PEDAL_NOTE_SAMPLES ${CMAKE_CURRENT_SOURCE_DIR}/../assets/samples/note_*.wav)
juce_add_binary_data(PedalAssets SOURCES ${PEDAL_NOTE_SAMPLES} ${CMAKE_CURRENT_SOURCE_DIR}/../assets/test_e_gsharp.wav ${CMAKE_CURRENT_SOURCE_DIR}/../assets/test_loop.wav ${CMAKE_CURRENT_SOURCE_DIR}/../assets/test_emaj7_arp.wav)
target_sources(PedalEmu PRIVATE PluginProcessor.cpp PluginEditor.cpp)
target_include_directories(PedalEmu PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/dsp/include)
target_compile_definitions(PedalEmu PUBLIC _CRT_SECURE_NO_WARNINGS=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(PedalEmu PRIVATE PedalAssets juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

option(PEDAL_HARNESS "Build the in-process ASan stress harness" OFF)
if(PEDAL_HARNESS)
  juce_add_console_app(PedalHarness PRODUCT_NAME "PedalHarness" VERSION 0.1.0)
  target_sources(PedalHarness PRIVATE Harness.cpp PluginProcessor.cpp PluginEditor.cpp)
  target_include_directories(PedalHarness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/dsp/include)
  target_compile_definitions(PedalHarness PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JucePlugin_Name="PedalEmu" JucePlugin_Build_Standalone=1 JucePlugin_IsSynth=0 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0 JucePlugin_EditorRequiresKeyboardFocus=0 JucePlugin_Version=0.1.0 JucePlugin_VersionCode=0x100 JucePlugin_VersionString="0.1.0" JucePlugin_Manufacturer="guitar_pedal" JucePlugin_ManufacturerCode=0x4770646c JucePlugin_PluginCode=0x50646c31 JucePlugin_ManufacturerWebsite="" JucePlugin_ManufacturerEmail="" JucePlugin_Desc="" JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_MODAL_LOOPS_PERMITTED=1)
  target_compile_options(PedalHarness PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-omit-frame-pointer -g -O1 -UNDEBUG -DDEBUG=1> $<$<CXX_COMPILER_ID:MSVC>:/DDEBUG=1>)

  target_link_libraries(PedalHarness PRIVATE PedalAssets juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
endif()
