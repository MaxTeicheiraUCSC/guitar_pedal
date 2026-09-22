cmake_minimum_required(VERSION 3.22)
project(pedal_emulator LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# The DSP core is header-only; these flags mirror the firmware build so host code
# cannot accidentally depend on exceptions/RTTI.
add_library(pedal_dsp INTERFACE)
target_include_directories(pedal_dsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/dsp/include ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(pedal_dsp INTERFACE
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-exceptions -fno-rtti -Wall -Wextra -ffast-math>
  $<$<CXX_COMPILER_ID:MSVC>:/GR- /fp:fast /W3 /EHs-c- /D_CRT_SECURE_NO_WARNINGS /D_HAS_EXCEPTIONS=0>)

add_executable(emu cli/emu.cpp)
target_link_libraries(emu PRIVATE pedal_dsp)

enable_testing()
add_executable(dsp_tests tests/dsp_tests.cpp)
target_link_libraries(dsp_tests PRIVATE pedal_dsp)
add_test(NAME dsp_tests COMMAND dsp_tests)

option(PEDAL_BUILD_JUCE "Build the JUCE standalone/plugin front panel" OFF)
if(PEDAL_BUILD_JUCE)
  add_subdirectory(juce)
endif()
