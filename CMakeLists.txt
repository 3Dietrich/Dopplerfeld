cmake_minimum_required(VERSION 3.22)

project(Dopplerfeld VERSION 0.1.0)

# Ohne CMAKE_BUILD_TYPE erzeugt CMake bei Makefile-/Ninja-Generatoren einen
# Bau ganz ohne -O-Schalter. JUCEs juce_recommended_config_flags hängen ihre
# Optimierung an $<CONFIG:Release>, greifen dann also auch nicht. Das Ergebnis
# ist ein unoptimierter Löser im Audiothread - für dieses Plugin der
# Unterschied zwischen echtzeitfähig und Aussetzern. Deshalb Release als
# Vorgabe; wer debuggen will, gibt -DCMAKE_BUILD_TYPE=Debug ausdrücklich an.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Bauart" FORCE)
endif()

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
        Source/Motion/FlyByGenerator.cpp
        Source/Motion/OnePoleSmoother.cpp
        Source/Motion/CriticallyDampedSpring.cpp
        Source/Motion/SlewLimiter.cpp
        Source/Motion/OneEuroSmoother.cpp
        Source/Physics/RetardedTimeSolver.cpp
        Source/Sources/EngineGenerator.cpp
        Source/Sources/SampleSource.cpp
        Source/Physics/PropagationPath.cpp
        Source/Physics/DopplerEngine.cpp
        Source/UI/HeadSymbol.cpp
        Source/UI/FieldComponent.cpp
        Source/UI/CollapsiblePanel.cpp
        Source/UI/EnginePanel.cpp
        Source/UI/SamplePanel.cpp
        Source/UI/MotionPanel.cpp
        Source/UI/FieldPanel.cpp
        Source/UI/WallPanel.cpp
        Source/Util/Crossfader.cpp
        Source/Sources/SoundSourceHolder.cpp
        Source/UI/LevelMeter.cpp)

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

# Lasttest aus Plan Abschnitt 6 (H13): fährt den fertig verdrahteten Processor
# offline, ohne Audiogerät. Hängt an der Plugin-Bibliothek selbst, damit
# wirklich der ausgelieferte Code geprüft wird und keine Nachbildung davon.
add_executable(load_check Tests/load_check.cpp)
# Nur gegen Dopplerfeld linken, nicht zusätzlich gegen die juce::-Module: die
# stecken bereits in dieser Bibliothek. Ein zweites Mal eingebunden würden sie
# hier neu übersetzt, was den Testbau um die Fremdwarnungen aus den
# JUCE-Drittbibliotheken (harfbuzz, VST3-SDK) ergänzen würde.
# juce_recommended_warning_flags trägt nur Compiler-Schalter, keine Quellen -
# der Test wird damit genauso streng geprüft wie das Plugin, ohne dass etwas
# doppelt übersetzt wird.
target_link_libraries(load_check
    PRIVATE
        Dopplerfeld
        juce::juce_recommended_warning_flags
        juce::juce_recommended_config_flags)

# Include-Pfade und Defines von der Plugin-Bibliothek übernehmen, statt die
# juce::-Module ein zweites Mal einzubinden: die stecken dort bereits fertig
# übersetzt drin, ein erneutes Einbinden würde sie für diesen Test noch einmal
# komplett übersetzen (samt der Fremdwarnungen aus harfbuzz und VST3-SDK).
target_include_directories(load_check PRIVATE
    Source
    $<TARGET_PROPERTY:Dopplerfeld,INCLUDE_DIRECTORIES>)
target_compile_definitions(load_check PRIVATE
    $<TARGET_PROPERTY:Dopplerfeld,COMPILE_DEFINITIONS>)
target_compile_features(load_check PRIVATE cxx_std_20)

enable_testing()
add_test(NAME solver_check COMMAND solver_check)
add_test(NAME load_check COMMAND load_check)
