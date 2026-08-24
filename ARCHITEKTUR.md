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

![Dopplerfeld-UI: Feldanzeige links, Regler-Panels rechts](docs/screenshot.png)

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
  Boden- und Wandspiegelung sind genau das und kein Sonderweg: dieselbe
  Klasse mit einem anderen `PathTransform` (Spiegelung an einer beliebigen
  Ebene, siehe `PathTransform.h`), dazu ein zweiter, streckenunabhängiger
  Dämpfungsgrad (`setReflectionDamping`) für die Reflexionsfläche.
  Der Löser trennt **Nachführen** (jeder Solver-Punkt) vom **Entdecken**
  (Vollscan, höchstens alle 0,5 ms, `setDiscoveryIntervalSeconds`) - siehe
  Stand-Abschnitt zur Löser-Last.
  **Einsatz und Ende eines Zweigs sind nicht symmetrisch.** Der Einsatz ist die
  lineare Anti-Klick-Rampe (`rampSeconds`, 0,5..2 ms) - eine Kegelankunft ist
  eine echte Stoßfront und darf steil sein. Stirbt ein Zweig dagegen **an der
  Kaustik** (`|1 - M_r|` innerhalb `causticWidths * eps`), bekommt er einen
  exponentiellen Schattenausläufer mit `tau = eps / |dM_r/dt|`, also der Zeit,
  in der sich `M_r` um genau eine Regularisierungsbreite bewegt. Nur so
  verschwindet er nicht bei seiner grössten Amplitude. Tode aus anderen Gründen
  (verlorene Nachführung) behalten die lineare Rampe.
  Gemessen wird das vom **Zweig-Tod-Zählwerk** (`branchDeaths()`, über den
  `FieldSnapshot` in Statuszeile und `load_check`): Anzahl, `env` beim Tod
  (Mittel/Maximum/Anteil ≥ 0,5) und Verdrängungen.
  **Bekannte Schwachstelle:** `lpZ` ist
  persistenter Filterzustand - ein einzelner nicht-endlicher Wert würde ihn
  für immer vergiften (siehe `git log` Commit "Fix: dauerhafter Sound-Ausfall"
  für einen bereits gefundenen, aber laut @dpa NICHT vollständig behobenen
  Fall dieser Bug-Klasse - Stand 2026-08-16 weiterhin ungeklärt, u.a. tritt er
  auch beim Verlust des Fensterfokus auf, was gegen einen reinen Physik-
  Edge-Case spricht).
- **`DopplerEngine`** (Physics/) - hält Quellsignal-Ringpuffer
  (`SourceSignalBuffer`), Quell-Trajektorie und **ein Pfadpaar (L/R) je
  reflektierender Fläche**: `Surface` 0 ist der Direktschall (keine
  Spiegelung, immer an), 1 der Boden, 2 und 3 die frei platzierbaren Wände -
  also acht `PropagationPath` pro `PathSet` (`pathEar`/`pathSurface`). Jede
  Fläche trägt ihre Abbildung, ihren Schalter und ihre Dämpfung; die Pfade
  holen sich beides vor jedem Block, weil eine Wand sich bewegen darf. Alle
  Pfade liegen dauerhaft bereit und werden bei abgeschalteter Fläche
  übersprungen - Umschalten allokiert damit nichts, und ausgeschaltet kosten
  sie keine Löserzeit.
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
  die `CollapsiblePanel` mit den `XyzPanel`s rechts in einem Viewport
  (Motorsteuerung, Motor, Sample, Bewegung, Feld/Physik/Ausgang, Reflexionen/
  Wände, Schwarm/Klone) - alle standardmäßig zugeklappt. 30-Hz-Timer holt
  `FieldSnapshot` ab, aktualisiert Statuszeile/Button-Texte.

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
Mach-3-Querung), prüft auf NaN/Inf, auf **Aussetzer** (längste
zusammenhängende Stille) und auf **Löser-Auswertungen pro Block**. Die
Reflexions-Szenarien fahren denselben Vorbeiflug mit und ohne Spiegelpfade und
prüfen, dass die Reflexion den Ausgang überhaupt verändert - ein nie
gerechneter Spiegelpfad würde sonst stumm durchrutschen.

Kriterien hängen ausdrücklich **nicht** an der Wanduhr: die schwankt auf einem
beschäftigten Rechner um Faktor zwei, ein Test darauf wäre ein Würfelspiel und
Regressionen verschwänden im Rauschen. Die Wanduhrzahlen werden gedruckt, aber
nichts scheitert an ihnen.

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

## Stand 2026-08-18 (Wand-Seitenerkennung, Bildquellen-Wellenfronten)

Zwei weitere @dpa-Wünsche zu den Wänden, `solver_check`/`load_check` grün,
Bau warnungsfrei.

- **Seitenerkennung ("Wand von der Rückseite muten").** Die Spiegelquellen-
  Reflexion war bislang unbedingt aktiv, egal auf welcher Seite der
  Wandebene Quelle und Hörer standen - physikalisch nur korrekt, wenn
  **beide auf derselben Seite** stehen (die reale Wand wirft den Schall in
  denselben Raum zurück, aus dem er kam; stehen sie auf verschiedenen
  Seiten, wäre die "Reflexion" ein Durchschein durch die feste Wand, das
  gibt es hier nicht). Neuer `Surface::normal` (aus `PathTransform::
  wallNormal()`, jetzt eigenständig statt nur lokal in
  `wallMirrorTransform()`) plus `DopplerEngine::wallSideGain()`: multipliziert
  bei einfacher Wandreflexion (`order()==1`, nur Wände - Index ≥ 2, nicht der
  Boden) einen Faktor auf `t.gain`, der **weich** (stetige Funktion von
  `dSrc*dLis`, keine harte Fallunterscheidung) von 1 auf 0 fällt, wenn Quelle
  und Hörer die Seite wechseln - ±1,5 m Übergangsband um den
  Ebenendurchgang, damit ein Durchqueren der Wandebene nicht klickt.
  Mehrfachreflexion (`order()==2`) bleibt davon unberührt (@dpa fragte
  ausdrücklich nur nach den Wänden; eine korrekte Erweiterung auf zwei
  Flächen wäre deutlich komplexer). Gewählt wurde **Mute statt Lowpass**
  (beides war von @dpa als Option genannt) - einfacher, kein zusätzlicher
  Filterzweig nötig, der bestehende `.gain`-Mechanismus aus dem Wand-Gain-
  Feature trägt das direkt mit.
- **Bildquellen-Wellenfronten.** Die cyan Kreise (`drawWavefronts()`) zeigen
  bislang nur den Direktschall. Neu: `FieldSnapshot::wallWavefronts[2]` und
  `wallPairWavefronts[2]` (die zwei Reihenfolgen einer Mehrfachreflexion,
  Wand0→Wand1 und Wand1→Wand0) - dieselben `wavefrontPositions`/
  `wavefrontEmitTimes` wie beim Direktschall, aber durch `applyPathTransform()`
  mit der jeweiligen Wandspiegelung geschickt. Funktioniert, weil eine
  Spiegelung ihre eigene Inverse ist: dieselbe Abbildung, die sonst den
  EMPFÄNGER spiegelt, liefert auf die QUELLE angewandt exakt die
  Bildquellen-Position - kein zusätzlicher Löser- oder Trajektorien-Code
  nötig, nur eine weitere affine Abbildung auf bereits vorhandene Punkte.
  `FieldComponent::drawReflectionWavefronts()` zeichnet sie in eigener Farbe
  (violet fuer einfache, hotpink fuer doppelte Reflexion, dünner/blasser als
  der Direktschall) - nur in der Draufsicht, nicht in der perspektivischen
  Ansicht (dort wäre die Projektion der gekippten Bildquellen auf den Boden
  ein eigenes Stück Arbeit, zurückgestellt).

## Stand 2026-08-18 (Wand-/Bounce-Gain, Schwarm-Streuung, M-Source-Jitter)

Drei @dpa-Wünsche aus `dd.md` umgesetzt, `solver_check`/`load_check` grün,
Bau warnungsfrei.

- **Wand-Gain und Bounce-Gain-Boost.** `wall1Damp`/`wall2Damp` waren schon
  immer reine Tiefpässe mit Gleichstromverstärkung 1 (nehmen nur Höhen,
  keinen Gesamtpegel) - eine einzelne Wandreflexion (`order()==1` in
  `DopplerEngine::recipeTransform()`) hatte deshalb bislang **keinen**
  Pegelregler. Neue Parameter `wall1Gain`/`wall2Gain` (dB, ±36) landen in
  `Surface::transform.gain`; `composeTransforms()` (`PathTransform.h`)
  verkettet `outer.gain * inner.gain` ohnehin schon automatisch, die
  Mehrfachreflexion bekommt den Wandgain also geschenkt mit. `bounceGain`
  (der Generationsfaktor <1, siehe Stand 2026-08-17 "Mehrfachreflexion")
  bleibt unverändert - die Garantie "jede weitere Generation wird leiser"
  wäre sonst gebrochen. Stattdessen neuer, unabhängiger Boost-Parameter
  `bounceGainDb` (`DopplerEngine::bounceGainBoost`, ohne Klemmung nach oben)
  obendrauf. Alle drei Gains wirken nur auf `.gain`, nie auf den Tiefpass -
  die Wand bleibt trotz Höhenverlust im Pegel steuerbar.
- **`cloneSpread`-Range** 0-200 m → 0-1000 m (Skew-Centre 3 → 15 m), sonst
  unverändert - @dpa wollte den Schwarm "mehr, weiter" auseinanderziehen
  können.
- **`PositionJitter`** (neu, `Source/Motion/`, JUCE-frei wie der Rest des
  Ordners): additive Mikrobewegung der Quelle M (**nicht** des Hörers -
  M meint Motor/Sender, das runde Symbol in `FieldComponent::drawSource()`).
  Drei unabhängige Sinusoszillatoren je Achse, deren Momentanfrequenz über
  einen zweckentfremdeten `OnePoleSmoother` (glättet ein Vec3-Frequenztripel
  statt einer Position) langsam zwischen zufällig gewürfelten Zielwerten
  driftet - dadurch bleibt `d(position)/dt` stetig und die Bewegung klickfrei,
  ganz ohne eigenen Positions-Glätter. Zwei neue Parameter `srcJitterAmount`
  (m, Default 0 = aus) und `srcJitterRateHz` ("Hektik", Default 0,2 Hz).
  Eingehakt in `PluginProcessor::advanceMotion()` **additiv auf `target`,
  bevor** `sourceSmoothers`/`bypassSmoothing` greifen (@dpa-Vorgabe: "Jitter
  Addition vor den Smoothern") - dadurch läuft die gejitterte Position durch
  denselben Weg wie jedes andere Bewegungsziel und landet unverändert in
  `smoothedSourcePos` → `dopplerEngine.setSourceTarget()` →
  `snapshot.sourcePos`. Das sichtbare Quellensymbol wackelt damit automatisch
  mit, ohne dass `FieldComponent`/`FieldSnapshot` angefasst werden mussten.
  Immer additiv aktiv (kein Sonderfall für Stillstand): bei Bewegung geht der
  kleine Jitter im normalen Doppler unter, im Stillstand ist er die einzige
  Bewegung und dominiert von selbst - "echter Chorus" ergibt sich ohne
  Zustandsautomat.
- **UI:** `WallPanel` bekam je Wand einen sechsten Knob (Gain) - die Reihe
  wurde dafür von 84px auf 70px Knopfbreite verschmälert, damit sie in der
  Panel-Breite bleibt; dazu ein `Bounce Boost`-Knob neben `Bounce Gain`.
  `FieldPanel` bekam eine dritte Knob-Reihe (`Jitter`/`Hektik`) unter der
  Höhen-Reihe (`fieldContentHeight` 218 → 306).



Nachlauf hatte einen echten Bug (@dpa-Repro): bei "Slew Limiter" lief er ein
Stück, bremste aber nicht sichtbar. Ursache war ein Design-Fehler der
ersten Fassung, kein Solver-/Physik-Bug - siehe `git log` ("Nachlauf-Fix:
analytischer Zielpunkt statt eigenem Simulations-Timer") für die Herleitung.
Fix: statt eines eigenen 60-Hz-Simulations-Timers wird beim Loslassen EIN
analytisch integrierter Endpunkt gesetzt (`v0*halfLife/ln(2)`), der
jeweils aktive Smoother bremst mit seiner EIGENEN Kurve dorthin - kein
zweiter, konkurrierender Bremsmechanismus mehr. Der "Nachlauf"-Schalter
sitzt jetzt im "Bewegung"-Panel statt in der Kopfzeile.

**Motor-Gating** (`DopplerfeldProcessor::setMotorGateEnabled`, Schalter
"Motor bei Griff" in "Motorsteuerung", Default aus): Motor klingt nur,
während/nachdem M gegriffen ist. mousedown faedet schnell ein (~30ms),
mouseup wartet erst auf die Ruheposition (dieselbe 0,05-m/s-Schwelle wie
der Nachlauf, inkl. eines evtl. laufenden Nachlaufs) und faedet dann über
2,5s ruhig aus. `FieldComponent` meldet nur die rohen Greif-/Loslass-
Ereignisse (`onSourceGrabbed`/`onSourceReleased`), kennt das Feature selbst
nicht - Zustandsmaschine (Sustaining/Attacking/AwaitingRest/Releasing/Idle)
und die eigentliche Gain-Rampe leben im Processor, direkt am Mono-Puffer
der Motor-Quelle, DopplerEngine/Physik unberührt. Wirkt nur bei Quelle
"Motor" und nur auf M, nicht L (Klärung per Rückfrage). **Ungehört.**

**Record-Diagnose** (noch offen, kein Fix): @dpa berichtet, eine über die
ganze Fläche aufgenommene Bewegung spiele nur in kleinen Kreisen um den
Anfang ab. Preset `presets/irgendwas ist mit rec kaputt` von Hand dekodiert
(JUCE-eigenes Base64-Format, `<Bytelänge>.<Daten>`, siehe
`MemoryBlock::toBase64Encoding` in JUCE) und ausgewertet:

- Die Daten selbst sind NICHT leer/korrupt: 9276 Frames (46,4 s), x
  2026–3278 m, y 789–1412 m bei `fieldMetres` 3580 m - ein plausibler,
  nicht-trivialer Ausschnitt.
- Aber: Weglänge 17713 m gegen eine Bounding-Box-Diagonale von nur 1399 m
  (Faktor 12,7×) - viel Hin-und-Her/Schleifen auf engem Raum, und die Box
  deckt auch nur ~35 %/30 % der Feldbreite/-höhe ab, nicht die ganze Fläche.
- `CriticallyDampedSpring::tick()` (aktiver Smoother bei dieser Aufnahme,
  τ≈0,18s) sieht bei Code-Lektüre korrekt kritisch gedämpft aus (kein
  Überschwinger-Bug wie neulich beim Slew Limiter) - Verdacht deshalb:
  `MotionRecorder` zeichnet bewusst die GEGLÄTTETE (nicht die rohe) Position
  auf; ein schneller, richtungswechselnder Sweep durch einen 180ms-Glätter
  rundet Ecken ab und bleibt hinter schnellen Ausschlägen zurück - das
  könnte genau dieses Bild erzeugen, WÄRE aber kein Bug, sondern erwartetes
  Lag-Filter-Verhalten. Nicht verifiziert: ob die Bewegung schon beim
  LIVE-Ziehen so kringelig aussah (dann Smoother-Charakteristik) oder erst
  bei der Wiedergabe (dann echter Record/Playback-Bug) - @dpas Antwort
  darauf steht noch aus, bevor hier weitergesucht wird.

## Stand 2026-08-17 (Nacht: Cockpit-Tempo, Motorsteuerung, Audio In, Nachlauf)

Vier UI-/Quellen-Features aus einer Runde, `solver_check`+`load_check` grün,
warnungsfrei. Alles ungehört - reine Umsetzung, kein Hördurchgang dazu.

- **Cockpit-Tempoanzeige im Feld** (`FieldComponent::drawSpeedReadout()`):
  dieselbe Einheit wie der `speedUnitButton` (km/h/m/s/Mach), oben rechts im
  schwarzen Feld, ~140x50px, Alpha-Gelb `#ffff0055`, kontrastarmer Rahmen
  (Sanfte-Rahmen-Konvention statt eines hellen). Läuft in beiden Ansichten
  (Draufsicht + Perspektive). Formel/Einheiten-Zuordnung sitzt jetzt EINMAL
  in `FieldComponent::formatSpeed()` (statisch), die Statuszeile
  (`PluginEditor::statusText()`) ruft dieselbe Funktion statt einer zweiten
  Kopie des Switch.
- **"Motorsteuerung" als eigenes Panel**: RPM und Imbalance aus "Motor"
  herausgezogen (`Source/UI/EngineControlPanel`) - das sind die Regler, die
  man live/oft anfasst, während der Rest von "Motor" (Harmonische,
  Rauschband, Jitter) Klangdesign ist. Eigenes Panel statt Extra-Abschnitt,
  damit es ohne "Motor" aufzuklappen erreichbar ist.
- **Alle Panels starten zugeklappt** (`CollapsiblePanel::expanded`-Default auf
  `false` gedreht) - vorher stand "Motor" (und zeitweise "Feld/Physik/
  Ausgang") beim Öffnen aufgeklappt da.
- **"Audio In" als dritte Quelle** neben Motor und Sample
  (`Source/Sources/AudioInSource`). `SoundSource` ist PULL-basiert
  (`renderMono()` liest nur), der Host liefert Audio aber PUSH-artig einmal
  pro `processBlock()` - `pushBlock()` ist die Brücke, vom Processor VOR dem
  bisherigen `buffer.clear()` gerufen (der Kommentar dort - "Instrument ohne
  Eingang" - gilt jetzt nur noch für den AUSGANG). Dafür bekam der Prozessor
  erstmals einen Eingangsbus (`BusesProperties().withInput(..., mono, true)`,
  `isBusesLayoutSupported()` erlaubt mono ODER deaktiviert). Die alte
  bool-Quellwahl (`useSampleSource`) wurde zu einem 3-wertigen
  `SourceKind`-Enum (`selectSourceKind()`/`currentSourceKind()`) - eine
  Fallunterscheidung (`sourceForKind()`) statt der Bool-Prüfung an zwei
  Stellen dupliziert. **Einschränkung:** `IS_SYNTH TRUE` bleibt unverändert;
  ob ein Host einem so deklarierten Plugin überhaupt einen Audio-Eingang
  anbietet, ist formatabhängig (VST3/Standalone ja, manche AU-Instrument-
  Hosts bieten dafür keine Eingangsroutingoption an) - ungeprüft, "wenn
  möglich" war die Vorgabe.
- **Nachlauf nach `mouseUp()`** (`FieldComponent`): Quelle/Hörer laufen mit
  der zuletzt gezogenen (leicht geglätteten) Geschwindigkeit noch kurz weiter
  und bremsen exponentiell ab (Halbwertszeit 0,15 s als Modellkonstante),
  statt abrupt stehenzubleiben. Läuft über einen `juce::Timer` (60 Hz), der
  denselben `onSourceDragged`/`onListenerDragged`-Rückkanal wie ein echter
  Drag bedient - keine neue Verdrahtung nötig, aus Sicht des Processors sieht
  ein Nachlauf wie eine sehr feine Automation aus. Nur Positions-Drags in der
  Draufsicht (Quelle, Hörerposition); Perspektive und Kopfdrehung bleiben
  außen vor. Ein neuer Griff (`mouseDown`) bricht einen laufenden Nachlauf
  sofort ab. Zu-/abschaltbar über den Kopfzeilen-Knopf "Nachlauf"
  (`FieldComponent::setCoastEnabled()`), Default an - reines
  Bedienungsgefühl, kein Parameter, wie `tooltipsButton`.

## Stand 2026-08-17 (Abend: Slew-Regler ausgrauen, Fly Approach entkoppelt, Stille-Diagnose)

**Zwei kleine Fixes**, `solver_check`+`load_check` grün, warnungsfrei:

- **Slew Vmax/Amax** im Bewegung-Panel sind jetzt nur bei Smoother "Slew
  Limiter" aktiv (`setEnabled`), bei jedem anderen Verfahren ausgegraut - vorher
  bedienbar, aber wirkungslos, standen sie einfach so da.
- **"Fly Dist" steuerte ungewollt zwei Groessen**: `FlyByGenerator::halfLength()`
  = max(100, 6·|distance|) hing am seitlichen Vorbeiflugabstand, ein Regler
  kontrollierte damit Abstand UND Anflugstrecke/Startpunkt der Bahn zugleich.
  Neuer, eigenstaendiger Parameter **Fly Approach** (`Params::flyApproach`,
  Default 300 m) uebernimmt die Bahnlaenge.

**Diagnose (noch OFFEN, kein Fix):** @dpa berichtet bei ~2000 km/h einen
kurzen hohen Pegel, der nach rund 250 ms abstandsabhaengig abbricht, bei
~1500 km/h nicht. Ein neues `load_check`-Szenario (reiner Diagnose-Block,
keine Assertion, im Quelltext nach dem N-Wellen-Test) fliegt beide Tempi
(555,56 und 416,67 m/s) bei mehreren Vorbeiflugabstaenden und misst die
laengste zusammenhaengende Stille waehrend des Fluges:

- **Bestaetigt: echte Stille, keine CPU-Ueberlast.** Blockzeiten liegen bei
  13-37 % vom Budget (kein Block nahe der Grenze), aber der Ausgang wird fuer
  150-800 ms **exakt** 0.0 (nicht nur leise) - abstandsabhaengig wachsend
  (5 m ≈ 150-330 ms, 300 m ≈ 590-810 ms).
- **Verdaechtigter Mechanismus, mit Beleg, aber nicht bestaetigt:**
  `DopplerEngine::configurePendingSet()` fuellt beim Start der linearen
  Vorgeschichte (`SourceTrajectory::fillLinear`) die Bahn nur soweit rueckwaerts,
  wie
  `allowed = (0.9·331.3·maxHistorySeconds - startR) / preSpeed`
  erlaubt (Datei `Source/Physics/DopplerEngine.cpp`, `configurePendingSet()`).
  **`allowed` faellt umgekehrt proportional zur Fluggeschwindigkeit** - eine
  schnellere Quelle bekommt eine KUERZERE gueltige Vorgeschichte. Im
  Block-Trace (2000 km/h, d=20 m) blieb der Anzeigewert `delaySeconds` eines
  Pfades ueber die gesamte Stille-Phase bitgleich bei 36,3355 s eingefroren
  (waehrend `residualEvaluations()` im selben Fenster kontinuierlich weiter
  waechst, der Loeser also aktiv bleibt) - ein Wert nahe an, aber unter
  `maxHistorySeconds` (≈ 41,8 s bei n_max = 10000 m). Passt zu einer Wurzel,
  die aus der wegen `allowed` verkuerzten/gepadeten Vorgeschichte keinen
  gueltigen Signal-Lesepunkt mehr findet (`SourceSignalBuffer::readAt()`
  liefert 0,0f ausserhalb des geschriebenen Bereichs, siehe Kommentar dort).
  **Nicht** bestaetigt: der exakte Code-Pfad, der den Zweig danach wieder
  freigibt (env muesste nach ~1 ms auf 0 laufen und den Slot freigeben - warum
  "Zweige" in der Anzeige die ganze Stille ueber bei 1 verharrt, ist noch
  ungeklaert).
- **Nicht deckungsgleich mit @dpas Beobachtung:** im synthetischen Sweep
  (fester `flyApproach` = 300 m fuer beide Tempi) ist die Stille bei
  1500 km/h *laenger* als bei 2000 km/h, nicht kuerzer/abwesend wie berichtet -
  vermutlich weil @dpas tatsaechliche Regler-Kombination (Fly Approach, Fly
  Dist, Smoother, evtl. Bodenreflexion/Waende) von diesem Sweep abweicht. Der
  Sweep beweist also: der Bug ist real und nicht CPU-bedingt, aber nicht,
  dass er bei genau diesen zwei Tempi exakt so auftritt wie gehoert.

**Naechster Schritt:** gezielte Session in `RetardedTimeSolver.cpp`/
`PropagationPath.cpp`/`DopplerEngine::configurePendingSet()` - Kandidaten:
entweder `allowed` grosszuegiger bemessen (die 0,9-Sicherheitsmarge oder die
0°C-Referenzgeschwindigkeit lockern), oder den Fall "keine gueltige
Vorgeschichte am gewuenschten Punkt" explizit behandeln statt still auf 0 zu
lesen. Empfehlung: hoher Denkaufwand (Opus), weil physikalisch-numerisch
kritischer Code mit bestehender `solver_check`-Abdeckung, kein Schnellschuss.

## Stand 2026-08-24 (Betriebsarten sind eigene Klangerzeuger)

Der erste Versuch hat die Betriebsarten als GEWICHTUNG derselben Bausteine
gebaut. Das war zu wenig, und @dpa hat es sofort gehört: "Bei Düsenantrieb
höre ich nur einen 5. Osc, sine, was hast Du dir dabei gedacht?" - denn genau
das war es, ein zusätzlicher Sinus über dem unveränderten Motorklang. Die
Ansage dazu: "jeder Betriebsmode ist ein eigener Generator!"

Jetzt hat jede Betriebsart ihren eigenen Aufbau, und gerechnet wird nur, was
sie braucht:

- **Frei** - der Motor wie immer: vier Teiltöne, Rauschband, Jitter, Unwucht.
  Hier liegen die alten Snapshots, und hier ändert sich nichts.
- **Düsenantrieb** - EIN Verdichterton (Sägezahn an der Fanblattfolge, hoch
  und leise), darüber das Strahlrauschen, das den Klang trägt. Keine vier
  Teiltöne: "braucht es höchstens 1 Oscillator".
- **Raketenantrieb** - kein Ton, nur Brüllen, dazu die Druckstöße der
  Stoßzellen im Abgasstrahl. Unregelmäßig getaktet (halber bis
  anderthalbfacher mittlerer Abstand - Stoßzellen sind keine Maschine mit
  fester Drehzahl) und hörbar auch unterschallig: überschallschnell ist der
  STRAHL, nicht die Rakete.
- **Hubschrauber** - die vier Teiltöne bleiben als Verbrennermotor, dazu der
  Rotor: bandpassgefiltertes Schwirren, dessen Pegel mit jedem Blatt atmet,
  plus ein kurzer harter Rauschstoß je Blatt. Der frühere DC-Impuls
  (Kosinus hoch 24) war fast immer null - deshalb war "von Hubschrauber nichts
  zu hören".
- **Propeller** - ein einzelner leiser Ton plus derselbe Blattschlag, weicher,
  dazu das Propellerpaar in der Geometrie (siehe voriger Abschnitt).

**Fahrtwind** in allen Betriebsarten außer "Frei": hochpassgefiltertes
Rauschen, das mit der Geschwindigkeit lauter wird und dessen Zischen mit ihr
höher rückt. Nicht in "Frei", weil dort die alten Snapshots liegen.

**Pegel**: eigener Regler je Betriebsart, Default +12 dB. Ihre Lautstärke
kommt aus der Sache und nicht aus vier einzeln gedrehten Teiltönen - ein
Hubschrauber in drei Metern ist ohrenbetäubend.

**Sinus je Oszillator** statt eines gemeinsamen Schalters, aus derselben Phase
wie der Sägezahn.

**Das Panel ist je Betriebsart schmal** (@dpa: "nur das nötigste"). Nicht
gebrauchte Regler werden nicht ausgegraut, sondern ausgeblendet - bei Düse und
Rakete verschwindet die ganze Teilton-Matrix samt Rauschband. Damit hängt die
Panelhöhe an der Betriebsart: `EnginePanel::preferredContentHeight()` führt
sie, der Editor fragt sie ab, statt eine Konstante zu halten. Sichtbarkeit,
Layout und Höhe kommen aus EINER Liste (`kindKnobs()`), sonst liefen sie
auseinander.

**Beschriftungen**: In allem, was der Benutzer sieht, stehen echte Umlaute
("Düsenantrieb"). Fachabkürzungen in den Hilfetexten sind erklärt, allen voran
M_r als die auf den Hörer bezogene Mach-Zahl - die Geschwindigkeitskomponente
GENAU IN RICHTUNG des Hörers, geteilt durch die Schallgeschwindigkeit. Eine
Quelle kann mit Mach 3 fliegen und ein M_r nahe null haben, wenn sie quer
vorbeizieht.

## Stand 2026-08-24 (Motor-Betriebsarten und Propellerpaar)

@dpa wollte im Motor "mehrere umschaltbar": Duesenantrieb, Raketenantrieb,
Hubschrauber (Motor plus Rotoren mit eigener Geschwindigkeit) und zwei
Propeller an Fluegeln. Das sind zwei ganz verschiedene Dinge, und sie liegen
darum auch an verschiedenen Stellen.

### Drei Klang-Betriebsarten im Generator

`Params::engineKind` ist ein Choice mit fuenf Eintraegen ("Frei",
"Duesenantrieb", "Raketenantrieb", "Hubschrauber", "Propeller"; die
Reihenfolge ist bindend fuer `EngineGenerator::kindWeightTable`). Die Wahl
**ueberschreibt keinen einzigen Regler** - sie gewichtet, was die vorhandenen
Bausteine beitragen, und schaltet je Art einen eigenen Zusatzklang dazu:

- **Duese**: Rauschband dominiert, Teiltoene treten zurueck, dazu ein
  Turbinen-Pfeifton auf dem Zwoelffachen der gejitterten Grundfrequenz.
- **Rakete**: fast nur breitbandiges tiefes Rauschen mit eigenem Filter und
  eigenem Zufallsgenerator - eine Rakete hat keine rotierenden Teile.
- **Hubschrauber**: Motorton unveraendert, dazu der Rotor als eigener
  Baustein mit EIGENER Drehzahl (`heliRotorHz`) und Blattzahl
  (`heliBladeCount`). Ihr Produkt ist die Blattschlagfrequenz, geformt als
  Kosinus-Potenz - ein scharfer kurzer Schlag statt einer glatten Welle.
- **Frei** und **Propeller** stehen auf den Identitaetsgewichten, klingen also
  wie bisher.

Alle Gewichte werden sample-genau ueber 50 ms nachgefuehrt, dasselbe Muster
wie beim Sinus-Umschalter. Ein harter Wechsel mitten im Ton waere ein Sprung.

### Das Propellerpaar ist Geometrie, kein Klang

Zwei Propeller an Fluegeln sind zwei SCHALLQUELLEN an verschiedenen Orten,
nicht ein zweiter Klang - sie gehoeren deshalb in `DopplerEngine`, nicht in
den Generator. Umgesetzt als zwei zusaetzliche Pfadpaare mit reinem
Direktschall (`PathRecipe::prop`), genau wie die Klone: dauerhaft
bereitliegend, uebersprungen solange sie aus sind, und dann auch ohne
Loeserlast.

Der Unterschied zu einem Klon steckt im Versatz. Er steht nicht im Raum fest,
sondern quer zur Flugrichtung und waagerecht - "immer flach in der Richtung
des fluges". Berechnet wird er im Processor, weil dort die Bewegung bekannt
ist: aus der tatsaechlich zurueckgelegten Strecke je Tick, damit er fuer Maus,
Vorbeiflug und Wiedergabe gleichermassen stimmt.

Zwei Feinheiten, die nicht weggelassen werden duerfen:

- Die Richtung wird **geglaettet** (0,15 s). Der Versatz IST eine Position;
  zappelte die Richtung, zappelten die beiden Schallquellen mit, und ein
  Positionssprung ist formal Ueberschall.
- Bei **Stillstand** bleibt die zuletzt bekannte Richtung stehen, statt auf
  null zu fallen - sonst klappten die Fluegel im Stand zusammen.

Die Propeller kommen zum Rumpfschall HINZU, statt ihn zu ersetzen.
Spannweite (`propSpan`) und Pegel (`propLevelDb`) sind eigene Regler.

## Stand 2026-08-24 (Bewegungssprung hoerbar, Begrenzer am Meter, Motor-Sinus)

### Der Knall-Start war nicht zu hoeren - warum, und was jetzt hilft

@dpa: "der Vorbeiflug 'Knall-Start' muesste ja mindestens subsonic zu hoeren
sein ... Bisher ist noch nicht zu hoeren!" Nachgemessen stimmte das: im
`load_check` lieferten weicher und abrupter Start im Startfenster dieselbe
Spitze, und im Restfenster lag der steilste Anstieg bei beiden bei 19,0 dB
zur selben Zeit - das ist die Passage, kein Einsatz des Starts.

Der Grund ist strukturell. Die N-Wellen-Schicht hat genau einen Ausloeser,
M_r = 1, und bleibt unterschallig stumm. Was vom Knall-Start uebrig bleibt,
ist ein Sprung im Amplitudenfaktor 1/(1-M_r) plus der zugehoerige
Tonhoehensprung - und der wird ueber die Laenge eines Solver-Segments
(64 Samples, 1,33 ms) interpoliert, wird also zur Rampe statt zur Kante.

Zwei Wege dagegen, beide schaltbar, beide aus per Default:

- **Sprungkante** (`jumpEdge`): Amplitude und Leseposition stehen im
  betroffenen Segment sofort auf ihrem Zielwert.
- **Sprungknall** (`jumpBoom`): eine Druckwelle darauf, ueber dieselbe
  N-Wellen-Schicht, mit einer Amplitude, die mit der Sprunghoehe waechst.
  Ein Geschwindigkeitssprung ist formal unendliche Beschleunigung, und die
  strahlt physikalisch eine Druckwelle ab.

**Wie der Sprung erkannt wird, und wie nicht.** Der erste Ansatz suchte einen
Sprung von M_r innerhalb eines Segments. Das ist falsch, und die Messung zeigt
warum: im normalen Ueberschallflug aendert sich M_r an der Kaustik um bis zu
0,15 je Segment, beim Start aus dem Stand um 0,58 - die Bereiche ueberlappen,
jede Schwelle darauf feuert bei jeder schnellen Bewegung. Stattdessen legt die
Engine beim Umschreiben der Bahn eine **Marke** ab (Zeitpunkt in Quellzeit plus
Sprunghoehe in m/s, `DopplerEngine::markSourceJump`), und jeder Hoerweg merkt
die Kante, sobald seine eigene Emissionszeit darueber laeuft. Das ist exakt
statt geraten und gilt je Zweig einzeln - ein zeitverkehrt gehoerter Zweig
laeuft spaeter darueber als der vorwaerts laufende.

**Was jeder Weg bringt** (`load_check`, Fall "Knall-Start", Vorbeiflug 200 m/s,
Start 243 m entfernt): die Druckwelle hebt die Spitze im Ankunftsfenster von
0,0179 auf 0,1697, also um gut 19 dB. Die Kante allein aendert am Ausgang so
gut wie nichts - Spitze und groesster Samplesprung bleiben gleich. Der Grund
steht in den Zahlen des Segments, in dem sie ankommt: die Amplitude springt
dort von 0,00404 auf 0,00986, also auf einem sehr leisen Zweig. Im Test steht
deshalb bewusst keine Pruefung auf einen Unterschied, den es nicht gibt.

### Stossfront: auch zwischen den beiden Knallen still

Die Absenkung des uebrigen Schalls waehrend einer N-Welle steht jetzt voll
aufgedreht als Voreinstellung, und der Regler fuer die Rueckkehrzeit ist
entfallen (fest 10 ms). @dpa: "es ist immer was zu hoeren zwischen den zwei
knallen.. das soll weg" - gemeint ist die ganze Welle, auch die Strecke
zwischen Bug- und Heckstoss, nicht nur die beiden Fronten.

### Begrenzer sitzt am Pegelmesser, nicht an der CPU-Zeile

@dpa: "Dann hat es bei der CPU Anzeige nichts zu suchen! Es gibt ja das Meter,
das hat die roten clip anzeigen - das ist die Anzeige, da gehoert sie hin.
ohne extra anzeige." Die Textmarke in der Loeserlast-Zeile ist weg; der
Begrenzer meldet sich auf der vorhandenen Clip-Marke des Pegelmessers, mit
deren 500-ms-Haltezeit und ohne zusaetzliches Anzeigeelement. Inhaltlich ist
das auch der richtige Ort: er greift gerade, DAMIT nichts uebersteuert - sonst
bliebe die Marke dunkel, waehrend der Ausgang an der Obergrenze klebt.

### Motor: vier Teiltoene wahlweise als Sinus

`engineSine` macht aus den vier PolyBLEP-Saegezaehnen reine Sinus, fuer alle
vier gemeinsam. Der Sinus kommt aus DERSELBEN Phase - kein zweiter Oszillator,
sonst liefen beim Umschalten zwei Phasen auseinander - und umgeschaltet wird
ueber 20 ms geblendet: bei Phase 0,25 steht der Saegezahn auf -0,5 und der
Sinus auf +1, ein harter Wechsel waere ein Sprung.

### Reglergroesse

@dpa wollte die Knoepfe auf zwei Drittel, und zwar (Berichtigung) **nur das
Drehrad**: Beschriftung (18 px) und Wertefeld (80x18) bleiben, wie sie waren.
Die Zellenhoehe schrumpft deshalb genau um das Drittel, das dem Rad gehoert
(82 -> 67 px, im Bewegungs-Panel 100 -> 79), die Breite bleibt - sonst wuerde
das Wertefeld beschnitten. JUCE zeichnet das Rad mit dem kleineren der beiden
Masse, die Hoehe allein macht es also klein. Die Panelhoehen in
`PluginEditor.h` ziehen um genau diese Differenz je Reglerreihe nach.

## Stand 2026-08-23 (Rotoren, Vorbeiflug-Schleife, Luft, Rueckwaerts-Anteil)

### Rotoren - zweite Betriebsart des Bewegungs-Wacklers

`PositionJitter` kann jetzt zweierlei: das bisherige Wackeln aus drei
unabhaengigen, langsam driftenden Sinussen, und eine gleichmaessige Kreisbahn
(@dpa: "statt Jitter Rotoren"). Im Rotoren-Modus bekommen die beiden
vorhandenen Regler eine andere Bedeutung - der Ausschlag ist der Radius,
"Hektik" heisst "Speed" und ist die Umlaufgeschwindigkeit. Zwei Regler kommen
dazu und sind nur dort sichtbar:

- **Randomize** 0 = sauberer Kreis mit konstantem Tempo, hoch = starke
  Temposchwankungen. Nutzt denselben geglaetteten Frequenzwuerfel wie die
  Hektik, nur multiplikativ um 1 herum (Faktor 1/4 bis 4, gleichverteilt im
  Logarithmus) und mit Randomize dazwischengeblendet.
- **Z-Jit** kippt die Kreisebene, 0 = flach in xy, 1 = senkrecht. Der Radius
  bleibt dabei gleich, der Anteil wandert stetig von y nach z.

Zwei Dinge, die nicht offensichtlich sind:

- Der **Moduswechsel** wird ueber 200 ms vom zuletzt ausgegebenen Versatz aus
  ueberblendet. Ohne das waere der Formelwechsel ein Positionssprung, und ein
  Positionssprung ist formal Ueberschall (siehe TODO "Bahn-Historie").
- **Startphasen** kommen aus dem eigenen Zufallsgenerator statt aus der Null.
  Sonst stuenden alle Klone im selben Punkt ihrer Kreisbahn und drehten
  sichtbar wie ein einziger Koerper.

Die Tempogrenze laeuft wie bisher ueber die Frequenz, nicht ueber den
Ausschlag - der Kreis wird langsamer statt kleiner.

### Vorbeiflug in Dauerschleife

`Params::flyLoop`. Am Streckenende faengt derselbe Flug von vorn an. Der
Neustart wird in der Tick-Schleife nur **vorgemerkt** und am Anfang des
naechsten `advanceMotion()` ausgefuehrt: er setzt Glaetter und Geometriesatz
neu, und die Tick-Schleife haengt an `nextTrajectoryTime()` - ein Eingriff
mitten darin waere ein Eingriff in ihre eigene Laufbedingung. Ausgefuehrt wird
`startFlyBy()`, also derselbe Weg wie bei einem frisch ausgeloesten Flug samt
Vorgeschichte und Ueberblendung. Mit Startvariante "Knall-Start" beginnt
folgerichtig jeder Durchgang schlagartig; das ist dort der Zweck.

### Luft: Temperatur und Hoehe

`MediumState` ist nicht mehr fest auf 20 Grad:

- **airTempC** war als Feld vorbereitet, aber nirgends verdrahtet. Jetzt
  bestimmt er c(T) und damit die Mach-Schwelle - der Unterschied zwischen
  Peitsche auf Meereshoehe (343 m/s) und Jet in grosser Hoehe (295 m/s).
- **airAltitude** wirkt ueber die barometrische Hoehenformel der
  Standardatmosphaere auf den Druck und von dort auf die Dichte, und die
  Dichte auf den Pegel. **Nicht** auf die Temperatur: die bleibt ein eigener
  Regler, sonst gaebe es zwei Werte fuer dieselbe Groesse.

Der Dichtefaktor haengt am Ausgangspegel (`outputGainLinear`), nicht in den
Pfaden, und ist gegen die Defaultwerte normiert - bei 20 Grad und 0 m kommt
exakt 1.0 heraus, damit bestehende Presets nicht leiser werden.

### Der rueckwaerts gehoerte Anteil

Bei Ueberschall liefert der Loeser mehrere Hoerwege; die Leseposition eines
Zweigs wandert mit (1 - dTau), ueber dTau = 1 also rueckwaerts. @dpa hoert
diesen Anteil als zu praesent und als abreissend ("es muss neben dem
Rueckwaerts irgend etwas lauteres geben, sonst waere es hoerbar" /
"klingt so als waere das schon korrekt, nur dass es ploetzlich aufhoert").
Drei Regler dafuer, alle mit dem bisherigen Verhalten als Default:

- **reverseGainDb** senkt nur die zeitverkehrt gehoerten Zweige, ueber dTau
  geblendet statt geschaltet (Breite 0,25, sonst waere der Uebergang ein
  Pegelsprung mitten im Signal). Die N-Welle bleibt unberuehrt - der Knall ist
  eine eigene Schicht, kein Zweiginhalt.
- **shadowTailMs** ist die Untergrenze des Kaustik-Ausklangs. Rechnerisch
  folgt der aus eps / (dM_r/dt), praktisch faellt er bei schnellen
  Vorbeifluegen immer auf `rampSeconds` = 1 ms - gemessen im `load_check`:
  alle Kaustik-Tode mit Ausklang 1,000 ms bei Todespegel 1,000, also voller
  Lautstaerke. Ein voll ausgesteuerter Zweig, der in einer Millisekunde weg
  ist, reisst hoerbar ab. Physikalisch liegt hinter der Kaustik eine
  Schattenzone, in die gebeugter Schall weiterlaeuft; wie lang, haengt an
  Geometrie und Frequenz - deshalb ein Regler statt einer erfundenen
  Konstante.

Zur **Geschwindigkeit** des Rueckwaertslaufs, weil die Frage wiederkommt: sie
ist 1/(M_r - 1), nicht pauschal 0,5 bis 1,0. Genau 1,0 gilt bei M_r = 2, bei
M_r = 3 ist es 0,5, bei M_r knapp ueber 1 laeuft der Anteil **schneller** als
Echtzeit. Der Wert folgt hier aus der Geometrie und wird nirgends gesetzt.

### Waehrend der Stossfront kommt nichts anderes durch

**shockDuckAmount / shockDuckMs** senken den uebrigen Schall, solange eine
N-Welle ueber den Hoerweg laeuft, und lassen ihn danach mit einstellbarer Zeit
zurueckkommen (@dpa: "hoechstens ein luftholen-geraeusch! aber keine Noise vom
Motor").

Zwei Entscheidungen dahinter:

- **Pfadweit, nicht je Zweig.** Der Motorton laeuft sonst ueber den
  Nachbarzweig weiter. Gefuehrt wird das als gemeinsamer **Zeitpunkt**
  (`shockEndTime`) statt als Huellkurve: die Zweige laufen nacheinander ueber
  denselben Sample-Bereich, ein gemeinsamer Huellkurvenzustand liesse sich so
  nicht fortschreiben, ein gemeinsamer Zeitpunkt schon.
- Zweimal je Solver-Segment ausgewertet und dazwischen linear geblendet, statt
  ein `exp()` pro Sample und Zweig.

Die N-Welle selbst wird nicht abgesenkt.

### Begrenzer-Anzeige

Die Marke unter dem CPU-Balken stand nur einen Block lang da
(`limiterHitCount` wird je Block neu gesetzt) und war unbeschriftet. Jetzt
fester Textplatz rechts in der Zeile, immer sichtbar, nur die Farbe wechselt,
mit 900 ms Nachleuchten im Editor (kein Eingriff in den Processor) und einem
Tooltip, was da begrenzt wird.

## Stand 2026-08-17 (Nachmittag: vier Bugfixes, vier neue Regler)

Aus einem echten Hördurchgang mit @dpa, direkt im Anschluss an z-Achse/
Bodenreflexion. `solver_check` und `load_check` grün, warnungsfrei.

**Bugfixes:**

- **Slew Limiter schwang bei Berührung ungedämpft um sein Ziel.** Die
  Zielgeschwindigkeit zielte bis zuletzt mit vollem `v_max` aufs Ziel, ohne
  Bremsstrecke einzuplanen - schoss zwangsläufig drüber hinaus, kehrte mit
  derselben Wucht um, traf von der Gegenseite wieder mit voller Geschwindigkeit
  auf. Fix: Zielgeschwindigkeit auf `min(v_max, sqrt(2·a_max·d))` gekappt
  (Standard-Bremskurve).
- **Play Speed wirkte bei CatmullRom-Wiedergabe wie ein Weichmacher.** Der
  Player-Output lief zusätzlich durch den Positionsglätter mit fester
  Zeitkonstante - bei höherem Tempo wanderte das Ziel schneller durch den
  Clip, als der Glätter folgen konnte, er schnitt Ecken: die Bewegung wurde
  kleiner/runder statt schneller. CatmullRom ist laut eigenem Klassenkommentar
  (`MotionPlayer.h`) schon C1-glatt und braucht diesen zweiten Glätter gar
  nicht (nur Linear). Bei CatmullRom wird das Ziel jetzt direkt übernommen,
  der interne Glätter-Zustand bleibt synchron mitgeführt.
- **Vorbeiflug sprang am Ende auf die alte Position von vor dem Flug.**
  `sourceTargetMetres` hielt die Position von davor unverändert; fiel
  `isRunning()` auf false (Streckenende oder Stop-Knopf), übernahm der
  nächste Tick sofort diesen alten, oft weit entfernten Wert. Jetzt wird die
  tatsächliche Endposition synchronisiert, bevor die Kontrolle zurückfällt.
- **"Engine Reset" half nicht zuverlässig bei Aussetzern nach CPU-Spitzen**
  (meist Mach > 1) - nur ein Wechsel der Audio-Puffergröße brachte den Ton
  verlässlich zurück, weil der einen echten `prepareToPlay()`-Durchlauf
  auslöst. `dopplerEngine.reset()` allein ließ Klangquelle
  (`engineGenerator`/`sampleSource`/`sourceHolder`) und beide Positionsglätter
  unangetastet. `restartEngine()` macht jetzt denselben vollen
  `prepareToPlay()`-Durchlauf mit den aktuellen Werten, umschlossen von
  `suspendProcessing()` (kein Datenrennen mit dem Audiothread) - vom
  Nachrichten-Thread aus (Button "Engine Restart"), nicht aus
  `handlePendingRequests()` heraus, weil `prepareToPlay()` allokieren darf,
  im Audiothread wäre das verboten.

**Neue Regler:**

- **Tempo-Anzeige der Quelle** in der Statuszeile, Einheit umschaltbar
  (km/h, m/s, Mach) - Mach aus derselben Momentangeschwindigkeit wie die
  anderen beiden, nicht aus `M_r` (das ist radial zum jeweiligen Ohr).
- **Amp-Verlauf-Regler** (`distanceCurve`, -1..1, Default 0): verstellt den
  Exponenten k in `A_geo = 1/R^k` statt starr `1/R`. Bei k=1 (Default,
  Reglermitte) wird `R` direkt benutzt statt `std::pow` - der Standardfall
  bleibt bitgleich, bestehende Presets klingen unverändert. Bereich
  unsymmetrisch (0,3 flach .. 2,5 steil): "flacher" ist schon bei kleiner
  Änderung deutlich hörbar, "schärfer" braucht mehr Spielraum.
- **Gemeinsamer Tempo-Deckel "Max Speed"** (m/s, Default 100000 =
  wirkungslos bis bewusst heruntergestellt): letzte Stufe in
  `advanceMotion()`, klemmt die pro Tick zurückgelegte Strecke von Quelle
  UND Hörer - unabhängig davon, ob Maus/Automation-Glättung (alle vier
  Verfahren), Vorbeiflug oder CatmullRom-Wiedergabe das Ziel geliefert hat.
  `Slew Vmax`/`Slew Amax` bleiben als eigene, spezifischere Regler des
  Slew-Limiter-Verfahrens bestehen - das sind zwei verschiedene Größen
  (Tempolimit vs. Antritt), die sich nicht verlustfrei zu einem Regler
  verschmelzen lassen, ohne Ausdruckskraft zu verlieren.
- **Wegvorschau bei Vorbeiflug**: geplante Reststrecke gestrichelt
  eingezeichnet, Punkt kürzesten Abstands zu L markiert und beziffert -
  geschlossene Geometrie (`FlyByGenerator::nearestPoint()`), keine Suche:
  die Bahn ist gerade und liegt per Konstruktion in konstantem Versatz zum
  Hörer. Vorerst nur Draufsicht, Perspektive folgt bei Bedarf. Der
  aktuelle L-M-Abstand steht jetzt unabhängig vom Vorbeiflug immer in der
  Statuszeile.

Gehört/gesehen hat @dpa davon: die vier Bugfixes ja (direkt gemeldet und
gegengetestet), die vier neuen Regler noch nicht - Modellkonstanten
(Amp-Verlauf-Grenzen, Max-Speed-Default) sind Startwerte, kein Urteil.

## Stand 2026-08-17 (Bewegungsaufzeichnung im gespeicherten Zustand)

Bisher lebten Aufzeichnungen ausdrücklich nur zur Laufzeit. @dpa will das
anders: "Recorded muss in state! und State laden muss Record laden! Und wenn
Play beim save aktiv war, soll es beim laden direkt play'en!!" - genau das ist
jetzt so.

- **Format bleibt dasselbe.** Die Frames hängen als `MemoryBlock`-Property am
  Wurzelknoten der `apvts.state`-Kopie. JUCE schreibt solche Properties beim
  Umwandeln in XML als `base64:`-Attribut mit und liest sie ebenso zurück -
  `copyXmlToBinary` bleibt unverändert, und **ältere Presets laden weiter**.
  Gespeichert werden drei ausgeschriebene `double` je Frame, nicht die rohe
  `Vec3`-Struktur: das Dateiformat soll nicht am Speicherlayout eines
  C++-Typs hängen.
- **`motionWasPlaying` wird immer geschrieben**, auch ohne Aufzeichnung. Es ist
  gleichzeitig die Marke, an der das Laden einen Zustand MIT Bewegungsteil von
  einem älteren ohne unterscheidet. Ein altes Preset löscht deshalb keine
  laufende Aufzeichnung, ein neues mit leerer Aufzeichnung schon.
- **Beide Richtungen kreuzen die Threadgrenze**, und beide brauchten dafür
  etwas Eigenes:
  - *Speichern* liest aus dem `MotionRecorder`, der dem Audiothread gehört.
    `MotionRecorder::copyFrames()` ist der lock-freie Leseweg dorthin: der
    Audiothread hängt an und veröffentlicht die Anzahl DANACH (also sieht ein
    Leser nur fertige Frames), und eine `takeGeneration` steigt bei jedem
    vollständigen Inhaltswechsel - nur dabei wird bestehender Speicher
    überschrieben statt angehängt, und genau daran erkennt der Leser, dass
    seine Kopie eine Mischung zweier Aufnahmen wäre. Vier Versuche, dann gibt
    er auf; dieselbe Bauart wie `DopplerEngine::fillSnapshot()`.
  - *Laden* schreibt nicht selbst in `MotionRecorder`/`MotionPlayer`, sondern
    legt die Frames in `stagedMotionFrames` ab und setzt ein Anfrage-Flag, wie
    Aufnahme und Wiedergabe. Das ist nicht nur Formsache: der Host ruft
    `setStateInformation()` typischerweise VOR `prepareToPlay()`, und dessen
    Kapazitäts-Vorwärmung würde einen direkt gesetzten Clip wieder überholen.
- **Echtzeitsicherheit**: `stagedMotionFrames` bekommt im Konstruktor einmal
  die Höchstkapazität und gibt sie nie wieder her - der Audiothread kann dem
  Vektor also nicht beim Kopieren den Boden wegziehen. Beim Übernehmen
  allokiert nichts: `MotionRecorder::setFrames()` und `MotionPlayer::setClip()`
  weisen in vorgewärmte Kapazität zu. Eine Aufzeichnung aus einem Preset mit
  anderer Höchstlänge wird **abgeschnitten** (Deckelung schon im
  Message-Thread) - Allokieren im Audiothread ist die Alternative, die es
  nicht gibt.

`load_check` prüft dreierlei: der Zustand wächst um mindestens die Rohgröße der
Frames (24 Byte je Frame; gemessen 3741 → 10190 Byte bei 200 Frames), dieselben
Frames kommen nach einem Lade-Speicher-Durchgang zeichengleich zurück, und eine
beim Speichern laufende Wiedergabe läuft nach dem Laden von selbst wieder an.
Geladen wird im Test in der Host-Reihenfolge (Zustand setzen, dann vorbereiten).

## Stand 2026-08-17 (perspektivische Ansicht)

Backlog-Punkt "zweite, perspektivische Ansicht" ist gebaut. Die Draufsicht
bleibt unverändert, die neue Ansicht kommt per Umschalter in der Kopfzeile
dazu (`FieldComponent::ViewMode`).

Welt-y zeigt in den Bildschirm hinein, Welt-z nach oben, die optische Achse
liegt waagerecht - deshalb läuft die Bodenebene z = 0 auf eine Horizontlinie
zu, das Fluchtpunkt-Trapez "wie eine Straße in die Ferne".

- **Tiefenlinien exponentiell** (1-2-5 je Dekade). Kein Schönheitsdetail: in
  der Perspektive fallen gleich weit auseinanderliegende Linien ab einer
  gewissen Entfernung auf denselben Pixel. Mit der 1-2-5-Stufung bleibt der
  Abstand zweier Linien *im Bild* ungefähr gleich, und nur dadurch bleibt die
  Ferne lesbar.
- **Kamerageometrie**: hinter und über dem Hörer. Der Abstand nach hinten
  wächst mit der Feldgröße, die Höhe hängt dagegen **fest am Abstand** und
  nicht eigenständig an der Feldgröße. Rein geometrischer Grund: der Fußpunkt
  des Hörers erscheint bei `horizon + focal * camZ / back`, und nur bei
  konstantem `camZ/back` liegt er unabhängig von der Feldgröße an derselben
  Stelle im Bild. Mit zwei unabhängigen Formeln wandert er unten heraus - so
  war es im ersten Wurf.
- **Gezeichnet** werden Horizont, beschriftetes Bodenraster, zum Fluchtpunkt
  zusammenlaufende Längslinien, die Wände als Bodenlinie mit senkrechten
  Rippen (eine flach liegende Wand hat null lange Rippen), die Wellenfronten
  als Schnitt ihrer Kugel mit dem Boden (in der Perspektive eine Ellipse,
  deshalb ein projizierter Linienzug statt `drawEllipse`), die Spur samt
  Bodenschatten, und Quelle und Hörer jeweils mit Lotlinie zum Boden.
- **Schatten und Lotlinien sind der eigentliche Zweck**: ohne sie ist bei
  einer fliegenden Quelle nicht zu unterscheiden, ob sie hoch und nah oder
  tief und fern ist - und z kommt in der Draufsicht überhaupt nicht vor.
- **Ziehen** stellt hier waagerecht die Seitenlage und senkrecht die **Höhe**,
  die Tiefe bleibt (eine Maus hat zwei Achsen, der Raum drei). Damit ist diese
  Ansicht der einzige Weg, die Quellhöhe mit der Maus zu setzen. Hörer
  verschieben und drehen bleibt der Draufsicht vorbehalten, dort ist es
  eindeutig.

Der Rauchtest in `load_check` zeichnet jetzt **beide** Ansichten mit einem
echten Snapshot. Dafür ist die Anzeige-Aktualisierung aus dem 30-Hz-Timer in
ein öffentliches `DopplerfeldEditor::refreshDisplay()` herausgezogen: im Test
läuft keine Nachrichtenschleife, und ohne diesen Aufruf zeichnete die
Feldanzeige einen genullten Snapshot - dann läge in der Projektion jeder Punkt
im Ursprung und geprüft wäre praktisch nichts.

Beurteilt hat @dpa die Ansicht noch nicht; Blickwinkel, Kamerahöhe und die
Farbgebung sind Gestaltungsentscheidungen.

## Stand 2026-08-17 (Klone, "Schrot"-Muster)

Vom Backlog-Punkt "Mehrfach-M / Schrot-Quellen" ist der Klon-Teil gebaut, samt
dem, worum es @dpa dabei vor allem ging: sichtbar machen und steuern, was man
sich einkauft. Der Teil "3 unterschiedliche M mit eigenem Sound" bleibt im
Backlog, Begründung steht dort.

**Echte Klone** sind zusätzliche Pfadpaare mit einer verschobenen
Empfängerabbildung. Sie brauchen weder eigene Trajektorie noch eigenen
Signalpuffer: eine um s verschobene *Quelle* ist dasselbe wie ein um −s
verschobener *Empfänger*, und Empfänger verschieben ist genau das, was
`PathTransform` ohnehin tut. Ein Klon kostet damit exakt ein Pfadpaar -
gemessen kosten zehn echte Klone das 11,8-fache des Direktschalls (166 → 1957
Löser-Auswertungen pro Block), also linear in der Anzahl. Die Versätze werden
deterministisch aus dem Index gebildet (goldener Winkel, Betrag mit der Wurzel
des Index), nicht gewürfelt - derselbe Reglerweg muss zweimal dasselbe ergeben.
Klone laufen nur über den Direktschall; sie zusätzlich über alle Flächen zu
spiegeln stünde in keinem Verhältnis zum Hörgewinn.

**Billige Klone** stehen in `Source/Util/CloneSpray` - und dass die Klasse
nicht in `Source/Physics/` steht, ist die Aussage: das ist keine Physik. Es
sind leicht versetzte, in der Verzögerung langsam wandernde Kopien des fertigen
Signals. Die wandernde Verzögerung erzeugt die Tonhöhenabweichung von selbst
(eine Leseposition, die sich mit dv/dt durch den Puffer schiebt, verstimmt um
genau diesen Faktor), ein eigener Tonhöhenparameter wäre doppelt gemoppelt.
Gemessen kosten zehn billige Klone **null** Löserauswertungen - 166 pro Block,
exakt dieselbe Zahl wie ohne. `load_check` prüft das ohne Toleranz: jede
einzelne zusätzliche Auswertung würde heißen, dass da doch ein Löser läuft.

**Panel "Schwarm / Klone"** trägt Gesamtzahl, "davon echt", Streuung, Pegel der
billigen und eine Automatik - und aus demselben Grund auch den CPU-Balken (bis
150 % mit Marke bei 100 %; ein Balken, der bei 100 anschlägt, verschweigt genau
das Interessante) und darunter, was tatsächlich gerechnet wird. Bei
eingeschalteter Automatik weicht das vom Regler ab, und genau das soll ablesbar
sein statt still zu wirken. Die Automatik ist ausdrücklich nur ein Angebot und
nicht der Standard; sie arbeitet mit Hysterese und getrennten Haltezeiten
(runter schnell, hoch zögerlich), sonst pendelt sie im Takt ihrer eigenen
Wirkung.

**Notaus** liegt jetzt hier statt bei den Wänden, weil er inzwischen mehr
abdeckt als Reflexionen: Boden, Wände, Mehrfachreflexion und Klone auf einen
Schlag. Er wirkt auf zwei Wegen, absichtlich beiden - der Processor schaltet im
Audiothread beim nächsten Block ab (das ist der Knopf für den Fall, dass es
gerade klemmt und der Message-Thread nicht durchkommt), und zusätzlich werden
die Parameter zurückgesetzt, damit die Schalter zeigen, was passiert ist.
Gemessen sinkt die Löserlast nach dem Druck von 1957 auf 166 pro Block.

## Stand 2026-08-17 (N-Wellen-Synthese)

Backlog-Punkt "Druckwellen-/N-Wellen-Synthese" ist gebaut, nach @dpas Vorgabe
als **zusätzliche Schicht** oben auf dem bestehenden, unveränderten
Amplituden-Mechanismus - kein Ersatz der Formel.

- **Ausgelöst** pro Zweig in `PropagationPath`, wenn dessen M_r die 1
  durchquert: der Moment, in dem die Mach-Front genau diesen Hörweg
  überstreicht. Ein frisch geborener Zweig löst nicht aus (kein gültiger
  Vorwert), sonst käme bei jeder Kegelankunft gleich ein Paar.
- **Echte N-Form**, kein Abkling-Impuls: steiler Anstieg auf +A, linearer
  Abfall durch null, steiler Rücksprung von −A auf null.
- Die Stoßfronten haben eine endliche Anstiegszeit, und das ist selbst Physik
  statt Anti-Aliasing-Kosmetik: eine Stoßfront verbreitert sich auf ihrem Weg
  durch die Luft. Deshalb wächst die Anstiegszeit mit der Entfernung - nah ein
  Peitschenschlag, fern ein dumpfes Grollen.
- **Regler "N-Wave Size"** (Größe/Masse in Metern) steuert die Pulsdauer:
  zweimal die Zeit, die der Schall braucht, um den Körper der Länge nach zu
  durchlaufen. Dazu ein **eigener An/Aus-Schalter, Default aus**.
- **Sauber getrennt** von den beiden Nachbarn, mit denen man das verwechseln
  könnte: "Boom Limit" bleibt eine reine Amplitudendeckelung über eps ohne
  jede Pulsform, der Limiter am Ausgang ist der Master-Softclip. Die N-Welle
  hängt an keinem von beiden und hat ihr eigenes 1/R statt des
  regularisierten Fokussierungsfaktors.

`load_check` prüft zweierlei, und das zweite ist das wichtigere: ein
Mach-2-Vorbeiflug muss sich messbar ändern (Spitze 0,108 → 0,142), ein
Unterschall-Vorbeiflug muss **bitgleich** bleiben. Ohne Machfront gibt es
keinen Auslöser, also darf sich kein einziges Sample unterscheiden - das ist
die prüfbare Fassung der Zusicherung "additive Zusatzschicht".

Damit das überhaupt prüfbar ist, bekommen die Zufallsgeneratoren des Motors
feste Startwerte statt der Uhrzeit-Aussaat von `juce::Random`. Ohne das
schwankt der Spitzenpegel zweier identischer Durchläufe um ein paar Promille
und jede Aussage der Form "das ändert den Ausgang nicht" wäre nicht prüfbar.

Ob die Welle richtig **klingt**, hat @dpas Ohr nicht beurteilt. Der
Spitzendruck (Modellkonstante) und die Verbreiterung der Stoßfront mit der
Entfernung sind Modellentscheidungen, keine gemessenen Größen.

## Stand 2026-08-17 (Vorbeiflug-Generatoren)

Backlog-Punkt "zwei neue Bewegungsgeneratoren" ist gebaut:
`Source/Motion/FlyByGenerator` liefert eine geradlinige Bahn in die Tiefe
("durch den Bildschirm") oder waagerecht querend, mit Abstandsparameter in
Metern und live automatisierbarer Geschwindigkeit. Kein Sonderzustand - der
Generator liefert nur Zielpositionen, wie Maus, Hostautomation und
`MotionPlayer`; geglättet und gelöst wird danach für alle gleich.

Zwei Startvarianten, und der Unterschied steckt ausschließlich in der
Vorgeschichte des neuen Geometriesatzes:

- **kontinuierlich** - `SourceTrajectory::fillLinear()` belegt rückwärts
  dieselbe Gerade. Der Löser sieht eine Quelle, die schon immer geflogen ist:
  kein Positions- und kein Geschwindigkeitssprung.
- **Knall-Start** - konstante Vorgeschichte, die Quelle erscheint schlagartig
  in voller Fahrt. Bewusst unphysikalisch und als reproduzierbarer Testfall
  für den offenen Punkt "Überschall-Boom klingt nicht richtig" gedacht.

**Zwei Fallen, die dabei nicht offensichtlich sind:**

1. Die lineare Vorbelegung darf nicht unbegrenzt weit zurückreichen. Der
   Puffer deckt eine endliche Laufzeit ab (bei n_max rund 42 s, also gut
   14 km); bei Überschall wäre die Quelle nach voller Rückrechnung so weit
   weg, dass ihr Schall die Pufferlänge nicht mehr schafft - der Löser fände
   keine Wurzel und das Ergebnis wäre Stille. Die Engine rechnet die zulässige
   Spanne selbst aus, davor ruht die Quelle am Anfang der Geraden.
2. Beide Varianten müssen die Quelle im ersten Moment bereits mit voller
   Geschwindigkeit fliegen lassen. Statt den vier Glättungsverfahren eine
   Anfangsgeschwindigkeit aufzudrücken (wofür sie keine gemeinsame
   Schnittstelle haben), werden sie beim Start mit einem gleichförmig
   wandernden Ziel eingelaufen.

**Ein Ergebnis zum Merken:** der Pegel taugt nicht, um die beiden Varianten zu
unterscheiden. Bei gleichförmiger Annäherung ist die retardierte Entfernung
R_e = R_0/(1 − M_r), und der Fokussierungsfaktor 1/(1 − M_r) hebt sich mit ihr
exakt weg: A = 1/(R_e (1 − M_r)) = 1/R_0. Beide Varianten sind im Anflug
gleich laut (gemessen RMS 0,01746 gegen 0,01754), sie klingen nur verschieden
hoch. `load_check` misst deshalb M_r: 0,58 beim weichen Start, 0,00 beim
Knall-Start im selben Fenster.

## Stand 2026-08-17 (Mehrfachreflexion, eine Generation)

Der Backlog-Punkt war als riskantester gekennzeichnet ("Stabilitätsthema").
Er ist es nicht - aber aus einem Grund, der erst beim Hinsehen klar wird:

**Warum das nicht aufschwingen kann.** Die Sorge galt einem
Verzögerungsnetz mit Rückführung. Hier liegt die Spiegelquellen-Methode vor:
jeder Weg ist ein eigener `PropagationPath`, der den geteilten
Quellsignalpuffer **liest** und additiv auf den Ausgang schreibt. Kein Pfad
schreibt je in den Puffer zurück, kein Ausgang ist irgendwo Eingang - es gibt
keine Schleife, in der sich eine Verstärkung aufsammeln könnte. Der Ausgang
ist eine endliche Summe endlich vieler beschränkter Terme. Deshalb keine
Rekursion und keine Abbruchbedingung, sondern eine feste, abzählbare Liste.

- **Genau eine zusätzliche Generation**: Wege Quelle → Fläche X → Fläche Y →
  Ohr mit X ≠ Y. Zweimal dieselbe unendliche Ebene gibt es nicht (die
  Verkettung wäre die Identität und damit der Direktschall). Bei drei Flächen
  sechs Kombinationen, also zwölf weitere Pfade - Default aus, und der
  Notaus-Knopf schaltet sie mit ab.
- **`PathTransform` speichert jetzt eine allgemeine affine Abbildung.** Die
  Verkettung zweier Spiegelungen ist keine Spiegelung mehr, sondern eine
  Drehung (schneidende Ebenen) bzw. Verschiebung (parallele Ebenen). Beides
  ist wieder eine Isometrie und damit zulässig, nur nicht mehr als Normale
  darstellbar.
- **`bounceGain`** (Default 0,6) ist der Pegelfaktor je Generation, hart unter
  1. Nötig, weil die Flächendämpfung ein Tiefpass mit
  Gleichstromverstärkung 1 ist: sie nimmt Höhen, keinen Pegel. Ohne ihn wäre
  eine zweifach reflektierte Welle nur durch den längeren Weg leiser - und der
  kann bei zwei nah beieinander stehenden Wänden fast null sein.
- **Nachweis im Test statt Behauptung**: `load_check` fährt den ungünstigsten
  Fall (zwei parallele, nahezu schallharte Wände plus Boden, Dämpfung null,
  Bounce Gain am Anschlag, Limiter aus, Quelle steht still) und misst den
  Pegel eine Sekunde nach dem Einschwingen und zehn Sekunden später:
  RMS 2,41118 → 2,41384, Faktor 1,001.

Der Preis ist Rechenzeit, nicht Stabilität: alles an heißt 20 Pfade statt 2.
Im Ruhezustand sind das 4640 statt 187 Löser-Auswertungen pro Block.

## Stand 2026-08-17 (Wände als frei platzierbare Ebenen)

Backlog-Punkt "frei platzierbare Wände" ist gebaut, `solver_check` und
`load_check` sind grün, der Bau bleibt warnungsfrei.

- **`PathTransform` spiegelt jetzt an einer beliebigen Ebene** statt nur
  achsenparallel: die Ebene steht als `{ x : normal·x = planeOffset }`,
  gespiegelt wird nach Householder. Normale der Länge 0 heißt "keine
  Spiegelung" und ist der Direktschall; der Boden ist der Sonderfall
  `normal = ẑ` und rechnet bitgleich wie vorher.
- **Zwei feste Wandplätze**, keine dynamische Liste - die Pfade müssen im
  Audiothread allokationsfrei bereitliegen. Je Wand: Schalter, Fußpunkt X/Y
  (normiert wie alle Feldpositionen), Winkel der Wandlinie in der Draufsicht,
  Neigung um genau diese Linie und Dämpfung. Bei ±90° Neigung fällt die Wand
  mit der Bodenebene zusammen - das ist die Kontrolle, an der die Herleitung
  der Normalen hängt, und `solver_check` prüft genau das.
- **Kosten**: eine Wand kostet exakt so viel wie der Direktpfad, zwei Wände
  zusammen das Dreifache (174 → 538 Löser-Auswertungen pro Block im
  Vorbeiflug-Szenario). Deshalb Default aus, wie beim Boden.
- **Notaus "Alle Reflexionen aus"** im selben Panel: zurück auf nur noch den
  Direktpfad pro Ohr. Läuft über die Parameter, nicht über einen Direktgriff
  in die Engine - so sieht man am Schalter, was passiert ist, der Host bekommt
  es mit, und es steht im gespeicherten Zustand. (@dpa hat heute Nacht live
  danach gefragt, als der Ton bei einer CPU-Spitze wegblieb und kein Weg
  zurück da war außer Neustart.)
- **Im Feld** werden eingeschaltete Wände als Linie über den Bildrand hinaus
  gezeichnet; begrenzt gezeichnet würden sie eine Kante suggerieren, die es
  nicht gibt. Die Neigung steckt in der Strichstärke.

Die Reglerwerte schreiben wie überall nur Ziele; gefolgt wird ihnen über
denselben One-Pole wie beim Yaw, weil eine springende Spiegelebene eine
springende Laufzeit und damit einen Klick bedeutet.

Gehört hat @dpa das noch nicht - insbesondere die 2500 Hz Eckfrequenz der
Wanddämpfung (härter als der Boden mit 800 Hz) ist eine Modellentscheidung,
die sein Ohr bestätigen muss.

## Stand 2026-08-17 (Löser-Last bei Bodenreflexion + hoher Geschwindigkeit)

@dpa hat live Bodenreflexion mit schneller Bewegung nahe Mach 1 kombiniert -
der Ton setzte aus wie vor dem Löser-Fix. Kein Testszenario hatte diese
Kombination gemessen: das Bodenszenario flog mit 30 m/s, das schnelle
Szenario hatte keine Spiegelpfade.

**Was die Messung ergibt** (neues `load_check`-Szenario "Mach1, Boden an"):

- Der Spiegelpfad hat **keinen eigenen Defekt**. Er kostet exakt so viel wie
  der Direktpfad, Faktor 2,00 in Löser-Auswertungen. Kein zu großes
  Suchfenster, keine verlorenen Zweigidentitäten, kein Neusäen.
- Es entgleist auch nichts: kein NaN, und - neu geprüft - **keine Stille im
  Signal**. Der Ausfall ist damit reine CPU-Überlast und kein Rechenfehler.
  Die bisherigen Kriterien konnten das gar nicht sehen: NaN-Zähler und
  Gesamtspitze bleiben sauber, wenn das Plugin mittendrin sekundenlang
  schweigt und danach weitermacht. `load_check` misst deshalb jetzt die
  längste zusammenhängende Stille.

**Neues Lastmaß.** Wanduhrzahlen schwanken auf einem beschäftigten Rechner um
Faktor zwei - ein Kriterium darauf wäre ein Würfelspiel, und Regressionen
verschwinden im Rauschen. Gezählt wird jetzt jede Auswertung des Residuums F
(`RetardedTimeSolver::residualEvaluations`, durchgereicht bis zum Processor).
Das ist die teuerste und häufigste Einzeloperation des Lösers und auf
derselben Codebasis exakt reproduzierbar.

**Drei Änderungen am Löser, keine am Klangmodell:**

1. **Trajektorien-Abfragen ohne Eviktion.** `maxSpeedSince`/`maxDistanceSince`
   suchen ihren Deque-Eintrag binär, statt den Vorderrand wegzuwerfen. Erst
   dadurch darf jeder Empfangspunkt sein eigenes Fenster fragen - vorher hätte
   der erste Frager dem zweiten die Einträge weggeworfen, weshalb der Löser
   gezwungen war, über die volle Historie zu fragen.
2. **Schranken aufs Suchfenster bezogen.** Lipschitz-Konstante und
   Überschall-Entscheidung gelten jetzt für `[windowStart, t_h]` statt für die
   ganze Historie. Das war die zweite Hälfte des Hangover-Problems: der
   Stride-Fix rief den Löser nach einem Überschallmoment wieder seltener, aber
   jeder einzelne Aufruf lief bis zu 40 s weiter im teuren Vollscan-Modus.
   `recentMaxSpeed()` fällt dabei weg - es scannte pro Pfad und Teilblock 2000
   Ringeinträge und räumte den Cache leer, den der Löser gleich danach braucht.
3. **Entdecken vom Nachführen getrennt.** Bekannte Zweige werden weiter an
   jedem Solver-Punkt nachgeführt; der Vollscan, der NEUE Zweige findet, läuft
   höchstens alle 0,5 ms (`setDiscoveryIntervalSeconds`). Im Überschall lief er
   bisher alle 167 µs, obwohl in dieser Zeit fast nie ein Zweig hinzukommt.
   Bleibt kein Zweig übrig, wird immer voll gescannt - sonst wäre die
   Alternative Stille.

Löser-Auswertungen pro Block:

| Szenario                | vorher | jetzt |
|-------------------------|--------|-------|
| Normalfall n=200        |    187 |   187 |
| Boden an, srcZ=20m      |    362 |   362 |
| Realistisch nahe Mach1  |   9263 |  3934 |
| **Mach1 + Boden an**    |  17050 |  6670 |
| Extrem, Mach ~3         |  19522 |  7857 |
| Extrem, Umkehr          |  26704 | 11318 |

Im Zielfall "Mach 1 mit Bodenreflexion" sind das gemessen 9,7 % vom
Echtzeitbudget im Mittel und 34 % im schlechtesten Einzelblock, vorher 20 %
und 78 %. Der Unterschall-Betrieb ändert sich nachweislich nicht (187 und 362
unverändert) - dort liegt der Solver-Punktabstand ohnehin über dem
Entdeckungsintervall.

Die Entdeckungslatenz von 0,5 ms ist eine Modellkonstante und liegt weit unter
der 3-ms-Toleranz, die `solver_check` seit jeher für die Kegelankunft ansetzt;
gemessen sind 0,33 ms Verspätung. Ein eigener `solver_check`-Testfall fährt
dieselbe Geometrie mit und ohne Drosselung und prüft, dass die Ankunft in der
Toleranz bleibt, nie früher erkannt wird und die Ersparnis wirklich anfällt.

Gehört hat @dpa auch das noch nicht.

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

1. **Bis zu 3 unterschiedliche M mit eigenem Sound.** Die Klone aus demselben
   Wunsch sind gebaut (siehe Stand-Abschnitt), dieser Teil nicht. Ein Klon ist
   billig, weil er dieselbe Trajektorie und denselben Signalpuffer liest und
   nur eine verschobene Empfängerabbildung bekommt. Eine wirklich EIGENE
   Quelle mit eigenem Klang teilt beides nicht: sie braucht ihren eigenen
   `SourceSignalBuffer` (bei n_max zweistellige MB), ihre eigene Trajektorie
   samt Glätterkette und ihren eigenen Satz Motor-/Sample-Parameter - also
   drei Motor-Panels statt einem. Das ist kein Physikproblem, sondern ein
   Zustands- und UI-Umbau, und in einen Lauf mit fünf anderen Punkten passt er
   nicht ohne Pfusch. Der billige Weg, es doch zu tun (dieselbe Quelle mit
   großem Versatz), wäre kein zweites M, sondern ein weit gestreuter Klon -
   und den gibt es schon.

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

## TODO: Bahn-Historie wird unter laufenden Zweigen umgeschrieben

Gemeinsame Wurzel von zwei Beobachtungen (@dpa 20260820/21):

- "hier gibt's eine unvermittelte n-Wave" bei einem Unterschallflug, und zwar
  "nicht beim ersten Durchlauf, sondern erst beim 2. und 3." Das Preset hat
  `playLoop = 1` und einen aufgezeichneten Clip: beim Loop-Wrap setzt
  `MotionPlayer` den Abspielkopf per `fmod` zurueck. Ist der Clip nicht
  zufaellig geschlossen, springt die Quelle dabei ueber eine Strecke, die sie
  in einem Tick nie zuruecklegen koennte - fuer den Loeser unendliche
  Geschwindigkeit, also Ueberschall, also ein neu entstehendes Zweigpaar.
- "test_vorbeiflug 909kmh knackst am anfang manchmal". Beim Flugstart schreibt
  `startLinearMotion()` -> `fillLinear()` die gesamte Vorgeschichte neu.
  Zweige, die gerade noch Schall von vor einer Sekunde wiedergeben, finden
  schlagartig eine andere Vergangenheit vor. "Manchmal" heisst: nur wenn
  gerade noch etwas unterwegs ist.

Zu beheben ist das am Sprung, nicht an der Ausloesebedingung der N-Welle: ein
Positionssprung IST formal Ueberschall, der Ueberschall-Check in
`PropagationPath` kann ihn also nicht abfangen.

Vorschlag: Die Engine hat bereits einen Geometrie-Crossfader (60 ms,
`DualPathCrossfader`), der bei Aenderungen der Wegewelt den alten Satz
ausblendet, waehrend der neue einsetzt. `startLinearMotion()` und
`jumpSourceTo()` benutzen ihn nicht, sondern ueberschreiben die aktive
Trajektorie direkt. Laufen sie ueber denselben Crossfade, verschwinden Knacks
und Scheinknall gemeinsam.

## TODO: One-Pole glaettet den Echtzeitverlauf der Maus nicht sauber

@dpa 20260821: "mit Smoother One-Pole wird der Echtzeitverlauf der maus noch
nicht korrekt geglaettet. aber nur dieser Smoother und nur in Echtzeit oder
waehrend der Aufnahme. Beim Abspielen ist es dann korrekt."

Ursache steht fest: `sourceTargetMetres` wird in `applyParameters()` gesetzt,
also einmal je Block, waehrend `advanceMotion()` mit 1000 Hz tickt. Das Ziel
steht rund elf Ticks still und springt dann. Die Glaetter zweiter Ordnung
(Feder, 1-Euro) und der Slew-Limiter fuehren eine eigene Geschwindigkeit und
stecken das weg. Der Ein-Pol nicht: seine Geschwindigkeit ist die
Positionsaenderung dieses Ticks geteilt durch dt (so auch im Klassenkommentar
von `OnePoleSmoother` vermerkt), sie springt also mit dem Ziel mit - und genau
diese Geschwindigkeit bestimmt den Doppler. Beim Abspielen umgeht die
Catmull-Rom-Wiedergabe den Glaetter, deshalb ist es dort korrekt.

Der naheliegende Fix (Ziel ueber den Block wandern lassen, wie beim
Pfad-Versatz in `PropagationPath`) wurde gebaut und wieder VERWORFEN: er macht
aus jedem SPRUNG des Ziels eine Strecke, die in einem einzigen Block
zurueckzulegen ist. Gemessen stieg M_r im Test "schnell subsonisch" von 0,74
auf 2,46, nach Ausnahme fuer `holdSourceTargetAt()` immer noch auf 1,26 - die
Rampe erzeugte also Scheinueberschall. Ein tragfaehiger Fix muss alle Stellen
erfassen, an denen das Ziel springen DARF (Flugende, Preset, Feldwechsel,
Automationsspruenge), und nur die echte Reglerbewegung rampen. Alternativ am
Ein-Pol selbst ansetzen und ihm einen eigenen Geschwindigkeitszustand geben.

## TODO: Kaustik-Lastspitze (bekannt, bewusst offen)

Beim Vorbeiflug nahe Mach 1 kostet der teuerste Block das rund 12-fache des
Blockschnitts (143994 gegen 12354 Löser-Auswertungen, gemessen bei 358 m/s und
|M_r| = 0,99). An dieser Stelle setzt der Ton aus. Der Test in
`Tests/load_check.cpp` misst und meldet das weiterhin, lässt den Lauf aber
nicht mehr fehlschlagen (@dpa 20260820), damit gepusht werden kann.

Was bereits ausgeschlossen ist: die *Länge* eines einzelnen Suchfensters. Ein
testweise auf den Beginn der geradlinigen Phase angehobenes Fenster halbierte
den Schnitt (12354 -> 7422), liess den teuersten Block aber bei exakt denselben
143994 Auswertungen. Die Spitze entsteht also durch mehrere Vollscans innerhalb
*eines* Blocks, nicht durch einen einzelnen langen Scan. Dort ist
weiterzusuchen. (Die Fenster-Verkleinerung selbst wäre als CPU-Gewinn im Mittel
trotzdem zu haben, sie ist nur kein Aussetzer-Fix.)

Chat-Verlauf mit der vollständigen Entstehungsgeschichte (inkl. aller
Design-Entscheidungen aus dem Grill-Interview) liegt in der Claude-Code-
Session vom 2026-08-15/16, falls tiefere Begründungen für eine Entscheidung
gebraucht werden, die weder hier noch im Code-Kommentar stehen.
