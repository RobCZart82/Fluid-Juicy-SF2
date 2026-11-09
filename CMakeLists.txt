cmake_minimum_required(VERSION 3.24)
project(JUICY_SF_PLUGIN VERSION 3.1.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Build pinned dependencies from source with scripts/build.py.
set(JUICY_JUCE_SOURCE "" CACHE PATH "JUCE source checkout")
option(JUICY_COPY_PLUGIN_AFTER_BUILD "Install into the user plugin folder" OFF)
set(JUICY_FORMATS "VST3;Standalone" CACHE STRING "Plugin formats to build")
if(JUICY_JUCE_SOURCE)
    add_subdirectory("${JUICY_JUCE_SOURCE}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    find_package(JUCE 9.0.5 EXACT CONFIG REQUIRED)
endif()
find_package(Threads REQUIRED)
find_package(FluidSynth 2.6.2 EXACT CONFIG REQUIRED)
if(NOT FLUIDSYNTH_SUPPORT_SF3)
    message(FATAL_ERROR "FluidSynth must include SF3 support; use scripts/build.py")
endif()
juce_add_plugin(JuicySFPlugin
    ICON_BIG resources/Logo512.png
    COMPANY_NAME Birchlabs
    COMPANY_WEBSITE "https://birchlabs.co.uk"
    BUNDLE_ID uk.co.birchlabs.juicysfplugin
    DESCRIPTION "Audio plugin to play soundfonts"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD ${JUICY_COPY_PLUGIN_AFTER_BUILD}
    APP_SANDBOX_ENABLED TRUE
    APP_SANDBOX_OPTIONS com.apple.security.files.user-selected.read-only
    HARDENED_RUNTIME_ENABLED TRUE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    AU_SANDBOX_SAFE TRUE
    PLUGIN_MANUFACTURER_CODE Blbs
    # Preserve the original CMake ID (historical Projucer used a different ID).
    PLUGIN_CODE Jsfp
    FORMATS ${JUICY_FORMATS}
    PRODUCT_NAME "juicysfplugin")
add_subdirectory(Source)
target_compile_definitions(JuicySFPlugin PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 DONT_SET_USING_JUCE_NAMESPACE=1
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(JuicySFPlugin PRIVATE
    juce::juce_audio_utils FluidSynth::libfluidsynth
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
option(JUICY_BUILD_TESTS "Build processor regression tests" OFF)
if(JUICY_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(JuicyRegressionTests PRODUCT_NAME "Juicy regression tests")
    target_sources(JuicyRegressionTests PRIVATE tests/ProcessorTests.cpp)
    target_link_libraries(JuicyRegressionTests PRIVATE JuicySFPlugin)
    # The processor headers include JUCE and FluidSynth headers, whose include
    # paths are private to the shared-code target. Reuse them without compiling
    # a second copy of the JUCE modules into the test executable.
    target_include_directories(JuicyRegressionTests PRIVATE
        $<TARGET_PROPERTY:JuicySFPlugin,INCLUDE_DIRECTORIES>)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/make_fixture.py"
        "${CMAKE_BINARY_DIR}/test-sine.sf2" COMMAND_ERROR_IS_FATAL ANY)
    add_test(NAME processor-regressions COMMAND JuicyRegressionTests "${CMAKE_BINARY_DIR}/test-sine.sf2")
endif()
