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

## Stand 2026-08-25 (Wackler auf zwei Regler, Rakete tiefer, Fades weg)

Fünf Punkte aus @dpas Durchgang. Alle 44 `load_check`-Szenarien grün; jede
Abweichung gegenüber dem Stand davor steht unten benannt.

### Der Wackler hat nur noch zwei Regler

Ursache seiner Klage ("ist das mit der Hektik zu kompliziert das passende
Fenster zu finden"): Ausschlag, Hektik und "Jit Max" hingen multiplikativ
zusammen, `v_peak = A · 2π · f · 2√3`, gedeckelt. Wer einen der drei drehte,
verschob die Wirkung der anderen beiden.

Jetzt beschreiben **zwei** Größen die Bewegung vollständig:

- `srcJitterAmount` (m) - **wie weit**.
- `srcJitterSpeed` (m/s) - **wie schnell**, als SPITZE der Bahngeschwindigkeit.

Die Frequenz ergibt sich, `f = v / (2π·A·2·√(2+z²))`, und steht nirgends mehr
als Regler. `srcJitterRateHz` und `srcJitterMaxSpeed` sind ersatzlos entfallen;
ihre IDs leben in `Params.h` nur noch als `*Legacy` weiter, damit
`setStateInformation()` alte Zustände umrechnen kann (`v = A·2π·f·2√3`,
gedeckelt auf den damaligen "Jit Max") und die abgelösten Einträge danach
entfernt.

Warum die Spitze und nicht der Mittelwert: nur so ist der Wert mit der
Schallgeschwindigkeit vergleichbar, und genau darum geht es - ein Wackler über
Mach 1 löst fortwährend Stoßfronten aus. Auf den Mittelwert bezogen läge die
Spitze beim Doppelten, 340 m/s im Regler wären in Wahrheit Mach 2. Zwischen
den Spitzen bleibt die Bewegung ungleichmäßig: gewürfelt werden jetzt die
VERHÄLTNISSE der drei Achsen (`pickAxisFactors()`), nicht mehr ihre Frequenzen.

Reglerbereich bis 100000 m/s wie der alte "Jit Max" - 50 m Ausschlag bei 3 Hz
sind rechnerisch schon 3260 m/s. Default 20 m/s, das entspricht bei ein paar
Metern Ausschlag den vorherigen 0,2 Hz.

Rückkehr, falls es nicht gefällt: `git revert 3eddc97` bzw.
`git checkout vor-wackler-umbau -- Source Tests`.

### Rakete: tiefer, abstandsabhängig, Poisson-Stöße

- **Bis 5 Hz.** Filteruntergrenze in `applyVoicing()` von 20 auf 4 Hz; der
  Reglerweg nach unten ist jetzt je Betriebsart verschieden (`jetDarkOctaves`
  1,1 - unverändert; `rocketDarkOctaves` 3,2). `rocketVoiceTable` liegt rund
  eine Oktave tiefer, "Ferne" bei `lowFc` 20 Hz. Der Druck kommt aus dem
  Bandbreiten-Ausgleich in `place()`, nicht aus einem höheren Gesamtpegel.
- **Abstandsnaht** `EngineGenerator::setRocketDistance()`, gefüttert vom
  Processor wie `setRotorInPlane`. Daran hängen zwei Dinge, die die Ausbreitung
  nicht leisten kann, weil sie erst nach dem Generator passiert:
  - Neuer Regler "Fern-Farbe" (`rocketFarColour`): die Entfernung schiebt die
    drei Bänder nach unten, ein Oktav je Verdopplung bei Default 1.
  - Die Tiefe der Absenkung durch die Druckstöße.
- **"Rauschen bei jedem Abstand gleichlaut" ist nicht der Pegel.** Neuer
  `load_check`-Abschnitt "Raketen-Abstand" misst −19,9 und −20,0 dB je Faktor
  zehn; 1/R greift exakt. Was fehlte, war die Klangfarbe.
- **"Kein Unterschied zwischen den Druckreichweiten": der Regler kam nicht an.**
  `shockDuckRange` hängt an `triggerNWave()` in `PropagationPath`, und das
  feuert nur bei einer Kegelankunft. Die Stöße der Rakete entstehen im
  Generator. Jetzt senken sie das Brüllen dort selbst ab, mit derselben Formel
  `range/(range+R)` und proportional zur Stärke des einzelnen Stoßes
  (`setRocketShockDuck()`).
- **Stoßfolge als Poisson-Prozess.** Exponentiell verteilte Abstände statt
  gejittertem Raster. Das Knattern entsteht nicht an den stehenden Mach-Zellen,
  sondern an einzelnen steilen Fronten aus der turbulenten Scherschicht -
  unabhängige Ereignisse mit mittlerer Rate. Ihr Kennzeichen ist, dass sie sich
  ballen; ein Raster mit ±50 % Jitter kann das nicht. Zellgröße multiplikativ
  gestreut (Faktor 0,37 bis 2,7), Amplitude exponentiell verteilt statt
  gleichverteilt - das Kennmaß des Knatterns ist die Schiefe des Drucksignals.
- **Regelbereiche.** Stoßlänge 0,02..60 m -> 10..600 m, Default 20 m
  ("min. 10m sonst klingt es irgendwie unecht"). Druckstoß-Regler 0..1 -> 0..4,
  Default 1 ("leiser machts keinen sinn"). Stoß-Vorrat 32 -> 160, denn zehn
  Meter Zelle sind schon 58 ms Wellendauer.

### OSC-Sammelschalter

Neuer Parameter `oscOn` (Default an), Schalter auf Höhe der Level-Zeile rechts
neben der Teilton-Matrix. Wirkt auf "Frei" und den Verbrennermotor des
Hubschraubers - genau dort werden die vier Teiltöne gerechnet; Rauschband,
Fahrtwind, Unwucht und Rotor laufen weiter. Ein Mute, kein Pegel: die vier
Level-Regler behalten ihre Werte. Geblendet über denselben Ein-Pol wie die
Wellenform, Phasen laufen weiter.

### Fade Auto und Fade Manual entfallen

Die Fadedauer kommt nur noch aus dem Anlass. `computeFadeSamples()` behält
seine Formeln je `FadeReason`, der manuelle Zweig fällt raus - damit auch
`FadeContext::useManual`/`manualSeconds`, `FadeReason::Manual` (von keinem
Aufrufer je gesetzt) und beide `setManualFade()`. Alte Presets laden weiter,
die APVTS ignoriert unbekannte Einträge.

### 3D-Ansicht

`perspectiveZoom` 0,3..4 -> 0,04..16, `perspectiveHorizonFraction`
0,15..0,70 -> 0,04..0,94. An beiden Enden bleiben sechs Prozent der Bildhöhe
stehen, der Boden verschwindet also nie ganz. Bodenraster seitlich bis 5 km
statt bis 500 m.

### Gegenprobe

Alle 44 Szenarien grün. Die Abweichungen gegenüber dem Stand davor:

- Raketen-Vorlagen-Schwerpunkte 1182/3264/3506/395/3920 -> 814/2629/2871/191/
  3499 Hz, dunkelster Punkt 876 -> 447 Hz (die tiefere Tabelle).
- Flankensprung der Stöße 5,0 -> 19,5 x, Spitze 4,96 -> 16,0 (die neue
  Amplituden-Schiefe).
- Last Physik 4,8 -> 5,8 % des Budgets (Stoß-Vorrat 160 statt 32).
- Reglerruck 0 -> 200 m bei Tempo 100 m/s: |M_r| 0,40 -> 0,45, weit unter 1.
- Jitter-Wolke Streuung 1,009 -> 1,007. Klon-Jitter-Spitze 7,0 -> 34,1 % ist
  Phasenlage und kein Verhalten: das RMS derselben Messung ändert sich nur um
  1,5 %.
- Beschriftungszahlen 3579 -> 3552 und 864 -> 855 Regler: drei Regler weg
  (Fade Manual, Hektik, Jit Max), zwei dazu (Fern-Farbe, OSC).
- "Motor neu anlassen" 3,2 -> 5,4 ms: eine Wanduhrmessung, die auf dem Stand
  DAVOR zwischen 3,1 und 8,0 ms streute. Kein Regress.

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

## Stand 2026-08-24 (Wackler-Deckel, Pfeiltasten)

### Der Wackler hatte keinen eigenen Tempo-Deckel

Drei Beobachtungen von @dpa mit EINER Ursache: "jitter tut nichts",
"randomize tut nichts", "jitter verursacht ständig Überschall N-waves".

`PositionJitter` hing an `globalMaxSpeed`, dem Deckel der **Bahn**. Wer die
Bahn langsam wollte, hat damit unbemerkt den Wackler gebremst - in @dpas State
auf 1,9 %, also ein Umlauf in 16 Sekunden bei 69 m Radius. Das sieht von außen
aus wie "tut nichts", ist aber kein Zustandsfehler und darum auch durch keinen
Reset zu beheben. Seine eigene Beobachtung passt exakt: bei kleinem Ausschlag
greift die Bremse nicht, deshalb "funktionierte" es nach dem Hochdrehen von 0.

`srcJitterMaxSpeed` ist jetzt ein eigener Regler, Default 340 m/s (knapp unter
Schallgeschwindigkeit), 0 schaltet ihn ab.

**Zweiter Fehler in derselben Funktion:** der Bremsfaktor wurde aus der gerade
gewürfelten Frequenz gerechnet und war damit ein *Normierer* - er zog jede
Schwankung exakt auf die Grenze zurück. Sobald die Grenze überhaupt griff,
hatte `randomize` deshalb buchstäblich keine Wirkung mehr. Der Faktor kommt
jetzt aus den **Reglerwerten** (Bezug: das schnellstmögliche, das der Würfel
bei dieser Einstellung hergibt). Er ist damit über die Zeit konstant, die
relative Schwankung bleibt erhalten, die Grenze wird nie überschritten.

Merksatz für Folge-Sessions: eine Geschwindigkeitsbremse, die aus der
Momentangeschwindigkeit gerechnet wird, ist immer ein Normierer und macht jede
Modulation dieser Geschwindigkeit platt.

### Pfeiltasten am Regler

JUCE nimmt für die Tastatur das Parameter-Intervall, sonst ein Hundertstel des
Wertebereichs - bei Max Speed (bis 100000 m/s) also 1000 m/s je Druck, bei
einem 0..1-Regler dagegen viel zu fein. Das Maß war falsch, nicht der Wert.
`RoundedSlider::keyPressed()` bewegt jetzt ein halbes Prozent des **Reglerwegs**
(mit Umschalt ein Zehntel davon), weil der Weg Bereich und Kennlinie schon
enthält. Gilt damit für alle Regler auf einmal.

### Offen aus @dpas Durchgang vom 24.08. 15:52

Vier der fünf Punkte sind gebaut und gemessen (siehe "Stand 2026-08-24
(Sprungnaht, Sprungknall, Knall-Trigger)" weiter unten) - **gehört hat @dpa
sie noch nicht.** Offen bleibt:

- **Klon jittert nicht bei Klone = 1** (@dpa mit Fragezeichen). Wahrscheinlich
  dieselbe Deckel-Ursache wie oben und mit dem eigenen Wackler-Deckel erledigt
  - nachzuhören, bevor daran gesucht wird.

Zum Nachhören aus dem Umbau vom 24.08.:

- Preset-Wechsel: kommt der neue Ort sofort und ohne Anflug? Der Schnitt ist
  12 ms lang, danach klingt der neue Ort ohne Wartezeit.
- Der automatische Engine-Restart nach einem State-Load ist entfallen (dafür
  der Schnitt). Der Knopf "Audiomotor neu anlassen" bleibt von Hand da. Falls
  der Restart doch gebraucht wird, ist es eine Zeile in
  `setStateInformation()`.
- Rundenwechsel einer Bewegungswiedergabe: leise, Umbau, laut.
- Startvariante "Knall-Start": Sprungknall steht jetzt auf 0,5 statt 0 und
  sitzt in der Vorbeiflug-Gruppe. Ob 0,5 die richtige Lautstärke ist,
  entscheidet das Ohr.
- Scope-Knopf "Knall" plus Haltezeit.

Entschieden und **nicht** zu bauen: die Raketen-Druckstöße lösen KEINE
zusätzlichen N-Wellen in der Ausbreitung aus (@dpa: "Das wäre ja dann doppelt?
Nee. So wie es jetzt ist, ist es gut.").

## Stand 2026-08-24 nachts (Entfernung, Z-Anteil, Quant, Klappen)

Vier Punkte aus @dpas Durchgang 23:11. Gemessen, **gehört hat @dpa es noch
nicht.**

- **Abstand L..M ist immer sichtbar**, links oben, in derselben Machart wie
  die Tempo-Anzeige rechts: gleiche Farbe, gleiche Schrift, pixelfest in
  Monospace, eine Spalte in Metern unter der Beschriftung "Entfernung" /
  "Distance". In beiden Ansichten (`FieldComponent::drawDistanceReadout`).
- **Der Wackler hat wieder einen Z-Regler**, diesmal als ANTEIL: 1 = die Höhe
  wackelt genauso weit wie x und y, 0 = gar nicht. So bleibt "Jitter" der eine
  Regler für die Größe der Bewegung, und dieser sagt nur, ob sie flach liegt
  oder den Raum füllt. Eigener Parametername `srcJitterZAmount`: der alte
  `srcJitterZ` gehörte zur Rotoren-Betriebsart und meinte die Neigung der
  Kreisebene - ein Preset von damals würde mit seinem Wert (meist 0) die Höhe
  stilllegen, ohne dass jemand daran gedreht hätte. Default 1.
- **Schalter "Quant"** beim Hubschrauber: rastet die Rotordrehzahl auf das
  nächste ganzzahlige Verhältnis zur Motorgrundfrequenz (RPM/60). Das ist die
  Antwort auf @dpas Beobachtung, die Unwucht klinge wirksam und der Rotor
  nicht - die Unwucht sitzt seit jeher auf `f_base/2 * 2^Oktave`, also fest am
  Motor, der Rotor lief frei dagegen. **Default aus:** die Schwebung zwischen
  beiden ist ausdrücklich erwünscht ("die zwei leicht unterschiedlichen Tempi
  ... führen zu interessanten härte und weichheits verläufen").
- **Die Klappen-Überschriften** folgen jetzt auch dem Sprachumschalter. Der
  Beschriftungstest hatte sie übersehen, weil sie einen Pfeil vor dem Titel
  tragen und der exakte Vergleich daran scheiterte; er schneidet führende
  Zierzeichen jetzt ab.

Gemessen (`load_check`):

| | Wert |
|---|---|
| Z-Anteil, Ausschlag Ebene/Höhe voll | 50,0 / 50,0 |
| halb | 50,0 / 25,0 |
| aus | 50,0 / 0,0 |
| Quant aus, Blattschläge/s (RPM 434, Rotor 3,0 Hz) | 11,99 (Soll 12,00) |
| Quant an | 14,44 (Soll 14,47 = zweimal die Grundfrequenz) |

@dpas Preset `presets/Hubschrauber2` (Imb Octave 2, RPM 434, Rotor Hz 3,63,
Blade Len 22,6) bleibt unberührt - beide neuen Defaults sind der bisherige
Zustand. Er beschreibt es als den Stand, dem er gerne akustisch folgt; bei
Änderungen am Rotor ist das der Bezugspunkt, gegen den zu hören ist.

## Stand 2026-08-24 spät (Schnitt beim Vorbeiflug, Rotor-Fahrt, Sync, Sprachen)

Vier Punkte aus @dpas Durchgang 19:22. Alles gemessen, **gehört hat @dpa es
noch nicht.**

### Die Sprungkante ist weg, der Vorbeiflug wird geschnitten

@dpa: "Sprungkante bitte immer ohne (Sprung-)Bewegung im Audio, demnach immer
ohne N-Wave und ohne Doppler (durch den Sprung), gefadet, default für
Vorbeiflug! ohne On/Off toggle (weil das andere ist völlig sinnlos: von ende
auf anfang springen??)".

Der Schalter tat das Gegenteil des Gewünschten: er ließ die Kante am
Segmentanfang stehen, statt sie zu interpolieren - also einen Ruck im Audio.
Parameter und Sonderbehandlung im Löserpfad sind entfallen, Amplitude und
Leseposition werden immer interpoliert.

Der Positionssprung selbst - an den Startpunkt der Strecke und beim
Rundenwechsel zurück an den Anfang - läuft jetzt über den **Schnitt**.
`startFlyBy()` wird dafür aus dem stillen Fenster heraus gerufen
(`cutStartsFlyBy`) und setzt Glätter, Bahn-Vorgeschichte und Geometrie dort,
wo niemand zuhört. `DopplerEngine::cutTo()` nimmt dafür jetzt eine
Anfangsgeschwindigkeit mit; der kontinuierliche Start braucht sie, damit die
Quelle vom ersten Bahnpunkt an mit voller Fahrt fliegt.

Der Geometrie-Crossfade war hier zweierlei zu viel: er ließ den alten Satz
weiterfliegen, also den Sprung als Bewegung hören, und rechnete dafür zwei
komplette Lösersätze.

Gemessen (`load_check`, "Sprungnaht", Lauf "Vorbeiflug-Runde", zwei Runden
über eine 5,3-s-Strecke bei 150 m/s): teuerster Block 227, \|M_r\| max 0,42,
längste Stille 0,012 s - also genau das Flugtempo und die Schnittdauer.

Der Sprungknall bleibt: der Sprung ist lautlos, die Druckwelle darauf ist der
gewollte Effekt.

### Rotor: die Fahrt kommt an der Blattspitze an

@dpa: "das knattern kommt nicht vom Doppler, auch nicht bei höheren speeds ...
hören tut man bei einem Hubschrauberüberflug wirklich 'ganz schöne Spitzen'
die an Überschall erinnern."

Drei Lücken, alle drei erst durch Messung sichtbar geworden:

1. **Die Fahrt fehlte ganz.** Gerechnet wurde nur der Umlauf der Blattspitze
   (6 m, 5 Umdrehungen/s = Mach 0,55). In Wahrheit fliegt der ganze Rotor mit,
   und auf der vorlaufenden Seite addieren sich beide: mit 130 m/s sind es
   Mach 0,93.
2. **Über Mach 0,88 lösen sich die Verdichtungsstöße von der Blattspitze** und
   laufen als eigene Wellen davon. Das ist der Knall, der an Überschall
   erinnert, obwohl nichts Überschall fliegt. Er ist jetzt da: eine kurze
   N-Form (1,5 ms) je Blattschlag, deren Stärke mit dem Abstand zur Grenze
   wächst. Sie läuft am Bandpass des Schwirrens **vorbei** - dadurch bleibt sie
   eine Kante statt ein Blubbern.
3. **Die Normierung nahm den Effekt wieder weg.** Sie lief über die volle
   Verstärkung samt Fahrt und war damit ein Nullsummenspiel. Normiert wird
   jetzt nur die Drehzahl.

Dazu trifft die Richtwirkung den **Schlag**, nicht das Schwirren: das Schwirren
entsteht über die ganze Blattspanne, wo die örtliche Machzahl von null an der
Nabe bis zum Vollen an der Spitze reicht, der Schlag sitzt an der Spitze. Das
Schwirren bleibt dadurch ein gleichmäßiger Teppich, aus dem der Schlag
heraussticht.

"Knattern" geht bis 4 statt bis 1 und zieht über 1 hinaus auch die
Richtwirkung hoch (gedeckelt bei 36 dB). Zu @dpas Frage, ob der Regler den
Dopplerwert erhöht: bisher nicht, jetzt ja.

Gemessen (Modulationstiefe der Hüllkurve):

| | Wert |
|---|---|
| Knattern 1, Schwebeflug | 0,430 |
| Knattern 1, 70 m/s | 0,826 |
| Knattern 1, 130 m/s | 1,250 |
| Schwebeflug, Knattern 0,25 | 0,170 |
| Schwebeflug, Knattern 4 | 0,430 |

**Offen** aus demselben Punkt: @dpas Beobachtung, dass die Unwucht wirksam
klingt und mit "Rotor Hz" eigentlich zusammengehören müsste. Beim echten
Hubschrauber sind Motor und Rotor über ein Getriebe fest gekoppelt; hier sind
es zwei Regler, weil @dpa sie ausdrücklich getrennt wollte ("Motor, und
Rotoren mit Geschwindigkeit extra"). Nicht angefasst, bis er entschieden hat.

### Scope-Sync: erst die Grundwelle, dann die Flanke daneben

@dpas Vorschlag ist gebaut, zweistufig: das Fenster wird tiefpassgefiltert
(übrig bleibt die Grundwelle mit genau EINEM steigenden Nulldurchgang je
Periode), der grobe Fund wird dann im Rohsignal auf die nächste echte Flanke
gezogen.

Gefiltert wird vorwärts **und** rückwärts. Ein einfacher Durchlauf verschöbe
die Grundwelle um seine Gruppenlaufzeit, und um genau die läge der grobe Fund
daneben. Zwei Durchläufe gegeneinander heben die Phasendrehung exakt auf; das
geht, weil das ganze Fenster schon vorliegt.

Die Grenzfrequenz kommt aus dem Signal selbst: die Nulldurchgangsrate ist eine
grobe Frequenzschätzung, die von den Obertönen nach oben gezogen wird, ein
Viertel davon liegt zuverlässig unter der Grundwelle. Eine feste Zahl wäre für
jedes zweite Signal falsch.

Gemessen (`load_check`, "Scope-Sync": sechs aufeinanderfolgende Fenster eines
Klangs aus 220 Hz plus kräftigem siebten Teilton, verglichen über ihre
Ähnlichkeit):

| | Wert |
|---|---|
| ohne Sync | 0,585 |
| Sync vorher | 0,486 |
| Sync nachher | 0,998 |

Der alte Sync war also **schlechter als gar keiner** - genau das, was @dpa
gehört hat.

### Beschriftungen in beiden Sprachen

`Source/UI/Labels.h` ist eine Ersetzungstabelle, kein Schlüsselverzeichnis: im
Code steht weiterhin der deutsche Text, `Labels::text()` liefert im EN-Betrieb
seine Entsprechung. Eine fehlende Übersetzung fällt damit nicht aus, sondern
zeigt eben Deutsch.

Jeder Knob merkt sich seinen deutschen Quelltext, damit `refreshTooltips()`
beim Sprachwechsel auch die Beschriftung mitnimmt. Knöpfe, deren Text ihren
Zustand zeigt, bekommen dabei den Text zum aktuellen Zustand.

Die Auswahlfelder zeigen ihre Einträge übersetzt (`changeItemText`, die
Auswahl bleibt stehen). Die **Parameter** behalten ihre deutschen Einträge:
sie stehen in der Automationsliste des Hosts und dürfen sich nicht mit der
Anzeigesprache ändern.

Geprüft im `load_check`-Abschnitt "Beschriftungen EN": der echte Editor wird
einmal je Betriebsart gebaut, die Sprache steht auf Englisch, alle sichtbaren
Texte werden eingesammelt (3265 Stück). Keiner steht noch deutsch da, keiner
trägt einen Umlaut.

## Stand 2026-08-24 abends (Wackler, Stossfront-Modell, Rotor-Doppler)

Zwei Durchgänge von @dpa (16:20 Bewegung, 17:53 Motor/Physik). Alles
gemessen, **gehört hat @dpa es noch nicht.**

### Der Wackler ist wieder ein Dreiachser

Die Rotoren-Betriebsart des Positions-Wacklers ist entfallen (@dpa: "nicht
mehr nur auf der XY Ebene und nicht nur im Kreis (wir haben Hubschrauber ja
extra)"). Eine umlaufende Bewegung gehört zum Motor, nicht zur Mikrobewegung
der Position. Mit ihr sind "Randomize" und "Z-Jit" weg - beide wirkten nur
dort. Alte Presets laden unverändert, die drei Parameter werden ignoriert.

**Ausschlag und Hektik werden angefahren, nicht gesetzt.** Ein Ruck am
Ausschlag-Regler war ein Positionssprung: 0 auf 200 m innerhalb eines Ticks
sind 200000 m/s, für den Löser Überschall samt Kegelankunft und N-Welle -
genau das, was @dpa als "sehr laut beim Verstellen" gehört hat. Angefahren
wird über einen kurzen Ein-Pol, dessen Schrittweite unter dem eigenen
Tempo-Deckel des Wacklers liegt. Gemessen im `load_check`-Abschnitt
"Wackler", bei Jit Max 100 m/s:

| | vorher | nachher |
|---|---|---|
| \|M_r\| max | 1,04 | 0,40 |
| Ausgangsspitze | 0,9900 | 0,0142 |

Das ist zugleich der wahrscheinlichste Grund für @dpas Front-Duck-Bild
("bei 1 ist nichts mehr (außer Knalle)"): ein Wackler, der über Mach 1
springt, löst fortwährend Stoßfronten aus und hält die Absenkung damit
dauerhaft offen.

"Jit Max" folgt jetzt der eingestellten Tempo-Einheit, wie die anderen drei
Tempo-Regler.

### Stoßfront, Druckbeule und die Reichweite der Absenkung

@dpas Analyse war die Vorlage: eine unterschallige Beschleunigung schiebt eine
einseitige Verdichtung vor sich her, erst Überschall bringt das N mit seinen
zwei Fronten, und mit der Entfernung zerfällt beides zu Grollen "samt allem
drumherum".

Drei Änderungen daraus:

- **Der Sprungknall senkt den übrigen Schall nicht mehr ab.** Er löste bisher
  dieselbe Absenkung aus wie eine echte Stoßfront und schaltete damit für
  seine eigene Dauer alles andere stumm - man hörte Motor weg, Knall, Motor
  zurück ("das ist wie eine kurze Unterbrechung"). Eine Beschleunigungswelle
  ist keine Stoßfront, hinter der nichts herkommt; sie läuft MIT dem übrigen
  Schall. Gemessen im Ankunftsfenster: Umfeld-RMS 0,00636 → 0,05880.
- **Unter Mach 1 ist der Sprungknall eine einseitige Druckbeule**
  (`Branch::nSingleSided`, halbe Sinuskuppe statt Rampe durch null). Die
  Umschaltung hängt an der Sprunghöhe, die die Engine ohnehin mitgibt.
- **Neuer Regler "Duck-Reichw."** (@dpa: "wir müssen also bestimmen ab welcher
  entfernung die N-Wave noch 'echt' ist"). Die Absenkung fällt mit
  `range/(range+R)`, bei genau dieser Entfernung also auf die Hälfte.
  Default 300 m.

Was @dpa an Luftübertragung noch ansprach - Tiefpass mit der Entfernung,
Verbreiterung der Front zum Grollen hin - ist beides schon da: die
Luftdämpfung sitzt als One-Pole je Zweig (`airAbsorbAmount`), und `nRise`
wächst mit `2e-6 * R`, also 0,2 ms auf 100 m gegen 6 ms auf 3 km. Was gefehlt
hat, war nicht das Grollen, sondern dass die Absenkung das Drumherum
weggeschnitten hat.

### Rotor: Knattern über echten Doppler

Neuer Schalter "Doppler" bei Hubschrauber und Propeller, dazu "Blattlänge".
Aus bleibt alles wie bisher (nachgebaute Modulation). An ist jedes Blatt eine
eigene Quelle auf der Kreisbahn: eigenes Rauschen, eigene
Verzögerungsleitung, eigener Schlag.

Zwei Dinge waren dabei entscheidend, beide erst durch Messung sichtbar:

- **Die Laufzeit allein trägt den Effekt nicht.** Sie verschiebt die Schläge
  im Takt, ändert aber kaum ihre Lautstärke, und bei je eigenem Rauschen je
  Blatt entsteht auch keine Interferenz. Was man hört, ist die Richtwirkung:
  eine Quelle, die auf einen zuläuft, strahlt um `1/(1-M_r)²` stärker ab. Am
  Rotor ist das gewaltig - bei 6 m Blatt und 5 Umdrehungen/s läuft die Spitze
  mit Mach 0,55.
- **Der Schlag muss am Viertelumlauf feuern**, wo das Blatt am schnellsten auf
  den Hörer zuläuft. Am Umlaufpunkt selbst ist die Radialgeschwindigkeit null;
  dort bekommt der Schlag keine Richtwirkung ab und das Knattern bleibt in
  jeder Lage gleich - genau das war beim ersten Versuch gemessen worden.

Der Überflug kommt daher, dass der Processor durchreicht, wie stark die
Sichtlinie in der Rotorebene liegt (`setRotorInPlane`): von der Seite 1,
senkrecht darunter 0. Mit ihr geht M_r auf null.

Gemessen (`load_check`, "Rotor-Knattern", Modulationstiefe der Hüllkurve):

| | von der Seite | von unten |
|---|---|---|
| gefaket | 0,881 | 0,881 |
| Doppler | 0,392 | 0,228 |

Die acht Blattquellen bekommen gestreute Startwerte statt benachbarter
(@dpa: "achte bitte bei ab 2 unterschiedlichen Noises darauf, dass sie
unterschiedlich sind"). Der Abschnitt "Rauschquellen" misst die
Kreuzkorrelation nach - höchster Wert 0,0054. Ohne das wären N Blätter
klanglich ein einziges.

### Gegenprobe

Von 45 bestehenden `load_check`-Szenarien ändern sich vier, alle gewollt: die
drei mit Knall-Start (lauter, weil der Knall sich kein Loch mehr schneidet)
und "Vorbeiflug Mach 1,04" (die Absenkung greift dort jetzt
entfernungsabhängig).

## Stand 2026-08-24 (Sprungnaht, Sprungknall, Knall-Trigger)

Vier Punkte aus @dpas Durchgang von 15:52. Gemessen ist alles, **gehört hat
@dpa es noch nicht.**

### Die Sprungnaht: eine Positionsänderung, die keine Bewegung ist

@dpas Vermutung, dass CPU-Spitze beim State-Laden und Loop-Übergang dieselbe
Naht sind, war richtig - nur nicht als gemeinsamer Codepfad, sondern als
gemeinsame Fehlerklasse:

> Eine Positionsänderung, die keine Bewegung ist, läuft durch die
> Bewegungsglättung und steht damit als echte Bewegung in der Bahn.

Bei 1400 m und tau 0,145 s sind das einige tausend m/s. Der Löser sieht dort
Überschall, spaltet die Wurzel auf und rechnet ein Vielfaches - und zwar nicht
als kurze Spitze, sondern so lange, wie die schnelle Stelle in der
Vorgeschichte der Bahn steht (Pufferlänge, mehrere Sekunden).

Neu ist deshalb der **Schnitt** (@dpa: "Ende erreicht, leise, umbau, laut,
start"):

1. Ausblenden über `cutFadeSeconds` (12 ms), Quelle steht dabei still.
2. Bei Pegel null: `DopplerEngine::cutTo()` setzt **beide** Geometriesätze an
   der neuen Stelle mit ruhender Vorgeschichte auf, die Glätter dazu. Nichts
   läuft doppelt, nichts wird überblendet - der Unterschied zu
   `jumpSourceTo()`, bei dem zwei komplette Lösersätze gegeneinander laufen.
3. Einblenden.

Der Signalpuffer bleibt dabei stehen. Weil die Vorgeschichte der Bahn an der
neuen Stelle vollständig gefüllt wird, klingt der neue Ort sofort statt erst
nach der Laufzeit.

Die Zustandsmaschine (`CutState` in `PluginProcessor.h`) läuft im Audiothread;
Schritt 2 passiert am Anfang des Blocks nach dem Ende der Ausblende, also
sicher im stillen Fenster. Ausgelöst wird sie von drei Stellen:
`setStateInformation()`, dem Rundenende der Wiedergabe und dem Start einer
Wiedergabe.

Die Wiedergabe wickelt am Rundenende **nicht mehr selbst um**: sie bleibt auf
dem letzten Frame stehen und meldet es (`MotionPlayer::atLoopEdge()`), der
Processor schneidet und ruft `restartRound()`. Der Überhang über den letzten
Frame geht dabei nicht verloren.

**Der automatische Engine-Restart nach einem State-Load ist entfallen.** Er
war der bisherige Weg, die Quelle ohne Anflug an die geladene Stelle zu
bekommen (@dpa: "bei/nach jedem State-load Engine Restart triggern"), kostet
aber zweierlei: er hält den Audiothread über `prepareToPlay()` an (gemessen
3,7 bis 7,1 ms bei 10,7 ms Blockbudget) und leert dabei den Signalpuffer -
danach ist es still, bis der Schall die neue Strecke einmal zurückgelegt hat,
bei 1000 m gut drei Sekunden. Beides fiel bei jedem Preset-Wechsel an. Der
Knopf "Audiomotor neu anlassen" bleibt unverändert von Hand erreichbar; er ist
weiterhin das Mittel gegen den ungeklärten Ton-Ausfall.

Gemessen wird das im neuen `load_check`-Abschnitt **"Sprungnaht"**, Maß ist
der teuerste Block in Löser-Auswertungen gegen dieselbe Lage ohne Eingriff:

| Eingriff | vorher | nachher |
|---|---|---|
| Zustand geladen | 15782 gegen 216 (247 x) | 444 gegen 368 (1,2 x) |
| Rundenwechsel | 29558, \|M_r\| 16,1 | 446, \|M_r\| 0,33 |
| Motor neu anlassen | 387 gegen 311 (1,2 x) | unverändert |

Die dritte Zeile ist die Gegenprobe zur Vermutung: "Engine Reset" hatte auf
der Löserseite nie eine Spitze - er macht schon immer genau das Richtige
(Zeitachse zurücksetzen statt hinfliegen). Was er kostet, ist die
Zwangspause oben, und die taucht in keiner Blockzeit auf.

Der Abschnitt misst außerdem eine Kontrolllage, die nie angeflogen wurde
("Ruhe fern"). Sie kostet 368 Auswertungen - die weite Lage allein ist also
nicht teuer, nur der Anflug dorthin war es.

### Sprungknall: die Schicht war da, nur aus

@dpa: der Knall der Startvariante "Knall-Start" soll wie der Raketen-Stoß
hörbar sein, ist es aber nicht.

Die Schicht dafür gibt es seit `480fe09` und sie wirkt messbar - im
`load_check`-Abschnitt "Knall-Start" hebt die Druckwelle die Spitze im
Ankunftsfenster von 0,0179 auf 0,1697, also um Faktor 9,5. Zu hören war sie
trotzdem nicht:

- **Sprungknall stand auf 0.** Wer "Knall-Start" wählt, meint den Knall. Der
  Default steht jetzt auf 0,5.
- **Die Regler saßen im falschen Panel:** Feld/Physik/Ausgang, drei Panels
  entfernt von der Startvariante, die sie auslöst. "Sprungknall" und
  "Sprungkante" stehen jetzt in der Vorbeiflug-Gruppe, direkt bei Bahn und
  Startvariante.

Zu @dpas Frage, ob es zwischen Knall und Überschall einen Unterschied gibt:
ja, und er ist schon gebaut. Beide nutzen dieselbe N-Wellen-Form, aber
getrennte Auslöser und getrennte Regler - der Überschallknall hängt an
`M_r = 1` und am Schalter "N-Wave", der Sprungknall an der Sprungmarke und an
"Sprungknall". Der Sprungknall braucht "N-Wave" ausdrücklich **nicht**: ein
unterschalliger Start soll knallen dürfen.

### Scope: Knall-Trigger statt Nulldurchgang

Der vorhandene Sync richtet an einem steigenden Nulldurchgang aus. Ein Knall
hat keine Periode, an der man ausrichten könnte, sondern einen Einsatz - der
neue Trigger sucht genau den: ein schneller Hüllkurvenfolger (2 ms) gegen
einen langsamen (150 ms), ausgelöst bei 12 dB Abstand und über einer
Pegelschwelle, damit in der Stille nicht jedes Rauschen feuert.

Das Ereignis landet **mittig** im Bild: gesucht wird nur dort, wo hinter dem
Fund noch ein halbes Anzeigefenster an Nachlauf im Rohfenster steht. Danach
steht das Bild für die eingestellte Haltezeit (0,5/1/2/5 s) und schaltet sich
selbst wieder scharf; zwischen zwei Einsätzen bleibt das letzte Bild stehen,
statt zu wackeln. Unten rechts steht "scharf" oder "gehalten". Der manuelle
Freeze behält Vorrang.

Damit derselbe Knall nicht mehrfach auslöst - die Rohfenster überlappen bei
30 Hz Anzeigetakt stark -, vergleicht der Trigger die absolute Position im
Ringpuffer (neu: `ScopeRingBuffer::writePosition()`, durchgereicht an
`ScopeComponent::feed()`).

Geprüft wird das im `load_check` ohne Fenster und ohne Audio: ein gebautes
Rohfenster mit einem Einsatz bei Sample 6000 von 9600 steht im Bild bei 2398
von 4800, also 2 Samples neben der Mitte; ein anschließend gefüttertes leeres
Fenster lässt das Bild unverändert stehen. Ausgelesen wird über
`exportVisibleWindow()` - den sichtbaren Ausschnitt gibt es sonst nicht von
außen zu sehen.

### Gegenprobe

Von den 45 bestehenden `load_check`-Szenarien bleiben 43 bitgleich. Die zwei
Ausnahmen sind gewollt und beide vom neuen Sprungknall-Default:
"Vorbeiflug Knall, Start" (Spitze 0,0301 -> 0,0874) und "Vorbeiflug Knall,
Rest".

## Stand 2026-08-24 (Klangformung, Stoßwellen, Umlaute)

Vier Punkte aus @dpas Durchgang. Drei davon waren echte Fehler, keine
Geschmacksfragen.

### Umlaute: JUCE liest `const char*` als Latin-1

"DÃ¼senantrieb" stand in der Betriebsart-Auswahl. Die Quelldateien sind
richtig UTF-8 - der Fehler sitzt eine Ebene tiefer: `juce::String (const
char*)` nimmt `CharPointer_ASCII` an, also ein Zeichen je Byte. Aus den zwei
UTF-8-Bytes von "ü" (C3 BC) werden dabei die zwei Zeichen "Ã¼".

Das betrifft **jede** sichtbare Zeichenkette mit einem Zeichen jenseits von
ASCII, nicht nur Umlaute - auch das Gradzeichen der Winkel-Parameter stand als
"Â°" da.

Behoben mit `Text::utf8()` in `Source/Util/Utf8.h`. Der Umbau ist klein
geblieben, weil die Texte an wenigen Engstellen zusammenlaufen:

- `Tooltips::text()` ist die **einzige** Stelle, an der die rund 260
  Hilfetexte zu einem `juce::String` werden. Eine Zeile dort repariert alle.
- `Params.cpp` hat acht Literale (Einheiten und die Betriebsart-Liste).
- Einzelne Beschriftungen in `FieldPanel.cpp` und `EnginePanel.cpp`.

Gegen das Wiederauftreten gibt es jetzt eine **Prüfung im `load_check`**: sie
baut den echten Editor - einmal je Betriebsart, weil das Motor-Panel in jeder
andere Regler zeigt -, läuft rekursiv über alle Komponenten und sammelt
Beschriftungen, Knopftexte, Auswahleinträge und Hinweise ein, dazu die
Parameternamen und -einheiten aus der Automationsliste. Rund 3500
Zeichenketten. Erkannt wird an den Zeichen `Â` (U+00C2) und `Ã` (U+00C3): sie
sind die erste Hälfte jedes so verunglückten Zeichens und kommen in keinem
Text dieses Plugins legitim vor.

Wichtig für Folge-Sessions: **im Quelltext ist der Fehler nicht zu sehen**,
dort steht das "ü" richtig. Nur der Lauf zeigt ihn. Die Prüfung wurde
gegengeprüft, indem der Fix in `Tooltips::text()` kurz zurückgedreht wurde -
sie schlug sofort an.

### Dreiband-Klangformung für Düse und Rakete

@dpa: "Düsenantrieb hat einfach nur weises Rauschen? Das braucht einen
Klangveränderungsknob und/oder eine Auswahl an vorgefertigten (multiband?)
Filtern (am besten beides)." Es ist beides geworden.

`EngineGenerator::BandVoicing` ist eine Zerlegung **einer** Rauschquelle in
Tiefband (Tiefpass), Mittenband und Hochband (beide Bandpass), dazu ein
schmaler vierter Zweig für einen singenden Ton - den Verdichter des Turbojets.
Alle drei Bänder bekommen dasselbe Rauschsample; drei getrennte Quellen
klängen breiter und leerer zugleich, weil sich nichts mehr überlagert.

Vorlagen in `jetVoiceTable` / `rocketVoiceTable` (Reihenfolge bindend, sie ist
die von `Params::jetVoice` bzw. `Params::rocketVoice`). Zwei getrennte Listen,
weil ein Düsenstrahl und ein Raketenbrüllen nicht dieselben Klangfarben haben.

Zwei Konstruktionsentscheidungen, die den Unterschied machen:

**Das Hochband ist ein Bandpass, kein Hochpass.** Ein Hochpass lässt alles bis
zur Nyquistgrenze durch, und weißes Rauschen hinter einem Hochpass ist immer
noch weißes Rauschen - genau das, was @dpa gehört hat. Erst ein oben
begrenztes Band macht aus der Zerlegung eine Klangfarbe.

**Bandbreitenausgleich.** Ohne ihn bedeuten die Pegel in der Vorlage nicht,
was sie sagen: ein Tiefpass bei 220 Hz lässt aus weißem Rauschen ein
Zweihundertstel der Energie durch, ein breites Band bei 3 kHz ein Viertel. Das
Tiefband wäre chancenlos, egal wie die Pegel dastehen, und der Turbofan klänge
so hell wie der Turbojet. Jedes Band wird deshalb mit der Wurzel des
Bandbreitenverhältnisses hochgezogen - erst dann heißt "lowGain 1,0" wirklich
"dieses Band in voller Lautstärke". Das war im ersten Wurf falsch und ist
durch die Messung aufgefallen, nicht durch Nachdenken.

Der Klangfarbe-Regler (`jetTone` / `rocketTone`) kippt **Bänderpegel UND
Eckfrequenzen** (gut eine Oktave in jede Richtung). Nur die Pegel zu kippen
klänge nach einer Höhenblende; erst die wandernden Frequenzen machen daraus
einen anderen Klang statt eines lauteren Bandes.

Gemessen wird im `load_check` über einen FFT-freien Schätzer des spektralen
Schwerpunkts (`f ~ (fs/2π)·RMS(Δ)/RMS(x)`). Geprüft wird nicht der Absolutwert
- der Schätzer wichtet mit `f²` und liegt darum höher, als das Ohr urteilen
würde -, sondern dass die Vorlagen sich um mindestens Faktor 1,5
unterscheiden und der Klangfarbe-Regler den Schwerpunkt durchgängig anhebt.

### Die Druckstöße der Rakete sind jetzt N-Wellen

@dpa: "'Druckstoß' sind .. laute noise stöße?? Bullshit!! ... Die Druckstöße
sind Überschall, also donnernde N-Waves - oder nicht? jedenfalls klingen die
Noise Decays lächerlich." Er hatte recht: es war bandpassgefiltertes Rauschen
mit exponentiellem Decay.

Jetzt ist es die N-Wellen-FORM: senkrecht auf +1, lineare Gerade durch null,
senkrecht von −1 zurück. `EngineGenerator::nWaveShape()` ist eine **eigene
Kopie** der Form aus `PropagationPath::nWaveAt()` und zapft die
Ausbreitungsschicht bewusst nicht an - die hängt an M_r und bliebe stumm,
solange die Rakete unterschallig fliegt. Hier ist aber nicht die Rakete
überschallschnell, sondern ihr Abgasstrahl.

- Kein Filter über dem Stoß. Die Form ist der Klang; ein Bandpass würde genau
  die senkrechten Fronten abrunden, um die es geht.
- Die Dauer kommt aus `rocketShockSize` (Meter, `T = 2·Größe/c`), wie bei
  `nWaveSize`. Klein ist ein Peitschenknall, groß ein Donnern.
- Die Folge kommt aus `rocketShockRate`. Vorher stand sie als 18 Hz fest im
  Generator.
- Anstiegszeit 0,5 % der Dauer, mindestens zwei Samples. Die Quelle steht
  direkt an der Düse, dort ist die Front noch nicht durch den Weg durch die
  Luft verbreitert.
- **32 gleichzeitige Stöße.** Bei hoher Folge überlappen sie, und dieses
  Übereinander ist das Knattern. Ein neuer Stoß sucht sich einen freien Platz
  und fällt aus, wenn keiner frei ist - einen laufenden zu überschreiben hiesse,
  ihn mitten in der Flanke abzuschneiden, also ein Knacken.
- Amplitude streut, das **Vorzeichen nicht**: eine Stoßwelle beginnt immer mit
  Überdruck. Zufälliges Vorzeichen machte aus der N-Welle wieder ein Rauschen.

`rocketShockLevel` steht auf 4,0 und damit deutlich über dem Brüllen. Bei voll
aufgedrehtem Regler darf das übersteuern - dafür gibt es den sichtbaren
Begrenzer, und ein stiller Deckel wäre hier das Falsche.

Nachgewiesen wird das im `load_check` an der **Flankensteilheit**: eine
N-Welle springt in wenigen Samples auf ihren vollen Wert, das gefilterte
Brüllen kann das nicht. Gemessen 5,0-fach steilerer Sample-zu-Sample-Sprung
mit Stößen als ohne (Schwelle 3,0).

### Offen für @dpa

Sollen die Stöße **zusätzlich** echte N-Wellen in der Ausbreitung auslösen,
also mit Doppler und eigener Laufzeit je Hörweg? Bisher sitzen sie
ausschließlich im Quellsignal. Die Frage steht seit dem vorletzten Durchgang.

### Max Speed unter "Bewegung"

@dpa: "als letztes, kleiner, Abgeschnitten, hinzugequetscht". Der Tempo-Deckel
stand am Ende der gemeinsamen Zeile - und weil `MotionPanel::resized()` von
links wegnimmt, war er der erste, dem bei knapper Panelbreite die Hälfte
fehlte. Er steht jetzt als erstes in der Zeile, 128 statt 100 px breit, mit
fetter Beschriftung und einer Lücke dahinter, die zeigt, dass er nicht zum
Jitter gehört, sondern über beiden Reitern steht.

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
