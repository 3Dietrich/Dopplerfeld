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
  One-Pole) und einen Anti-Klick-Envelope. Beliebig oft instanziierbar - die
  Bodenspiegelung ist genau das und kein Sonderweg: dieselbe Klasse mit
  `PathTransform{scale.z = -1}`, dazu ein zweiter, streckenunabhängiger
  Dämpfungsgrad (`setReflectionDamping`) für die Reflexionsfläche.
  **Bekannte Schwachstelle:** `lpZ` ist
  persistenter Filterzustand - ein einzelner nicht-endlicher Wert würde ihn
  für immer vergiften (siehe `git log` Commit "Fix: dauerhafter Sound-Ausfall"
  für einen bereits gefundenen, aber laut @dpa NICHT vollständig behobenen
  Fall dieser Bug-Klasse - Stand 2026-08-16 weiterhin ungeklärt, u.a. tritt er
  auch beim Verlust des Fensterfokus auf, was gegen einen reinen Physik-
  Edge-Case spricht).
- **`DopplerEngine`** (Physics/) - hält Quellsignal-Ringpuffer
  (`SourceSignalBuffer`), Quell-Trajektorie, und vier `PropagationPath` pro
  `PathSet`: Direktschall auf L/R plus die Bodenspiegelung auf L/R
  (`pathEar`/`pathMirror`). Die Spiegelpfade liegen dauerhaft bereit und
  werden bei abgeschalteter Bodenreflexion übersprungen - Umschalten
  allokiert damit nichts, und ausgeschaltet kosten sie keine Löserzeit.
  Ein `PathSet` ist ein kompletter Geometriesatz;
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

Eine Ausnahme in der Einheitenwahl: `srcX/srcY/lisX/lisY` sind auf die
Feldfläche normiert (0..1) und werden erst im Processor mit dem Feldmaßstab
multipliziert, `srcZ/lisZ` stehen dagegen in echten Metern. Die Höhe hängt
nicht am Maßstab - ein Feldwechsel von 100 m auf 10000 m darf den Hörer nicht
mitwachsen lassen. Ein Feldgrößenwechsel verschiebt deshalb x/y, aber nie z.

## Build & Test

```
cmake -B build
cmake --build build --config Release -j 4
cd build && ctest --output-on-failure     # solver_check + load_check
```

Ohne ausdrückliche Angabe baut CMake hier **Release** (`-O3 -DNDEBUG`,
in CMakeLists gesetzt). Das ist kein Schönheitsdetail: ohne
`CMAKE_BUILD_TYPE` erzeugt CMake einen Bau ganz ohne `-O`-Schalter, und
JUCEs `juce_recommended_config_flags` hängen ihre Optimierung an
`$<CONFIG:Release>`, greifen dann also auch nicht - der Löser lief damit
unoptimiert im Audiothread. Zum Debuggen ausdrücklich
`-DCMAKE_BUILD_TYPE=Debug` angeben.

`solver_check`: Physik-Löser gegen geschlossene Lösung (Plan 2.5), muss bei
jeder Änderung an `RetardedTimeSolver`/`PropagationPath` grün bleiben.
`load_check`: Processor offline durchgefahren (n=200 und n=10000, inkl.
Mach-3-Querung), prüft auf NaN/Inf und grobe CPU-Plausibilität. Enthält auch
das Bodenreflexions-Szenario: derselbe Vorbeiflug mit und ohne Spiegelpfade,
und die Prüfung, dass die Reflexion den Ausgang überhaupt verändert (ein nie
gerechneter Spiegelpfad würde sonst stumm durchrutschen).

Zu bauen ist die komplette Konfiguration, nicht nur die Standalone: die
Testbinaries hängen an denselben Quellen, `--target Dopplerfeld_Standalone`
lässt sie stehen und `ctest` liefe danach gegen den alten Stand.

**Wichtig:** dieses Projekt hat bislang durchgehend warnungsfrei gebaut
(volle JUCE-Warnschärfe: `-Wall -Wextra -Wshadow-all -Wconversion
-Wsign-conversion -Wfloat-equal -Wcast-align -Wshorten-64-to-32`). Neue
Warnungen sind ernst zu nehmen, nicht zu ignorieren - bewusste Ausnahmen
(z.B. `-Wfloat-equal` bei absichtlichen Identitätsvergleichen) werden lokal
per `#pragma clang diagnostic` unterdrückt und im Kommentar begründet, nicht
projektweit abgeschaltet.

## Stand 2026-08-17 (dritte Achse z, Bodenreflexion)

Auslöser war @dpas Wunsch nach echter Höhe: sobald z ungleich 0 ist,
reflektiert der Boden. Beides ist gebaut, `solver_check` und `load_check`
sind grün, der Bau bleibt warnungsfrei.

**Was jetzt geht:**

- **z ist eine echte dritte Achse.** `srcZ`/`lisZ` sind reguläre Parameter
  in Metern (Defaults: Hörer 1,75 m Ohrhöhe, Quelle 0 m). Die Physik konnte
  das schon immer - `Vec3`, `RetardedTimeSolver`, `PropagationPath` und die
  Ohrgeometrie in `Listener.h` rechnen seit H3/H4 dreidimensional; gefehlt
  hat ausschließlich der Weg vom Regler bis dorthin. Regler dafür im
  `FieldPanel`, weil x/y gar keine Regler haben (die zieht man mit der Maus
  im Feld) und für z keine Feldachse existiert.
- **Bodenreflexion** als Spiegelquelle an der Ebene z = 0, an-/abschaltbar
  (`groundReflectionOn`, Default aus), mit eigenem Dämpfungsgrad
  (`groundDampAmount`, Eckfrequenz 800 Hz als Modellkonstante). Zwei
  zusätzliche `PropagationPath` pro `PathSet`, kein neuer Algorithmus.
  Gemessen im `load_check`-Szenario (Quelle 20 m hoch, Hörer 1,75 m,
  Vorbeiflug): RMS 0,0183 → 0,0275, Block-Mittel 1,3 % → 2,0 % vom
  Echtzeit-Budget.

**Zwei Dinge, die beim Hören sonst irritieren:**

- Gespiegelt wird der **Empfänger**, nicht die Quelle. Für die Ebene z = 0
  ist das exakt dasselbe (`|L' - M| = |L - M'|`, samt Zeitableitung und damit
  samt Doppler), aber es geht ohne eine zweite Trajektorie - die Quellbahn
  lesen alle Pfade gemeinsam. Herleitung steht in `PathTransform.h`.
- Bei `srcZ` = 0 (dem Default) liegt die Spiegelquelle **exakt auf** der
  echten Quelle. Die Reflexion ist dann nur eine gedämpfte Verdopplung ohne
  eigene Laufzeit, kein hörbar getrennter zweiter Weg. Das ist physikalisch
  richtig und kein Fehler - hörbar wird die Reflexion erst, wenn die Quelle
  über dem Boden liegt.

Gehört hat @dpa das alles noch nicht - gemessen ist es, beurteilt nicht.
Insbesondere die 800 Hz Eckfrequenz der Bodendämpfung und der Umstand, dass
die Reflexion mit voller Amplitude zurückkommt (der Verlust steckt allein in
den Höhen), sind Modellentscheidungen, die sein Ohr noch bestätigen muss.

## Backlog (aufgenommen 2026-08-17, bewusst nicht gebaut)

Aus derselben Grill-Session mit @dpa. Alles hier ist besprochen und gewollt,
aber jeweils ein eigener Lauf - nicht vergessen, nur nicht dran.

1. **Frei platzierbare Wände**, auch als unendliche Ebenen. Architektonisch
   derselbe Griff wie die Bodenreflexion: ein weiterer `PropagationPath` mit
   passendem `PathTransform` (Wand bei x = w heißt `scale.x = -1`,
   `offset.x = 2w`, siehe `PathTransform.h`). Offen ist vor allem die UI -
   wie platziert und orientiert man eine Ebene im 700x400-Feld.
2. **Mehrfach-Reflexionen / Feedback zwischen Flächen.** Bewusst
   zurückgestellt, weil es ein Stabilitätsthema ist (Spiegelquellen zweiter
   und höherer Ordnung wachsen kombinatorisch, und ein Rückkopplungsweg
   zwischen zwei Flächen braucht eine Abbruchbedingung, die weder klickt noch
   aufschaukelt).
3. **Druckwellen-/N-Wellen-Synthese für den Überschallknall**, mit eigenem
   Regler "Größe/Masse" der Quelle. Der Knall entsteht heute allein aus der
   Überlagerung mehrerer Wurzelzweige; eine echte N-Welle hätte eine eigene
   Wellenform, deren Länge von der Ausdehnung des Körpers abhängt. Hängt mit
   dem offenen Punkt "Boom klingt noch nicht richtig" unten zusammen.
4. **Mehrfach-M / "Schrot"-Quellen:** bis zu 3 unterschiedliche Quellen plus
   bis zu 20 günstige Klone davon. Dazu ein CPU-Meter, ein manueller Regler
   für die Anzahl und ein Reset-Knopf - die Klone sind der Grund für den
   Regler: die Löserlast skaliert linear mit der Pfadanzahl, und @dpa will
   sehen, was er sich gerade einkauft, statt einen stillen Deckel zu bekommen.
5. **Zwei neue Bewegungsgeneratoren:** geradlinig durch den Bildschirm und
   waagerecht querend, jeweils mit zwei Startvarianten - kontinuierlich
   einfahrend oder mit abruptem Knall-Start.
6. **Zweite, perspektivische Ansicht:** Blick in die Tiefe statt von oben,
   mit exponentiell wachsendem Feld-Blick. Erst mit z als echter Achse
   überhaupt sinnvoll; die heutige Feldanzeige zeigt z gar nicht an.

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
nahe/über Mach 1 gehaltener Bewegung.

**Update Löser-Performance (erledigt, Stand 2026-08-16):** Der Löser ist
jetzt echtzeitfähig mit reichlich Puffer. `load_check`, Block-Mittel vom
Echtzeit-Budget bei 48 kHz / 512 Samples; "vorher" ist der Zustand, wie er
tatsächlich lief (also unoptimiert gebaut, siehe Punkt 1 darunter):

| Szenario                | vorher  | jetzt |
|-------------------------|---------|-------|
| Normalfall n=200        | 14,9 %  | 1,4 % |
| Realistisch nahe Mach1  | 208,1 % | 8,9 % |
| Extrem, Mach ~3         | 328,0 % | 17,3 % |
| Extrem, Umkehr          | 458,5 % | 22,8 % |
| Extrem, Feldwechsel     |  28,9 % | 3,1 % |

Im Zielszenario "Realistisch nahe Mach1" bleibt vom Budget also gut 90 %
für Klangquelle und andere Prozessoren übrig; der schlechteste Einzelblock
liegt bei rund 2,2 ms von 10,7 ms. Drei Ursachen, in dieser Reihenfolge:

1. **Bauart.** Ohne `CMAKE_BUILD_TYPE` baute alles unoptimiert, auch die
   Standalone-App, mit der gehört wird (208 % -> 40 %). Siehe "Build & Test".
2. **Suchfenster.** Der Scan lief über die komplette Puffer-Historie, die
   nach der größten Feldgröße bemessen ist (rund 42 s), obwohl auf einem
   150-m-Feld die längste mögliche Laufzeit unter einer Sekunde liegt. Jede
   Wurzel erfüllt `c*(t_h - t_e) = R(t_e) <= |L| + max|M(t)|`, das Fenster
   beginnt deshalb bei `t_h - R_max/c` (40 % -> 34 %). Der Fall "verspäteter
   Boom aus großer Distanz" bleibt dabei per Konstruktion vollständig drin -
   ein jetzt eintreffender Boom hat genau `t_e = t_h - R(t_e)/c`.
3. **Kosten einer einzelnen Auswertung.** `SourceTrajectory::sampleAt()`
   suchte den Stützpunkt per Binärsuche über den ganzen Ringpuffer - rund
   16 Sprünge quer durch ein Megabyte pro Auswertung des Residuums, und
   das ist die häufigste Einzeloperation des Audiothreads. Das Raster ist
   gleichförmig, der Index wird jetzt direkt gerechnet; dazu eine
   positionsonly-Abtastung, weil F die Geschwindigkeit gar nicht braucht
   (34 % -> 9 %).

**Was am Scan nicht half** (gemessen, wieder verworfen - damit es niemand
ein zweites Mal versucht): eine lokal statt global bestimmte
Lipschitz-Schrittweite (streng, über eine Schranke des Catmull-Rom-
Interpolanten) war im Unterschall etwas schneller, im Überschall aber
langsamer, unterm Strich ein Nullsummenspiel. Ebenso wirkungslos: ein
exakter Frühabbruch des Rückwärts-Scans nach K gefundenen Wurzeln, und
eine gelockerte Brent-Toleranz.

Was übrig bleibt, ist die Struktur des Scans selbst: nahe der Mach-Front
ist `F' = -c*(1 - M_r)` fast null, `F` bleibt dort über eine lange Strecke
klein, und die Lipschitz-Schrittweite `|F|/Lip` kriecht entsprechend fein
durch. Das ist der Grund, warum die künstlichen Mach-3-Szenarien mit
10000-m-Feld bei 17-23 % liegen statt bei 9 %. Ein weiterer Sprung bräuchte
eine Schranke zweiter Ordnung (Beschleunigung der Quelle) oder eine über
Aufrufe hinweg gepflegte Faltungsstruktur der Ankunftszeitfunktion
`A(t_e) = t_e + R(t_e)/c`; beides ist derzeit nicht nötig.

**Offen / bekannt kaputt:**
- Ob der "Sound zerstückelt weg"-Komplex damit weg ist, muss @dpas Ohr
  entscheiden - gemessen ist die Überlastung weg, gehört wurde seitdem
  nicht. Falls Aussetzer bleiben, sind sie NICHT mehr mit CPU-Überlastung
  zu erklären und die Spur geht zurück zum vergifteten Filterzustand
  (`Branch::lpZ`, siehe oben) bzw. zum Fokusverlust-Trigger.
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
