cmake_minimum_required(VERSION 3.22)

project(Dopplerfeld VERSION 0.3.0)

# Ohne CMAKE_BUILD_TYPE erzeugt CMake bei Makefile-/Ninja-Generatoren einen
# Bau ganz ohne -O-Schalter. JUCEs juce_recommended_config_flags hängen ihre
# Optimierung an $<CONFIG:Release>, greifen dann also auch nicht. Das Ergebnis
# ist ein unoptimierter Löser im Audiothread - für dieses Plugin der
# Unterschied zwischen echtzeitfähig und Aussetzern. Deshalb Release als
# Vorgabe; wer debuggen will, gibt -DCMAKE_BUILD_TYPE=Debug ausdrücklich an.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Bauart" FORCE)
endif()

# JUCE liegt als Git-Klon daneben und wird eingebunden, nicht kopiert. Pfad
# per Variable statt fest verdrahtet, damit CI (und andere Rechner) mit
# -DJUCE_DIR=... auf einen eigenen Checkout zeigen koennen; lokal bleibt der
# bisherige Pfad als Vorgabe unveraendert.
if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR "$ENV{HOME}/Documents/JUCE")
endif()
add_subdirectory(${JUCE_DIR} JUCE)

# Startzustand beim allerersten Oeffnen (@dpa 20260828): das Preset liegt als
# Kopie in der Programmdatei selbst, damit es auch auf einem Rechner da ist,
# auf dem noch kein Preset-Ordner existiert. Quelle bleibt die eine Datei im
# presets-Ordner - sie wird zur Bauzeit unter einen Namen ohne Sonderzeichen
# kopiert, weil juce_add_binary_data den Dateinamen zum C++-Bezeichner macht
# und "@" wie "²" dort nichts zu suchen haben.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/presets/600kmh-Drone@600m²"
               "${CMAKE_CURRENT_BINARY_DIR}/StartPreset/StartPreset.dopplerfeld"
               COPYONLY)

juce_add_binary_data(DopplerfeldStartPreset
    SOURCES "${CMAKE_CURRENT_BINARY_DIR}/StartPreset/StartPreset.dopplerfeld")

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
        Source/Motion/PositionJitter.cpp
        Source/Physics/RetardedTimeSolver.cpp
        Source/Sources/EngineGenerator.cpp
        Source/Sources/SampleSource.cpp
        Source/Sources/AudioInSource.cpp
        Source/Physics/PropagationPath.cpp
        Source/Physics/DopplerEngine.cpp
        Source/UI/HeadSymbol.cpp
        Source/UI/FieldComponent.cpp
        Source/UI/CollapsiblePanel.cpp
        Source/UI/EngineControlPanel.cpp
        Source/UI/EnginePanel.cpp
        Source/UI/SamplePanel.cpp
        Source/UI/MotionPanel.cpp
        Source/UI/FieldPanel.cpp
        Source/UI/WallPanel.cpp
        Source/Util/Crossfader.cpp
        Source/UI/SwarmPanel.cpp
        Source/Sources/SoundSourceHolder.cpp
        Source/UI/LevelMeter.cpp
        Source/UI/ScopeComponent.cpp
        Source/UI/WelcomeOverlay.cpp
        Source/UI/PresetBar.cpp)

target_include_directories(Dopplerfeld PRIVATE Source)

target_compile_definitions(Dopplerfeld
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Dopplerfeld
    PRIVATE
        DopplerfeldStartPreset
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Die Bauzeit oben links im Fenster (PluginEditor::paint, __DATE__/__TIME__)
# steht beim Uebersetzen fest. Wird ausgerechnet an dieser Datei nichts
# geaendert, uebersetzt make sie nicht neu, und das Fenster zeigt weiter die
# Zeit des letzten Laufs, bei dem sie zufaellig mit dran war - die Anzeige, an
# der sich eine veraltete Fassung erkennen laesst, waere dann selbst veraltet.
# Deshalb vor jedem Bau anfassen: eine Uebersetzungseinheit kostet
# Sekundenbruchteile, ein Durchgang mit der falschen Fassung einen Hoerdurchgang.
add_custom_target(dopplerfeld_bauzeit ALL
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_SOURCE_DIR}/Source/PluginEditor.cpp
    COMMENT "Bauzeit-Stempel auffrischen")

add_dependencies(Dopplerfeld dopplerfeld_bauzeit)

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
    $<TARGET_PROPERTY:Dopplerfeld,COMPILE_DEFINITIONS>
    DOPPLERFELD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(load_check PRIVATE cxx_std_20)

# Messprogramm zum Rundenpunkt einer Wiedergabe (Tests/loop_peak.cpp). Wie
# load_check an der Plugin-Bibliothek, aber bewusst NICHT als Test
# registriert: es misst Rechenzeit und ist damit von der Maschine abhaengig.
add_executable(loop_peak EXCLUDE_FROM_ALL Tests/loop_peak.cpp)
target_link_libraries(loop_peak
    PRIVATE
        Dopplerfeld
        juce::juce_recommended_warning_flags
        juce::juce_recommended_config_flags)
target_include_directories(loop_peak PRIVATE
    Source
    $<TARGET_PROPERTY:Dopplerfeld,INCLUDE_DIRECTORIES>)
target_compile_definitions(loop_peak PRIVATE
    $<TARGET_PROPERTY:Dopplerfeld,COMPILE_DEFINITIONS>
    DOPPLERFELD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(loop_peak PRIVATE cxx_std_20)

# Messprogramm zum lauten Ausbruch nach einem Preset-Wechsel
# (Tests/burst_probe.cpp), wie loop_peak kein Test.
add_executable(burst_probe EXCLUDE_FROM_ALL Tests/burst_probe.cpp)
target_link_libraries(burst_probe
    PRIVATE
        Dopplerfeld
        juce::juce_recommended_warning_flags
        juce::juce_recommended_config_flags)
target_include_directories(burst_probe PRIVATE
    Source
    $<TARGET_PROPERTY:Dopplerfeld,INCLUDE_DIRECTORIES>)
target_compile_definitions(burst_probe PRIVATE
    $<TARGET_PROPERTY:Dopplerfeld,COMPILE_DEFINITIONS>
    DOPPLERFELD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(burst_probe PRIVATE cxx_std_20)

# Messprogramm zum Klon-Schwarm (Tests/swarm_probe.cpp), wie loop_peak kein Test.
add_executable(swarm_probe EXCLUDE_FROM_ALL Tests/swarm_probe.cpp)
target_link_libraries(swarm_probe
    PRIVATE
        Dopplerfeld
        juce::juce_recommended_warning_flags
        juce::juce_recommended_config_flags)
target_include_directories(swarm_probe PRIVATE
    Source
    $<TARGET_PROPERTY:Dopplerfeld,INCLUDE_DIRECTORIES>)
target_compile_definitions(swarm_probe PRIVATE
    $<TARGET_PROPERTY:Dopplerfeld,COMPILE_DEFINITIONS>
    DOPPLERFELD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(swarm_probe PRIVATE cxx_std_20)

# Messprogramm zum Nachlauf (Tests/coast_probe.cpp), wie loop_peak kein Test.
add_executable(coast_probe EXCLUDE_FROM_ALL Tests/coast_probe.cpp)
target_link_libraries(coast_probe
    PRIVATE
        Dopplerfeld
        juce::juce_recommended_warning_flags
        juce::juce_recommended_config_flags)
target_include_directories(coast_probe PRIVATE
    Source
    $<TARGET_PROPERTY:Dopplerfeld,INCLUDE_DIRECTORIES>)
target_compile_definitions(coast_probe PRIVATE
    $<TARGET_PROPERTY:Dopplerfeld,COMPILE_DEFINITIONS>
    DOPPLERFELD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(coast_probe PRIVATE cxx_std_20)

# Messprogramm zu "manchmal zu leise" (Tests/preset_probe.cpp), kein Test.
add_executable(preset_probe EXCLUDE_FROM_ALL Tests/preset_probe.cpp)
target_link_libraries(preset_probe
    PRIVATE
        Dopplerfeld
        juce::juce_recommended_warning_flags
        juce::juce_recommended_config_flags)
target_include_directories(preset_probe PRIVATE
    Source
    $<TARGET_PROPERTY:Dopplerfeld,INCLUDE_DIRECTORIES>)
target_compile_definitions(preset_probe PRIVATE
    $<TARGET_PROPERTY:Dopplerfeld,COMPILE_DEFINITIONS>
    DOPPLERFELD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(preset_probe PRIVATE cxx_std_20)

# Layout-Bilder des Bewegungs-Panels (Tests/panel_shot.cpp), kein Test.
add_executable(panel_shot EXCLUDE_FROM_ALL Tests/panel_shot.cpp)
target_link_libraries(panel_shot
    PRIVATE
        Dopplerfeld
        juce::juce_recommended_warning_flags
        juce::juce_recommended_config_flags)
target_include_directories(panel_shot PRIVATE
    Source
    $<TARGET_PROPERTY:Dopplerfeld,INCLUDE_DIRECTORIES>)
target_compile_definitions(panel_shot PRIVATE
    $<TARGET_PROPERTY:Dopplerfeld,COMPILE_DEFINITIONS>
    DOPPLERFELD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(panel_shot PRIVATE cxx_std_20)

# Draufsicht-Bilder der Feldanzeige mit M innerhalb/ausserhalb des Feldes
# (Tests/field_shot.cpp), Verifikation der Randmarke - kein Test.
add_executable(field_shot EXCLUDE_FROM_ALL Tests/field_shot.cpp)
target_link_libraries(field_shot
    PRIVATE
        Dopplerfeld
        juce::juce_recommended_warning_flags
        juce::juce_recommended_config_flags)
target_include_directories(field_shot PRIVATE
    Source
    $<TARGET_PROPERTY:Dopplerfeld,INCLUDE_DIRECTORIES>)
target_compile_definitions(field_shot PRIVATE
    $<TARGET_PROPERTY:Dopplerfeld,COMPILE_DEFINITIONS>
    DOPPLERFELD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(field_shot PRIVATE cxx_std_20)

# Messprogramm zum Anfassen von M (Tests/grab_probe.cpp) - simuliert
# Mausereignisse auf der FieldComponent, kein Test.
add_executable(grab_probe EXCLUDE_FROM_ALL Tests/grab_probe.cpp)
target_link_libraries(grab_probe
    PRIVATE
        Dopplerfeld
        juce::juce_recommended_warning_flags
        juce::juce_recommended_config_flags)
target_include_directories(grab_probe PRIVATE
    Source
    $<TARGET_PROPERTY:Dopplerfeld,INCLUDE_DIRECTORIES>)
target_compile_definitions(grab_probe PRIVATE
    $<TARGET_PROPERTY:Dopplerfeld,COMPILE_DEFINITIONS>)
target_compile_features(grab_probe PRIVATE cxx_std_20)

# Messprogramm zum Play-Knopf am Scope (Tests/scope_play_probe.cpp) - misst
# die Wiedergabe am Ausgang des Processors, kein Test.
add_executable(scope_play_probe EXCLUDE_FROM_ALL Tests/scope_play_probe.cpp)
target_link_libraries(scope_play_probe
    PRIVATE
        Dopplerfeld
        juce::juce_recommended_warning_flags
        juce::juce_recommended_config_flags)
target_include_directories(scope_play_probe PRIVATE
    Source
    $<TARGET_PROPERTY:Dopplerfeld,INCLUDE_DIRECTORIES>)
target_compile_definitions(scope_play_probe PRIVATE
    $<TARGET_PROPERTY:Dopplerfeld,COMPILE_DEFINITIONS>)
target_compile_features(scope_play_probe PRIVATE cxx_std_20)

# Bildaufnahmen von Editor und Schwarm-Panel (Tests/editor_shot.cpp) - kein Test.
add_executable(editor_shot EXCLUDE_FROM_ALL Tests/editor_shot.cpp)
target_link_libraries(editor_shot
    PRIVATE
        Dopplerfeld
        juce::juce_recommended_warning_flags
        juce::juce_recommended_config_flags)
target_include_directories(editor_shot PRIVATE
    Source
    $<TARGET_PROPERTY:Dopplerfeld,INCLUDE_DIRECTORIES>)
target_compile_definitions(editor_shot PRIVATE
    $<TARGET_PROPERTY:Dopplerfeld,COMPILE_DEFINITIONS>
    DOPPLERFELD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(editor_shot PRIVATE cxx_std_20)

enable_testing()
add_test(NAME solver_check COMMAND solver_check)
add_test(NAME load_check COMMAND load_check)
