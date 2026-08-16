# dopplerfeld - Architektur-Karte

Kurzeinstieg für KI-Sessions (Claude o.ä.), damit man nicht erst den ganzen
Code oder die komplette Chat-Historie lesen muss. Ausführliche Physik/Design-
Begründung steht in `dopplerfeld-plan.md` (eine Ebene höher, `../dopplerfeld-
plan.md`) - hier nur die Landkarte, was wo liegt und wie es zusammenhängt.

## Was das Plugin ist

JUCE-Audio-Plugin (AU/VST3/Standalone), physikalische Nachstellung des
akustischen Dopplereffekts inkl. Überschall/Mach-Kegel. Eine Schallquelle M
und ein Hörer L (mit Kopf-Orientierung) auf einer 700x400px-Fläche, die eine
reale Modellgröße n Meter darstellt. Ton entsteht aus echter Laufzeit
(retarded time), nicht aus einem Doppler-Formelfaktor.

Baubar mit CMake (siehe `granular/` als Schwesterprojekt für dasselbe Muster,
JUCE liegt unter `~/Documents/JUCE`, `add_subdirectory` statt Kopie).

## Schichtenmodell

```
Source/Physics/    - JUCE-frei, reines C++. Retarded-Time-Löser, Ausbreitung,
                      Trajektorien-/Signalpuffer. Offline testbar
                      (solver_check), unabhängig vom Audio-Framework.
Source/Motion/     - JUCE-frei. Bewegungsglättung (4 Verfahren), Aufnahme/
                      Wiedergabe.
Source/Sources/    - Klangquellen (Motor-Generator, Sample-Player) + deren
                      Crossfade. Nutzt JUCE-DSP, aber kein JUCE-Audio-Framework
                      (kein AudioProcessor-Wissen).
Source/Util/       - Crossfade-Engine (generisch), FieldSnapshot-Datenformat.
Source/UI/         - JUCE-Component-Schicht: Feldanzeige, Regler-Panels,
                      Kopf-/Quellen-Symbole.
Source/*.cpp (Wurzel) - PluginProcessor/PluginEditor: Zusammenbau, kennt alle
                      Schichten, enthält selbst möglichst wenig Logik.
Tests/             - solver_check (Physik-Löser gegen geschlossene Lösung),
                      load_check (Lasttest inkl. Extremfälle, offline).
```

Prinzip: je tiefer die Schicht, desto weniger weiß sie von JUCE/Audio-Threading
- der Löser kennt nicht mal `juce::AudioBuffer`. Das hält die physikalisch
kritischen Teile einzeln testbar (`solver_check`, `ctest`).

## Kernklassen und wie sie zusammenhängen

- **`RetardedTimeSolver`** (Physics/) - löst die Emissionszeit-Gleichung
  numerisch (Brent, nicht Newton - siehe Kommentar dort warum), überschall-
  fähig (0/1/2 Wurzeln). Ein Löser pro Empfangspunkt (Ohr), Zustand hängt an
  dessen Geschichte.
- **`PropagationPath`** (Physics/) - eine Ausbreitungsstrecke Quelle→Empfänger.
  Enthält den Löser, pro Wurzelzweig ein Luftdämpfungsfilter (`Branch::lpZ`,
  One-Pole) und einen Anti-Klick-Envelope. Beliebig oft instanziierbar (Plan:
  später auch Boden-/Wandspiegelungen). **Bekannte Schwachstelle:** `lpZ` ist
  persistenter Filterzustand - ein einzelner nicht-endlicher Wert würde ihn
  für immer vergiften (siehe `git log` Commit "Fix: dauerhafter Sound-Ausfall"
  für einen bereits gefundenen, aber laut @dpa NICHT vollständig behobenen
  Fall dieser Bug-Klasse - Stand 2026-08-16 weiterhin ungeklärt, u.a. tritt er
  auch beim Verlust des Fensterfokus auf, was gegen einen reinen Physik-
  Edge-Case spricht).
- **`DopplerEngine`** (Physics/) - hält Quellsignal-Ringpuffer
  (`SourceSignalBuffer`), Quell-Trajektorie, und zwei `PropagationPath`
  (L/R-Ohr) pro `PathSet`. Ein `PathSet` ist ein kompletter Geometriesatz;
  bei Positionssprüngen/Feldgrößenänderungen laufen zwei `PathSet`
  gegeneinander gecrossfadet (`DualPathCrossfader<PathSet>`, Member
  `geometry`). `fillSnapshot()` liefert per Seqlock-Doppelpufferung Anzeige-
  daten an den GUI-Thread.
- **`SoundSourceHolder`** (Sources/) - crossfadet zwischen `EngineGenerator`
  und `SampleSource`, ist selbst eine `SoundSource`.
- **`DopplerfeldProcessor`** (PluginProcessor) - hält alle Instanzen, liest
  Parameter pro Block über gecachte Rohzeiger (`raw()`-Helper, kein APVTS-
  Listener - alle Setter laufen dadurch ausschließlich im Audiothread),
  fährt die Engine in 128-Sample-Teilblöcken (`motionChunkSamples`).
  Record/Play laufen über atomare Request-Flags (Message→Audiothread), nicht
  über eine volle Kommandoqueue (bewusste Vereinfachung, siehe Kommentar
  dort).
- **`DopplerfeldEditor`** (PluginEditor) - `FieldComponent` (700x400) links,
  vier `CollapsiblePanel` mit den vier `XyzPanel`s rechts in einem Viewport.
  30-Hz-Timer holt `FieldSnapshot` ab, aktualisiert Statuszeile/Button-Texte.

## Parameter

Alle IDs zentral in `Source/Params.h` (`namespace Params`), Layout in
`Params.cpp::createParameterLayout()`. Jeder Regler dort eine Zeile - min/max/
step ändern heißt: diese eine Zeile ändern, nicht durchs UI suchen.

## Build & Test

```
cmake -B build
cmake --build build --target Dopplerfeld_Standalone -j 4
cd build && ctest --output-on-failure     # solver_check + load_check
```

`solver_check`: Physik-Löser gegen geschlossene Lösung (Plan 2.5), muss bei
jeder Änderung an `RetardedTimeSolver`/`PropagationPath` grün bleiben.
`load_check`: Processor offline durchgefahren (n=200 und n=10000, inkl.
Mach-3-Querung), prüft auf NaN/Inf und grobe CPU-Plausibilität.

**Wichtig:** dieses Projekt hat bislang durchgehend warnungsfrei gebaut
(volle JUCE-Warnschärfe: `-Wall -Wextra -Wshadow-all -Wconversion
-Wsign-conversion -Wfloat-equal -Wcast-align -Wshorten-64-to-32`). Neue
Warnungen sind ernst zu nehmen, nicht zu ignorieren - bewusste Ausnahmen
(z.B. `-Wfloat-equal` bei absichtlichen Identitätsvergleichen) werden lokal
per `#pragma clang diagnostic` unterdrückt und im Kommentar begründet, nicht
projektweit abgeschaltet.

## Stand 2026-08-16 (Phase 1 fertig, erste Hördurchgänge)

Phase 1 aus dem Plan ist komplett umgesetzt (alle Häppchen H1-H13, siehe
`git log`). Danach folgte ein erster echter Hördurchgang mit @dpa, daraus:

**Behoben:** Kopf-/Quellen-Symbol-Geometrie (Ohren zeigten nach hinten statt
vorne, Quellsymbol strahlte nur einseitig), fehlender Stop für die Bewegungs-
Wiedergabe, Regleranzeige rundet jetzt dynamisch statt fest, deutsche
Tooltips für alle Regler (abschaltbar).

**Update (späterer Hördurchgang, selber Tag):** Der Sound-Ausfall ist mit
hoher Wahrscheinlichkeit **CPU-Überlastung**, kein reiner Numerik-Edge-Case -
gemessen und bestätigt (siehe Commit "CPU-Aufschlüsselung..."): schon ein
Positionssprung von 40m alle 150ms bei Feldgröße 150m und Standard-Tau
(50ms) - also realistischer Gebrauch, kein künstlicher Extremtest - erreicht
dauerhaft nahe/über Mach 1 und treibt den Audiothread auf ~290% des
Echtzeit-Budgets, davon 276% im Löser (`PropagationPath`/
`RetardedTimeSolver`), nur 4% in der Klangquelle. Zerstückelter Ton und
Totalausfall bei hoher Geschwindigkeit sind damit als Überlastungs-Symptom
erklärt; @dpa bestätigt: Buffergrößen-Wechsel im Standalone bringt den Ton
kurzzeitig zurück (klassisches Symptom für einen Audio-Geräte-Recovery nach
Dropout). Der Fokusverlust-Trigger ist damit vermutlich einfach "noch etwas
weniger CPU-Zeit für einen ohnehin knappen Prozess", nicht separat untersucht.

Ein erster Fix (`SourceTrajectory::recentMaxSpeed`, siehe Commit "Stride-
Hangover-Fix") behebt einen TEIL-Mechanismus (ein kurzer Ausreißer hielt den
Löser bisher bis zu 40s im teuren Modus), hilft aber NICHT bei dauerhaft
nahe/über Mach 1 gehaltener Bewegung - dort bleibt der Vollfenster-Scan im
Löser selbst (Plan 2.10, bei Stride 8) grundsätzlich teuer, unabhängig davon
wie lange er anhält.

**Offen / bekannt kaputt:**
- **Löser-Performance (nächster großer Brocken, @dpa: "frischer/Opus-
  Anlauf", ähnlich H4-Sorgfalt nötig).** Ziel: der Vollfenster-Scan-Algorithmus
  in `RetardedTimeSolver::solve()` selbst muss günstiger werden (nicht nur
  seltener aufgerufen), ohne die per `solver_check` verifizierte Korrektheit
  zu verlieren (inkl. des physikalisch echten Falls "verspäteter Boom aus
  großer Distanz", der einen Grund für das Vollfenster liefert - siehe
  Kommentar bei `recentMaxSpeed()` in SourceTrajectory.h für die Falle, die
  hier schon einmal drohte). Diagnose-Werkzeug dafür: neues load_check-
  Szenario "Realistisch nahe Mach1" (Tests/load_check.cpp) reproduziert das
  reale Problem offline, mit CPU-Aufschlüsselung (`cpuLoadPhysicsPercent()`/
  `cpuLoadSourcePercent()` in PluginProcessor) als Fortschrittsmaß.
- Motor-Klangfarbe verändert sich manchmal nach schnellem Maus-Drag,
  bleibt dann dauerhaft anders (unabhängig vom Doppler-Effekt selbst) -
  noch nicht untersucht. Könnte mit derselben CPU-Überlastung zusammenhängen
  (Glitch in der Motor-Renderkette unter Zeitdruck?) - ungeprüft.
- Überschall-Boom klingt laut @dpa noch nicht "richtig" (zu leise/kein
  hörbarer Doppelschlag), auch bei waagerechten Vorbeiflügen prüfen - Ursache
  noch nicht untersucht, könnte an `boomLimitDb`-Default liegen oder simpel
  daran, dass die Löser-Überlastung die Boom-Phase selbst hörbar verzerrt.
  Mach-Kegel-Visualisierung im Feld ist ein offener Wunsch (aktuell nur
  Wellenfront-Kreise, keine Kegel-Tangenten/Einhüllende explizit gezeichnet).
- Aufnahme/Debug-Export/Snapshots (wie im Schwesterprojekt `werkbank`) sind
  gewünscht, aber noch nicht gebaut - zurückgestellt, nachdem die CPU-Spur
  sich als ergiebiger erwies. Snapshot-Speicherung ist auch im Standalone
  möglich (kein Host nötig, JUCE-Apps dürfen ins Application-Support-
  Verzeichnis schreiben), nur nicht automatisch wie die Host-Projektspeicherung.
- Levelmeter (-6dB-Marke, Clip-Halt) und CPU-Echtzeit-Anzeige (Statuszeile,
  rot über 100%, mit Physik/Quelle-Aufschlüsselung) sind bereits umgesetzt
  (siehe `git log`).

Chat-Verlauf mit der vollständigen Entstehungsgeschichte (inkl. aller
Design-Entscheidungen aus dem Grill-Interview) liegt in der Claude-Code-
Session vom 2026-08-15/16, falls tiefere Begründungen für eine Entscheidung
gebraucht werden, die weder hier noch im Code-Kommentar stehen.
