cmake_minimum_required(VERSION 3.22)

project(Dopplerfeld VERSION 0.1.0)

# JUCE liegt als Git-Klon daneben und wird eingebunden, nicht kopiert.
add_subdirectory(/Users/dpa/Documents/JUCE JUCE)

juce_add_plugin(Dopplerfeld
    COMPANY_NAME               "Dietrich Pank"
    BUNDLE_ID                  "de.pankd.dopplerfeld"
    PLUGIN_MANUFACTURER_CODE   Dpnk
    PLUGIN_CODE                Dp01
    FORMATS                    AU VST3 Standalone
    PRODUCT_NAME               "Dopplerfeld"
    IS_SYNTH                   TRUE
    NEEDS_MIDI_INPUT           FALSE
    NEEDS_MIDI_OUTPUT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD    FALSE)

# Weitere Quellen kommen Häppchen für Häppchen dazu
# (Physics, Motion, Sources, Util, UI - siehe dopplerfeld-plan.md).
target_sources(Dopplerfeld
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Params.cpp
        Source/Physics/SourceTrajectory.cpp
        Source/Physics/SourceSignalBuffer.cpp
        Source/Motion/MotionRecorder.cpp
        Source/Motion/MotionPlayer.cpp
        Source/Motion/OnePoleSmoother.cpp
        Source/Motion/CriticallyDampedSpring.cpp
        Source/Motion/SlewLimiter.cpp
        Source/Motion/OneEuroSmoother.cpp
        Source/Physics/RetardedTimeSolver.cpp)

target_include_directories(Dopplerfeld PRIVATE Source)

target_compile_definitions(Dopplerfeld
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Dopplerfeld
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Solver-Prüfprogramm ohne JUCE-Audio, nur zum Rechnen (Plan Abschnitt 6, H4:
# Abnahme gegen die geschlossene Lösung aus Plan 2.5). `ctest` fasst es an,
# damit ein künftiger Regressions-Check nicht am solver_check vorbeigeht.
add_executable(solver_check
    Tests/solver_check.cpp
    Source/Physics/RetardedTimeSolver.cpp
    Source/Physics/SourceTrajectory.cpp)
target_include_directories(solver_check PRIVATE Source)
target_compile_features(solver_check PRIVATE cxx_std_20)

enable_testing()
add_test(NAME solver_check COMMAND solver_check)
