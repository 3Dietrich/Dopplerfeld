# dopplerfeld - Architektur-Karte

Kurzeinstieg für KI-Sessions (Claude o.ä.), damit man nicht erst den ganzen
Code oder die komplette Chat-Historie lesen muss - hier nur die Landkarte, was
wo liegt und wie es zusammenhängt. Die Herleitung der Physik dahinter steht in
[docs/physik.md](docs/physik.md).

## Was das Plugin ist

JUCE-Audio-Plugin (AU/VST3/Standalone), physikalische Nachstellung des
akustischen Dopplereffekts inkl. Überschall/Mach-Kegel. Eine Schallquelle M
und ein Hörer L (mit Kopf-Orientierung) auf einer 700x400px-Fläche, die eine
reale Modellgröße n Meter darstellt. Ton entsteht aus echter Laufzeit
(retarded time), nicht aus einem Doppler-Formelfaktor.

![Dopplerfeld-UI: Feldanzeige links, Regler-Panels rechts](docs/screenshot.png)

Baubar mit CMake (siehe `../granular/` als Schwesterprojekt für dasselbe Muster,
JUCE liegt unter `~/Documents/JUCE`, `add_subdirectory` statt Kopie).

## Schichtenmodell

```
Source/Physics/    - JUCE-frei, reines C++. Retarded-Time-Löser, Ausbreitung,
                      Trajektorien-/Signalpuffer. Offline testbar
                      (solver_check), unabhängig vom Audio-Framework.
Source/Motion/     - JUCE-frei. Bewegungsglättung (4 Verfahren:
                      CriticallyDampedSpring, OneEuroSmoother, OnePoleSmoother,
                      SlewLimiter hinter dem Interface MotionSmoother),
                      Aufnahme/Wiedergabe (MotionRecorder/MotionPlayer), dazu
                      die beiden Bewegungsquellen FlyByGenerator (Vorbeiflug)
                      und PositionJitter (Wackler).
Source/Sources/    - Klangquellen (Motor-Generator, Sample-Player, Live-
                      Audioeingang) + deren Crossfade. Nutzt JUCE-DSP, aber
                      kein JUCE-Audio-Framework (kein AudioProcessor-Wissen).
Source/Reverb/     - JUCE-frei wie Physics, und aus demselben Grund: der Hall
                      lässt sich offline messen und anhören (reverb_check,
                      reverb_probe). Gemeinsame Bausteine in ReverbParts.h
                      (Verzögerungsleitung, Dämpfung, Allpass), darauf vier
                      Bauarten hinter der Schnittstelle ReverbUnit
                      (AllpassDiffuser, SchroederReverb, FdnReverb,
                      OpenAirReverb) plus EarlyReflections. TapBus haengt
                      daran, was ein Abgriffpunkt sonst noch braucht: Vorlauf,
                      Breite, Pegel, Wand-Rückwege.
Source/Util/       - Crossfade-Engine (generisch), FieldSnapshot-Datenformat,
                      ScopeRingBuffer (SPSC-Ring für das Oszilloskop),
                      Utf8 (Umlaute sicher an JUCE übergeben).
Source/UI/         - JUCE-Component-Schicht: Feldanzeige, Regler-Panels,
                      Kopf-/Quellen-Symbole, Oszilloskop, Pegelmesser,
                      Begrüßungsfenster, Zustandsstreifen (PresetBar); alle
                      Bedientexte zentral in Labels.h/Tooltips.h, alle Farben
                      und Reglermaße in Theme.h.
Source/*.cpp (Wurzel) - PluginProcessor/PluginEditor: Zusammenbau, kennt alle
                      Schichten, enthält selbst möglichst wenig Logik.
Tests/             - fünf ctest-Tests: solver_check (Physik-Löser gegen
                      geschlossene Lösung), load_check (Lasttest inkl.
                      Extremfälle, offline), reverb_check (Hallbauarten),
                      scope_boom_probe (Knall-Ansicht des Scopes) und
                      repo_check (Python, ohne Build). Daneben liegen hier
                      dreiundzwanzig Messprogramme, die bewusst KEIN Test
                      sind (siehe Build & Test), und in Tests/fixtures die
                      Dateien, die ein ctest-Test öffnet.
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
  Dazu kommen **acht Abgriffpunkte** (`maxTaps`, `TapState`/`TapBus`):
  Empfangspunkte im Feld, die nicht zu einem Ohr gehören. Sie kosten je einen
  Löser statt zweier - ein Punkt hat kein zweites Ohr - und werden über
  dieselben Flächen beliefert wie die Ohren (Direktweg, Boden, beide Wände,
  erste Ordnung). Die Pfade rendern sie als zusätzliche **Kanäle** neben den
  zwei Ohrkanälen, deshalb laufen sie beim Geometrie-Crossfade von selbst mit.
  Was dort ankommt, geht in den Hall und von dort in den Ausgang; der Rückweg
  zum Hörer ist ein Vorlauf und keine zweite Ausbreitung. Über `chainTo` geht
  ein Punkt statt auf die Ohren in einen **späteren** Punkt hinein - nur nach
  hinten, damit die Rechenreihenfolge zugleich die Kettenreihenfolge ist und
  ein Kreis nicht entstehen kann.
- **`TapBus`** (Reverb/) - was an einem Abgriffpunkt hängt: Vorlauf, Hall,
  Breite, Pegel, dazu die Rückwege über die Wände. Alle vier Bauarten liegen
  gleichzeitig bereit, damit ein Typwechsel im Audiothread nichts allokiert
  (und damit automatisierbar bleibt). Wie viel RAM das kostet, entscheidet die
  Kapazität: die Leitungen sind auf den wirklich gebrauchten Raum bemessen
  (`reverbparts::capacityFor`), nicht auf den größten einstellbaren. Verlangt
  der Regler mehr, wird der Raum geklemmt und der Mehrbedarf gemeldet - das
  Nachbemessen allokiert und gehört deshalb in den Nachrichtenthread
  (`DopplerfeldProcessor::growTapCapacityIfNeeded`). Mono rein, Stereo raus:
  die Breite entsteht im Hall selbst, geregelt wird sie danach hier.
- **`SoundSourceHolder`** (Sources/) - crossfadet beim Wechsel der Klangquelle
  (`SourceKind`: `EngineGenerator`, `SampleSource`, `AudioInSource`), ist
  selbst eine `SoundSource` und von außen nicht als Doppelpfad sichtbar.
  `AudioInSource` fällt aus dem Pull-Muster heraus: der Host liefert den
  Eingang einmal je `processBlock()`, der Processor schiebt ihn mit
  `pushBlock()` hinein, bevor die Blockverarbeitung beginnt.
- **`DopplerfeldProcessor`** (PluginProcessor) - hält alle Instanzen, liest
  Parameter pro Block über gecachte Rohzeiger (`raw()`-Helper, kein APVTS-
  Listener - alle Setter laufen dadurch ausschließlich im Audiothread),
  fährt die Engine in 128-Sample-Teilblöcken (`motionChunkSamples`).
  Record/Play laufen über atomare Request-Flags (Message→Audiothread), nicht
  über eine volle Kommandoqueue (bewusste Vereinfachung, siehe Kommentar
  dort).
- **`DopplerfeldEditor`** (PluginEditor) - `FieldComponent` (700x400) links,
  rechts in einem Viewport acht `CollapsiblePanel`, jedes mit genau einer
  Panel-Klasse als Inhalt: `EngineControlPanel` (Motorsteuerung),
  `EnginePanel` (Motor), `SamplePanel` (Sample), `MotionPanel` (Bewegung),
  `FieldPanel` (Feld/Physik/Ausgang), `WallPanel` (Reflexionen/Wände),
  `ReverbPanel` (Hall/Abgriffpunkte), `SwarmPanel` (Schwarm/Klone).
  Die Kopfzeile einer Hülle trägt, was man im Vergleich ständig umschaltet und
  wofür ein Aufklappen zu teuer wäre: der Motor seinen An-Schalter, der Hall
  seinen Bypass, die Wände "1", "2" und "++". Welche davon offen stehen, gehört zum
  Zustand: eine Bitmaske (`panelsOpen`) reist im Preset mit, ein Preset ohne
  die Property klappt alles zu. Über dem Feld liegt der Zustandsstreifen
  (`PresetBar`): Liste, zwei Pfeile, Sichern/Neu/Ordner - laden und speichern
  ohne den nativen Dateidialog, dieselben Dateien wie "Save/Load State" der
  Standalone-App. Unter dem Feld sitzt
  das Oszilloskop (`ScopeComponent` + Werkzeugleiste aus Ein/Aus, Freeze,
  Sync, Play; wegschaltbar, dann schrumpft das Fenster mit), darunter
  CPU-Balken und Statuszeile. Der Pegelmesser (`LevelMeter`) gehört dem
  `FieldPanel` und trägt zugleich die Clip-Marke des Master-Begrenzers.
  Als letztes Kind über allem liegt das `WelcomeOverlay`, das nur beim
  allerersten Start erscheint (`hasBeenSeen()`/`markAsSeen()`). 30-Hz-Timer
  holt `FieldSnapshot` ab, aktualisiert Statuszeile/Button-Texte.
  Zum allerersten Start gehört auch der Klang: `loadStartPresetOnFirstRun()`
  im Processor-Konstruktor spielt einmalig das mitgelieferte Start-Preset ein
  (siehe Stand-Abschnitt weiter unten).

## Parameter

Alle IDs zentral in `Source/Params.h` (`namespace Params`), Layout in
`Params.cpp::createParameterLayout()`. Jeder Regler dort eine Zeile - min/max/
step ändern heißt: diese eine Zeile ändern, nicht durchs UI suchen.

Dasselbe Muster für die Texte: **Beschriftungen** stehen in
`Source/UI/Labels.h`, **Hilfehinweise** in `Source/UI/Tooltips.h`, beide
zweisprachig (deutsch/englisch) und über einen Schlüssel angesprochen - kein
Text-Literal direkt am Regler. Die Sprache ist globaler Zustand in Tooltips
(`setLanguage()`/`toggleLanguage()`); die Hinweise selbst sind per
`ToggleableTooltipWindow` ganz abschaltbar. Umlaute gehen nie als nacktes
`const char*` an JUCE, sondern über `Source/Util/Utf8.h` - sonst liest JUCE
sie als Latin-1.

Eine Ausnahme in der Einheitenwahl: `srcX/srcY/lisX/lisY` sind auf die
Feldfläche normiert (0..1) und werden erst im Processor mit dem Feldmaßstab
multipliziert, `srcZ/lisZ` stehen dagegen in echten Metern. Die Höhe hängt
nicht am Maßstab - ein Feldwechsel von 100 m auf 10000 m darf den Hörer nicht
mitwachsen lassen. Ein Feldgrößenwechsel verschiebt deshalb x/y, aber nie z.

## Build & Test

```
cmake -B build
cmake --build build --config Release -j 4
cd build && ctest --output-on-failure     # fünf Tests, siehe unten
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

`reverb_check`: die vier Hallbauarten offline, ohne JUCE und ohne Audiogerät -
Stabilität, Abklingzeit gegen ihre Vorgabe, Orthogonalität der Mischmatrix,
kein NaN. Was die Sache kostet, steht bewusst nicht dort, sondern im
`reverb_probe`: Rechenzeit hängt an der Maschine und hat in einem Test nichts
verloren, der auch auf einem ausgelasteten CI-Rechner grün sein muss.

`scope_boom_probe`: füttert die `ScopeComponent` so, wie es der Editor-Timer
tut, und zählt je Zoomstufe, wie viele der eingespeisten Knalle im Bild landen.
Mit Unter- UND Obergrenze - zu selten ist der eine Fehler, Zappeln auf Rauschen
der andere.

`repo_check`: Python, braucht keinen Build und läuft in Sekunden. Er liest die
Quellen der ctest-Tests, sammelt jeden Pfad, den sie über
`DOPPLERFELD_SOURCE_DIR` öffnen, und vergleicht ihn mit dem, was git wirklich
führt. Was ein Test öffnet, gehört eingecheckt - sonst ist er auf dem eigenen
Rechner grün und auf einem frischen Klon rot, und rot wird er erst draußen bei
GitHub. Der Ort dafür ist `Tests/fixtures` (siehe das README dort), ausdrücklich
nicht `presets/`, das der Release-Schritt ins Nutzer-Zip kopiert.

Zu bauen ist die komplette Konfiguration, nicht nur die Standalone: die
Testbinaries hängen an denselben Quellen, `--target Dopplerfeld_Standalone`
lässt sie stehen und `ctest` liefe danach gegen den alten Stand.

**Messprogramme neben den Tests.** In `Tests/` liegen außer den fünf
ctest-Tests dreiundzwanzig Diagnose-Programme, die absichtlich keine Tests sind:
sie messen oder zeichnen etwas, statt ein Kriterium zu prüfen, und würden eine
grüne Testsuite nur verwässern. Einundzwanzig davon stehen als
`EXCLUDE_FROM_ALL`-Targets in `CMakeLists.txt` und werden gezielt gebaut
(`cmake --build build --target <name>`):

| Target | misst / zeigt |
|---|---|
| `loop_peak` | Rundenpunkt einer Wiedergabe |
| `burst_probe` | lauter Ausbruch nach einem Preset-Wechsel |
| `swarm_probe` | Klon-Schwarm |
| `coast_probe` | Nachlauf nach dem Loslassen |
| `preset_probe` | "manchmal zu leise" nach einem Preset-Wechsel |
| `grab_probe` | Anfassen von M |
| `trail_probe` | die "Fahne" hinter dem Überschallknall |
| `whip_probe` | Doppelhiebe im Peitschentest: wie dicht liegen zwei Auslöser |
| `live_probe` | Live-Bewegung gegen aufgezeichnete, Fahrtwind und Rauschband |
| `pitch_probe` | springende Tonhöhen, einzelne Verursacher abschaltbar, dazu die Lastverteilung Quelle gegen Physik |
| `rpm_glide_probe` | Treppigkeit der Drehzahl beim Reglerzug |
| `tap_probe` | hört ein Abgriffpunkt wirklich von SEINEM Ort |
| `tapfeed_probe` | womit ein Abgriffpunkt gespeist wird (Boden, Wände) |
| `chain_probe` | Hallkette, Stereo-Weitergabe, Wand-Rückwege, Kosten je Bauart |
| `reverb_probe` | Impulsantwort und Rechenzeit der vier Hallbauarten |
| `scope_play_probe` | Play-Knopf am Scope |
| `panel_shot` | Layout-Bilder des Bewegungs-Panels |
| `reverb_shot` | Layout-Bild des Hall-Panels |
| `field_shot` | Feldanzeige, Randmarke, Ketten, Spiegelfronten |
| `tapfield_shot` | Abgriffpunkte in der Feldanzeige |
| `editor_shot` | Bilder von Editor, Hall- und Schwarm-Panel |

Die restlichen zwei, `Tests/reverse_probe.cpp` (zeitverkehrt gehörter Zweig im
Überschall) und `Tests/reverse_level_probe.cpp`, stehen gar nicht in CMake -
sie brauchen nur Löser und Trajektorie und werden direkt übersetzt, die Zeile
dafür steht jeweils im Dateikopf.

**Wichtig:** dieses Projekt baut warnungsfrei (volle JUCE-Warnschärfe:
`-Wall -Wextra -Wshadow-all -Wconversion -Wsign-conversion -Wfloat-equal
-Wcast-align -Wshorten-64-to-32`). Neue Warnungen sind ernst zu nehmen, nicht
zu ignorieren - bewusste Ausnahmen (z.B. `-Wfloat-equal` bei absichtlichen
Identitätsvergleichen) werden lokal per
`JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE`/`..._END` um genau die eine Zeile
gelegt und im Kommentar begründet, nicht projektweit abgeschaltet.

Prüfen lässt sich das am ganzen Bau, nicht am inkrementellen: eine Warnung in
einer Datei, die make gerade nicht anfasst, taucht im Log nicht auf.

## Stand 2026-08-31 (v0.4.0: Hall an acht Abgriffpunkten, Motor an Luft und Gas)

### Ein Abgriffpunkt ist ein Ohr ohne Kopf

Der Hall hängt nicht am Ausgang, sondern an Punkten im Feld. Ein Abgriffpunkt
ist ein Empfangspunkt wie ein Ohr, nur ohne zweites: derselbe
`PropagationPath`, dieselbe Laufzeit, derselbe Abstandsverlust, derselbe
Doppler - nur dass am Ende kein Trommelfell steht, sondern ein Hall. Acht davon
gibt es, im Feld sichtbar und mit der Maus ziehbar.

Beliefert wird ein Punkt über dieselben Flächen wie ein Ohr: Direktweg, Boden
und beide Wände, erste Ordnung. Eine Talwand hört eine tieffliegende Maschine
zu einem guten Teil über den Boden, und ohne diese Wege hinge der Hall an einer
Quelle, die es so nicht gibt. Zweite Ordnung bleibt außen vor - sie kostet noch
einmal dasselbe und trägt an einem Punkt am wenigsten, dessen Signal ohnehin
durch einen Hall läuft.

Der Preis ist ein Löser je Punkt statt zweier. Zum Vergleich kosten Direktschall
und Boden zusammen schon vier, weil jeder von beiden für beide Ohren läuft; acht
Punkte liegen damit in der Größenordnung dessen, was die zweite Reflexionsordnung
ohnehin verbraucht. Gemessen (`tapfeed_probe`, Budget 10667 µs je Block): acht
Punkte mit Boden und zwei Wänden kosten 1642 µs im Mittel.

Der Hall selbst läuft **nicht** noch einmal durch die Physik. Das ist der
Unterschied zwischen bezahlbar und nicht bezahlbar: eine echte Rückausbreitung
bräuchte je Punkt einen zweiten Signalpuffer und ein zweites Pfadpaar. Der
Rückweg zum Hörer ist stattdessen ein Vorlauf, sein Panorama kommt aus dem Ort
des Punktes gegenüber dem Kopf - dieselbe Regel und derselbe Regler wie beim
Ohr-Panorama.

Rendertechnisch sind die Punkte zusätzliche **Kanäle** neben den zwei
Ohrkanälen, keine eigenen Puffer. Deshalb laufen sie beim Geometrie-Crossfade
von selbst mit.

### Vier Bauarten, alle gleichzeitig bereit

`Source/Reverb/` ist JUCE-frei wie die Physik, und aus demselben Grund: der Hall
lässt sich damit offline messen und anhören, ohne dass ein Plugin gebaut werden
muss (`reverb_check` als Test, `reverb_probe` als Messwerkzeug).

Die geteilten Bausteine liegen in `ReverbParts.h` - Verzögerungsleitung,
Dämpfungsfilter, Allpass. Ein neuer Typ beschreibt damit nur noch seine
Verschaltung und bringt nicht wieder einen eigenen Ringpuffer mit. Darauf sitzen
vier Bauarten hinter der Schnittstelle `ReverbUnit`:

| Bauart | was sie ist |
|---|---|
| Diffusor | Allpass-Ketten, dichte Verwischung ohne eigene Färbung |
| Schroeder | Kammfiltersätze, die klassische Bauform |
| FDN | Rückkopplungsnetz mit orthogonaler Mischmatrix |
| Draussen | eine Fläche im Gelände, abgetastet und untereinander verkoppelt |

"Draussen" ist die, die es sonst nirgends gibt. Ein Abgriffpunkt steht für eine
Stelle im Gelände - eine Bergflanke, eine Hauswand, einen Waldrand. Was von dort
zurückkommt, ist EIN Rückwurf, verschmiert über die Ausdehnung der Fläche: ihre
Mitte antwortet zuerst, ihre Ränder später. Jede Verzögerung hat dafür eine
eigene Leitung, damit sie sich gegenseitig speisen können, und die Verkopplung
ist eine zyklisch verschobene Householder-Matrix. Deren Eigenwerte liegen alle
auf dem Einheitskreis - sie erhält die Energie und lässt alle Moden am Leben,
während die naheliegendere Form "Durchschnitt aller außer mir selbst" nach
wenigen Umläufen zu einem einzelnen Kammfilter zusammenfällt.

Vor jeder Bauart stehen die frühen Einzelechos (`EarlyReflections`, eine
getappte Leitung). Sie sind der Grund, warum ein Knall in einem Tal wuchtig
klingt und in einem Hallgerät dünn: ein Nachhallnetz verteilt die Energie eines
Impulses auf tausende winzige Echos, eine reale Talflanke wirft EIN Echo zurück,
das fast so laut ist wie das Original.

Alle vier liegen gleichzeitig bereit. Das kostet RAM, erlaubt aber einen
Typwechsel im Audiothread ohne Allokation - und ohne den wäre der Typ nicht
automatisierbar.

### Wie viel Speicher, entscheidet der Raum

Der Raumregler geht bis 2000 m, leicht logarithmisch. Die Leitungen sind aber
nicht auf den größten einstellbaren Raum bemessen, sondern auf den wirklich
gebrauchten: `reverbparts::capacityFor` verdoppelt von 25 m aus, bis es passt.

Gemessen (acht Punkte, 48 kHz, alle Bauarten bereit): 25 m kosten 6,8 MB je
Punkt, 200 m 16,4 MB, 2000 m 104,6 MB - also 837 MB, wenn wirklich alle acht
Punkte ganz aufgedreht werden.

Verlangt der Regler mehr, als die Puffer tragen, wird der Raum geklemmt und der
Mehrbedarf in `roomShortfall` gemeldet. Das Nachbemessen allokiert und gehört
deshalb in den Nachrichtenthread (`growTapCapacityIfNeeded`). Ein geladener
Zustand mit großen Räumen setzt die Bemessung vor dem `prepare()`, damit er
sofort richtig anfängt statt sich hochzuarbeiten - und hart, denn nur so gibt
ein Zustand mit kleinen Räumen den Speicher wieder her.

### Ketten: ein Punkt geht in einen späteren hinein

`Params::TapPart::chain` schickt einen Abgriffpunkt statt auf die Ohren in den
Eingang eines anderen. Wer ein Ziel hat, geht dorthin; wer Ziel ist, hört nicht
mehr das Feld, sondern nur noch seinen Vorgänger - seine Lage im Feld spielt
dann keine Rolle mehr, und im Feld steht er auch nicht mehr als eigene Marke.
Der Geber trägt die Kette in seiner Beschriftung: `1›2`, über mehrere Stufen
`3›4›5`.

Nur nach hinten, also immer in einen Punkt mit höherer Nummer. Damit ist die
Reihenfolge, in der die Punkte gerechnet werden, zugleich die Reihenfolge der
Kette, und ein Kreis kann gar nicht erst entstehen. Mehrere Ketten nebeneinander
gehen (1 in 3 und 2 in 4), jedes Ziel hat dafür einen eigenen Eingangspuffer.

Weitergegeben wird **stereo**. Alle vier Bauarten sind innen ohnehin zweikanalig
- der Diffusor hat zwei Allpass-Ketten, Schroeder zwei Kammfiltersätze, das
FDN-Netz zwei senkrecht zueinander stehende Vorzeichenmuster, bei Draussen
sitzen die Leitungen links und rechts im Panorama. Eine Mono-Summe wäre hier ein
Verlust: die beiden Seiten einer Bauart sind absichtlich unkorreliert und
löschen sich in der Summe stellenweise aus. `ReverbUnit::processStereo()` ist
deshalb die einzige Fassung, die eine Bauart umsetzt; Mono ist ihr Sonderfall
mit zweimal demselben Zeiger.

In der Kette entfällt der Vorlauf: er bildet den Weg vom Punkt zurück zum Hörer
ab, und ein Punkt in der Kette hat keinen eigenen Ort mehr - sein Weg steckt im
Vorgänger.

Gemessen (`chain_probe`, Direktschall stumm): die Breite der ersten Stufe spreizt
die Seite und lässt die Mitte stehen, sie wirkt also nur bei Stereo-Weitergabe.
Schmal 0 ergibt L/R-Korrelation +0,123, breit 2 ergibt +0,320.

### Was sich sonst am Hall bewegt

**Wandern.** Der Regler "Bewegung" lässt die Leseköpfe um ihre Ruhelage
wandern, geführt von weißem Rauschen durch zwei Ein-Pole und zwischen zwei
Samples linear interpoliert. Gebaut in `reverbparts::DelayLine` selbst, nicht in
den Bauarten - sie benutzen alle dieselbe Leitung, also wandern alle vier mit
einem Schlag. Jede Leitung hat ihren eigenen Takt (0,19 bis 0,56 Hz) und ihren
eigenen Zufall: liefen sie im Gleichtakt, wanderte nicht der Raum, sondern die
Tonhöhe. Der Ausschlag ist anteilig zur Leitungslänge (zwei Prozent bei vollem
Regler) und wird aus der aktuellen Länge gerechnet - dreht jemand am Raum,
bleibt er von allein passend. Kosten: ein halber bis ein Prozentpunkt
Echtzeitbudget je Punktepaar.

**Wände.** Was ein Abgriffpunkt hört, läuft über die Wände; sein Hall auf dem
Rückweg zum Hörer ebenso. Der Rückweg läuft nicht noch einmal durch die Physik -
der Hall ist kein bewegter Punkt mehr, sein Doppler steckt schon im Eingang. Es
bleibt, was eine Spiegelung an einer ruhenden Fläche ausmacht, und genau das
rechnet der `TapBus` je Wand: Umweg gegenüber dem geraden Weg, Abstandsverlust,
Höhendämpfung der Wand und die Richtung, aus der das Spiegelbild kommt. Liegen
Punkt und Hörer auf verschiedenen Seiten, fällt der Rückweg weg. Der Boden
bleibt außen vor - ein zweiter Rückweg kostet noch einmal dasselbe.

**Direktschall und Bypass.** "Direkt" regelt, was ohne Abgriffpunkte
herauskäme, gilt fürs ganze Plugin und schließt den Boden ein. Der Bypass in der
Kopfzeile ist kein Pegelregler, sondern schaltet die Wege ab, bevor sie gerechnet
werden; er stellt den Direktschall dabei auf 0 dB und danach zurück, damit ein
A/B-Vergleich denselben Pegel hat.

**Im Feld.** Jeder Punkt zeigt seine Raumgröße als Kreis im Feldmaßstab.
Wellenfronten von Spiegelquellen enden an ihrer Wand - hinter der Wand ist
akustisch nichts, dort gehört auch nichts ins Bild.

### Der Motor hängt an Luft und Gas, nicht nur am Regler

**An/Aus in der Kopfzeile.** Abgeschaltet verstummen Teiltöne, Motorband und die
gewählte Betriebsart. Stehen bleibt, was die Luft an der fahrenden Quelle macht:
der Fahrtwind und die geschwindigkeitsabhängigen Geräusche. Die Quelle fährt
weiter, sie treibt nur nichts mehr an. Geblendet, nicht geschaltet - ein Sprung
im Pegel wäre ein Knacken.

**Fahrtwind** ist ein eigener Regler in dB, ±36 dB wie die Flächen. Er lebt in
jeder Betriebsart außer "Frei" - dort würde er alte Snapshots hörbar verändern.
Ist der Motor aus, fällt dieser Grund weg und er wird auch dort eingeblendet.

**Rausch v** koppelt das Rauschband ans Tempo statt allein an die Drehzahl. Ein
Leerlaufmotor, der mit 300 km/h vorbeigezogen wird, rauscht sonst wie im Stand.
Bei 100 % wächst es mit dem Quadrat der Geschwindigkeit, mit demselben Bezug wie
der Fahrtwind (120 m/s). Der Tempoanteil ist Luft am fahrenden Körper und kein
Motor: er bleibt stehen, wenn der Motor aus ist.

**Gas aus a** zieht die Drehzahl mit der Längsbeschleunigung der Quelle mit -
nur der Anteil in Fahrtrichtung, eine Kurve bei gleichbleibendem Tempo ist kein
Gasgeben. Bezug ist ein g: wer damit anzieht, bekommt bei 100 % die doppelte
Drehzahl, wer genauso stark bremst, die halbe. Geglättet wird der Faktor und
nicht die Beschleunigung, deshalb fällt die Drehzahl mit derselben
Zeitkonstante zurück, mit der sie gestiegen ist.

**Gas-Trägheit** ist die Zeitkonstante dazu, 20 ms bis 3 s. Sie IST der
Unterschied zwischen elektrisch (folgt sofort) und Verbrenner (hängt nach, fällt
langsam ab) - ein Regler statt zweier Betriebsarten. Sie ist zugleich das
Fenster, über das die Beschleunigung gemessen wird: kurz eingestellt kommt auch
das Zittern der Bewegung als Gas an.

Die Beschleunigung selbst entsteht als **Abstand zweier verschieden träger
Glätter desselben Tempos** (schneller Zweig zweipolig, langsamer bei vierfacher
Zeitkonstante), ohne Division durch die Tickdauer. Eine Ableitung Tick für Tick
teilte durch eine Millisekunde - ein Zentimeter Zappeln steht so als 10 m/s² da,
und eine aufgezeichnete Bahn hat an jedem Stützpunkt einen Knick. Der schnelle
Zweig sieht außerdem nie feiner hin als 40 ms; darunter liegt keine Bewegung
mehr, sondern nur das Raster, in dem sie hereinkommt.

**Die Drehzahl gleitet.** Sie läuft dem Regler je Sample als Ein-Pol hinterher
(50 ms), und zwar im Logarithmus, damit das Glissando unten wie oben gleich
schnell ist. Gemessen (`rpm_glide_probe`, 6000→24000 RPM in 0,5 s): die Stufe
zwischen zwei Rasterwerten des Reglers wäre 76,8 Cent, gehört werden 38,8 Cent
Maximum bei 23,1 Cent mittlerer Rampe je Periode.

### Live-Bewegung ohne Eingaberaster

Die Oberfläche meldet eine neue Position nur im Bildtakt. In der Position fällt
das kaum auf, in ihrer zweiten Ableitung steht bei jeder Stufe eine Spitze, und
die fährt als Gas in die Drehzahl. Jede Zieländerung wird deshalb über die Zeit
ausgerollt, die seit der vorigen vergangen ist: aus der Treppe wird ein Zug mit
gleichmäßigem Tempo, zum Preis einer Rasterperiode Verzug (rund 17 ms). Halt und
Schnitt setzen ohne Ausrollen.

**Der Nachlauf** fährt schnurgerade auf den Loslasspunkt zu, mit dem Tempo vom
Loslassen; nahe am Punkt wird dieses Tempo auf Abstand/`coastTau` gedrosselt und
läuft stetig gegen null. Dieser Fahrt folgt die Quelle kritisch gedämpft, gegen
die Rampengeschwindigkeit gedämpft - damit bleibt kein Rückstand stehen, den
danach noch jemand aufholen müsste, und es gibt keine zweite Anfahrt. Beim Start
liegen Quelle und Rampe aufeinander und haben dasselbe Tempo: die Beschleunigung
beginnt bei null, die Bewegung geht ohne Knick aus dem Ziehen hervor.

Gemessen (`coast_probe`, sieben Fälle): kein Wiederanstieg des Tempos über 5 %,
alle 100 % am Punkt, die Naht zum Ziehen knickfrei (-1,8 bis +1,5 %).

**Jit glatt** (`srcJitterSmooth`, Default aus) schickt den Wackler wahlweise
durch die Bewegungsglättung: aus behält er seinen vollen Ausschlag und läuft an
Glättung und Tempo-Deckel vorbei, an teilt er sich Verfahren, Zeitkonstante und
Deckel mit Maus, Vorbeiflug und Wiedergabe.

### Knall-Sperre und Knall-Ansicht

Die **Sperrzeit** (0 bis 500 ms, Vorgabe 0) steht in der Knall-Gruppe des
Feld-Panels: was in dieser Zeit noch einmal auslösen würde, fällt weg. Ihr
Zustand liegt nicht im einzelnen Hörweg - die zwei Fronten eines
Wackel-Durchgangs kommen über vier verschiedene Pfadobjekte innerhalb einer
Millisekunde, von denen jedes seine eigene Sperre hätte. Ganz global darf sie
auch nicht sein: der Knall trifft das zweite Ohr eine halbe Millisekunde später,
und das ist die Ortung. Also eine Sperre je Ohr, plus eine für die
Abgriffpunkte. Gemessen (`whip_probe`, `peitschentest`): ohne Sperre Spitze
0,835, mit 60 ms 0,425 - der lauteste "Schlag" ist das Zusammentreffen zweier
Fronten.

Die **Knall-Ansicht des Scopes** führt in jeder Zoomstufe nach. Der
Hüllkurven-Detektor läuft durch und bekommt nur die wirklich neuen Samples,
statt in jedem Rohfenster bei null zu starten. Ob ein Fund "derselbe Knall" ist,
entscheidet eine Refraktärzeit von 3 ms - eine Eigenschaft der Flanke, nicht der
Zeitbasis. Ein wartender Einsatz bleibt stehen, bis er gezeigt ist. Das
Rohfenster deckt mindestens eine Achtelsekunde ab. Gemessen
(`scope_boom_probe`, elf Knalle im Abstand von 500 ms): 11 von 11 bei 128 bis
8192 Samples Zeitbasis, 9 bei 32768.

### Die Statuszeile sagt, was los ist

Die Abriss-Meldung nennt die Sache beim Namen statt Zahlen aufzureihen: "Hörweg
abgerissen 2x - bei vollem Pegel, kann knacken", darunter "halb ausgeklungen"
und "war ausgeklungen, unhörbar". Gezeigt wird, wie viele neu dazugekommen sind,
nicht der Zählerstand.

Die CPU-Zeile zeigt neben dem Mittelwert den teuersten EINZELNEN Block und die
Zahl der Blöcke über Budget: "CPU 8 %  Spitze 142 %  über Budget: 3". Genau
diese Einzelfälle sind die Aussetzer, und genau sie verschwinden in einem
Ein-Pol über zehn Blöcke. Die Spitze wird drei Sekunden gehalten, der
Überlaufzähler läuft bis zum Abschalten des Plugins.

### Was ein Test öffnet, gehört ins Repo

`repo_check` liest die Quellen der ctest-Tests, sammelt jeden Pfad, den sie über
`DOPPLERFELD_SOURCE_DIR` öffnen, und vergleicht ihn mit dem, was git führt. Er
braucht keinen Build und läuft in Sekunden - der Befund kommt damit nach vorne,
vor den Push. Die Dateien selbst liegen in `Tests/fixtures`, ausdrücklich nicht
in `presets/`, das der Release-Schritt ins Nutzer-Zip kopiert.

Dazu zwei weitere ctest-Tests: `reverb_check` (Hallbauarten offline) und
`scope_boom_probe` (Knall-Ansicht, mit Unter- und Obergrenze).

## Stand 2026-08-28 (v0.3.0: Zustandsstreifen, Preset ohne Erbe, Farbwelt, Startzustand)

### Laden und Sichern ohne Dateidialog

`Source/UI/PresetBar.cpp` ist eine Zeile über dem Feld: Liste, zwei Pfeile,
`Sichern`, `Neu...`, `Ordner...`. Sie kennt keine Parameter, nur Dateien - der
Editor hängt sich über `onLoad`/`onSave`/`onCheck` ein und reicht die Blöcke
an `setStateInformation()`/`getStateInformation()` weiter. Das Format ist
dasselbe wie bei "Save/Load State" der Standalone-App, die vorhandenen Presets
bleiben also unverändert brauchbar. Der Ordner wird einmal gewählt und in
derselben `.settings`-Datei gemerkt wie die übrigen Merkposten der Oberfläche
(`WelcomeOverlay`, `FieldComponent`).

Der Dateidialog des Systems braucht beim ersten Öffnen über eine Sekunde -
daran lässt sich von hier aus nichts ändern, wohl aber daran, dass man ihn
überhaupt braucht.

Dateinamen sind Benutzertext und werden nicht übersetzt. Damit die
Sprachprüfung in `load_check` nicht über deutsche Namen im EN-Betrieb
stolpert, tragen Liste und Statuszeile die Kennung
`PresetBar::userTextComponentId`.

### Ein Preset erbt nichts mehr vom vorigen

`apvts.replaceState()` setzt nur, was im Baum steht; jeder Parameter ohne
Eintrag behält seinen aktuellen Wert. Ein Preset aus einer älteren Fassung
übernahm damit alles, was seither dazugekommen ist, vom zuletzt geladenen
Preset - und klang beim zweiten Laden anders als beim ersten. Von 144 in den
Presets vorkommenden Parametern fehlten einzelnen bis zu 50.

`setStateInformation()` ergänzt jetzt **vor** dem Übernehmen jeden fehlenden
Parameter mit seinem Grundwert. So steht der vollständige Satz auch in
`apvts.state`, und wer das Preset danach sichert, schreibt es vollständig
zurück.

### Was sonst noch im Zustand steckt

Neben den Parametern hängen als Properties am Wurzelknoten: die
Bewegungsaufzeichnung (`motionFrames`/`motionRateHz`/`motionWasPlaying`), die
Quellwahl (`sourceKind`), der Sample-Pfad (`samplePath`, relativ zu
`presetsRootDirectory()`, wenn er darunter liegt), der Schalter "Motor bei
Griff" (`motorGateEnabled`) und **neu** die Klappzustände der Panelspalte
(`panelsOpen`, Bitmaske). Ein Preset für den Vorbeiflug zeigt damit die
Bewegung offen, eines für den Motor den Motor; ein Preset ohne die Property
klappt alles zu.

### Startzustand beim allerersten Öffnen

Beim allerersten Start auf einem Rechner stand bisher der Grundzustand da -
Motor auf Standardwerten, nichts in Bewegung. Jetzt spielt
`DopplerfeldProcessor::loadStartPresetOnFirstRun()` am Ende des Konstruktors
einmalig ein mitgeliefertes Preset ein (`600kmh-Drone@600m²`).

Zwei Dinge machen das unauffällig:

- Der Merkposten (`startPresetLoaded`) liegt in derselben `.settings`-Datei
  wie `welcomeSeen`. Steht er, passiert nichts mehr.
- Der Aufruf sitzt im **Konstruktor**, also vor dem Zeitpunkt, an dem die
  Standalone-App ihren gespeicherten Zustand einspielt. Ab dem zweiten Start
  überschreibt der gespeicherte Zustand das Preset also von selbst - "danach
  wie bisher: was zuletzt aufgerufen war" braucht keinen eigenen Code.

Das Preset liegt als Kopie **in der Programmdatei**, nicht als Pfad: auf einem
frischen Rechner gibt es noch keinen Preset-Ordner. `CMakeLists.txt` kopiert
die eine Quelldatei aus `presets/` zur Bauzeit unter einen Namen ohne
Sonderzeichen und gibt sie an `juce_add_binary_data` - `@` und `²` haben in
einem C++-Bezeichner nichts zu suchen, und eine zweite gepflegte Kopie im
Quellbaum wäre die schlechtere Antwort darauf.

### Farbwelt und Reglermaße an einer Stelle

`Source/UI/Theme.h` hält Grundflächen, Linien, Textfarben, die sieben
Bereichsfarben der Panelspalte und die Zellenmaße der Regler
(`Theme::knobWidth`/`knobHeight`). Die Akzentfarbe färbt nie die Fläche
selbst, sondern liegt in sehr geringer Deckkraft über dem Panelgrund; Rahmen
bleiben kontrastarm, Radien klein.

### Überschall-Frontlinien im Feld

Die Feldanzeige zeichnet bei Überschall die Stoßfront nicht als geschätzte
Kegelachse, sondern aus den Wellenfronten selbst: Tangenten an die
gezeichneten Kreise, dazwischen Bogenstücke statt Zacken, nach Höhe gestaffelt
(`FieldComponent`).

### Zerlegter Parameterdurchlauf

`DopplerfeldProcessor::applyParameters()` war eine Methode über mehrere
hundert Zeilen und ist in thematische Methoden zerlegt (Quelle, Bewegung,
Feld/Physik, Reflexionen, Schwarm, Ausgang). Reine Umstellung, kein anderer
Klang - der Aufrufpunkt pro Block bleibt derselbe.

## Stand 2026-08-26 (Z-Anteil gemeinsam, Kopf in der Perspektive, Anfassen ohne Sprung, RPM)

### Ein Z-Anteil für Wackler UND Klon-Streuung

Die Höhe der Klon-Streuung hing an einem festen Bruchteil (0,35) mitten in
`DopplerEngine::cloneOffset()`. Sie hat jetzt denselben Regler wie das Wackeln
der Quelle: `Params::srcJitterZAmount`, ein Parameter, zwei Räder im UI
(Bewegung und Schwarm). Zwei `SliderAttachment` auf denselben Parameter sind
die Gleichschaltung, die @dpa vorgeschlagen hat ("zwei mal den Control scheint
zuviel, aber in beiden ist er wichtig.. vielleicht gegenseitig
ferngesteuert/gleich geschaltet?") - niemand muss sie synchron halten.

1 heißt jetzt in beiden Fällen dasselbe: die Höhe zählt so weit wie die Ebene.
Für die Klone ist das mehr als die alten 0,35; der Regler steht auf 1, weil
das schon vorher der Default des Wacklers war.

**Neu geprüft:** `Tests/swarm_probe.cpp` gibt die Höhenspanne des Schwarms aus.
Streuung 10 m, Wackler aus: bei Z-Anteil 0 liegt die Spanne bei **0,000 m**,
bei Z-Anteil 1 bei **6,479 m** - der größte Versatz in der Ebene bleibt in
beiden Fällen 4,472 m, die Streuung selbst ändert sich also nicht.

### Der Hörer liegt in der Perspektive flach in seiner Ohrhöhe

Das Kopfsymbol stand in der Perspektive bildschirmparallel auf und war nur
gedreht - es zeigte weder die Höhe noch die Blickrichtung im Raum. Jetzt ist
es dieselbe Zeichnung wie in der Draufsicht, aber Punkt für Punkt durch
`project()` gelegt, flach in der Ebene z = Ohrhöhe (@dpa: "ruhig als die
gleiche 2D-Darstellung, aber perspektivisch verzerrt.. quasi auf einer
xy-fläche in der z-Höhe. senkrechten strich zu z=0 (wie bei m)"). Aus dem
Kopfkreis wird von selbst die liegende Ellipse; Lotlinie und Fußpunkt sind
jetzt dieselben wie bei M.

Dafür kennt `HeadSymbol` neben `draw()` ein `drawMapped()`, das jeden Punkt
der Kopfebene durch eine übergebene Abbildung schickt. `draw()` ist derselbe
Aufruf mit "drehen und skalieren" als Abbildung - eine zweite Geometrie gäbe
es nur, damit beide auseinanderlaufen können. Der Kopfkreis ist deshalb ein
48-Eck: das Bild eines Kreises ist unter einer perspektivischen Abbildung
keine achsenparallele Ellipse mehr, und `juce::Graphics` kennt nur die.

Die Größe bleibt eine Bildgröße und wird nur in Meter zurückgerechnet, damit
die Verzerrung eine Ebene hat, in der sie stattfinden kann. Ein maßstäblicher
Kopf wäre bei den üblichen Feldgrößen ein Punkt. Sie folgt jetzt derselben
Regel wie M (`perspectiveSourceScale`), im selben Verhältnis, in dem der Kopf
auch in der Draufsicht größer ist als M - vorher war ausgerechnet das größte
Symbol der Draufsicht in der Perspektive das kleinste (@dpa: "L in perspektive
zu klein"). Nach unten begrenzt ihn `headRadiusPx`, also die Größe, die er in
der Draufsicht hat.

**Neu geprüft:** `Tests/field_shot.cpp` rendert die Perspektive jetzt mit
Blick von der Kamera weg, quer und zur Kamera hin sowie mit erhöhtem Hörer
(`build-ui/field_persp_listener_*.png`, headless, kein Fenster). Was die
wörtliche Umsetzung mitbringt: mit der Nase zur Kamera oder von ihr weg ist
die Blickrichtung stark verkürzt und schwerer abzulesen als seitlich - das
ist die ehrliche Perspektive einer flach liegenden Zeichnung.

Neu dazu `Tests/editor_shot.cpp`: nimmt das komplette Editor-Fenster und das
Schwarm-Panel headless auf (`build-ui/editor_full.png`,
`build-ui/panel_swarm.png`) - dafür gab es bisher nur den Weg über ein echtes
Fenster.

### Anfassen bewegt M nicht mehr

Ein Klick auf M setzte seine Position auf den Mauszeiger. Weil der Fangradius
großzügig ist (28 px, plus Wackeln), sprang M dabei fast immer - hörbar.
Jetzt merkt sich `mouseDown()` den Versatz zwischen Zeiger und Ruhelage
(`grabOffsetPx`) und meldet selbst nichts; jeder folgende Ziehschritt rechnet
ab der Ruhelage plus Mausversatz (@dpa: "es soll sich durchs click 0 bewegen.
Erst dragging zählt dann von der Klickposition aus.. ohne sprung").

Zwei Punkte gehören dazu, nicht einer (`GrabAnchor`): geprüft wird am
gezeichneten, möglicherweise gewackelten Punkt - dagegen hat `dragTargetAt()`
gefangen -, gerechnet wird ab der jitterfreien Ruhelage. Die Randmarken
bleiben ausgenommen: dort ist der Sprung zur geklickten Stelle der Zweck.

**Neu geprüft:** `Tests/grab_probe.cpp` simuliert Mausereignisse auf der
`FieldComponent` und misst, was nach außen gemeldet wird. Klick auf M, 2 m
neben seiner Ruhelage: **0 Meldungen**. Zug um 70 px (10 m): gemeldet wird
0,60059 der Feldbreite, also die Ruhelage plus genau diese 10 m, nicht die
Mausposition.

### Play-Knopf am Scope

Der sichtbare Scope-Ausschnitt lässt sich anhören: "Play" ist ein Umschalter,
Einschalten spielt das Bild einmal von vorn bis hinten und geht danach auf
null; solange er an bleibt, startet ein Klick ins Scope die Wiedergabe an der
geklickten Stelle bis zum rechten Rand (@dpa: "wieder bis hinten").

Wörtlich heißt "auf null", dass die Wiedergabe den Ausgang **ersetzt**,
solange der Knopf an ist - das Dopplersignal ist dann stumm, sonst wäre
zwischen zwei Abspielvorgängen nichts still. Der Weg dorthin sind zwei
getrennte Signale, wie sonst auch hier (siehe Record/Play): ein Level-Flag für
den Ein/Aus-Zustand und ein diskretes Anfrage-Flag für "genau diesen Puffer
jetzt". Der Puffer wird im Message-Thread gefüllt, bevor das Flag ihn
ankündigt; die Kapazität steht seit `prepareToPlay()` fest
(`scopeMaxDisplaySeconds`), der Audiothread allokiert nichts.

Zwei Rampen, weil sie zwei verschiedene Klicks verhindern: eine blendet
zwischen Doppler und Wiedergabe (8 ms, Ein/Aus des Knopfs), die andere gehört
einem einzelnen Abspielvorgang (3 ms, an einer angeklickten Stelle steht eine
beliebige Amplitude). Ein Klick mitten in eine laufende Wiedergabe blendet die
alte erst aus und übernimmt dann - kein Überblenden zweier verschiedener
Ausschnitte.

`renderScopePlayback()` läuft **vor** `applyOutputStage()`: Gain, Begrenzer,
Pegelanzeige und Scope-Ringpuffer behandeln die Wiedergabe damit wie normales
Ausgangssignal.

**Neu geprüft:** `Tests/scope_play_probe.cpp` misst am Ausgang des Processors.
Bezugspegel (Motor läuft) 0,95117. Play an mit einem 0,1-s-Ausschnitt (links
0,50): während der Wiedergabe **0,50000**, danach **0,00000** bei weiterhin
eingeschaltetem Knopf. Klick auf die Mitte: wieder 0,50000, danach 0,00000.
Zweiter Start mitten hinein: 0,50000, kein Überschlag. Nach dem Ausschalten
steht der Dopplerausgang wieder da.

### RPM-Bereich

`Params::rpm` geht bis 96000 statt 12000 (drei Oktaven, @dpa: "erweitere es um
2-3 Oktaven"). Der Skew bleibt bei 1000, der brauchbare Bereich liegt
weiterhin unten.

## Stand 2026-08-25 mittags-2 (Startknall: Runde, Länge, Regelweg)

Berichtigung einer eigenen Fehlentscheidung von wenigen Stunden zuvor, plus
der eigentliche Grund, warum der Startknall nie nach Knall klang.

### Der Rundenwechsel bekommt seine Sprungmarke zurück

Vormittags hatte ich eingebaut, dass ein Rundenwechsel keine Sprungmarke mehr
setzt. @dpa arbeitet mit eingeschalteter Dauerschleife - damit blieb es beim
einen Knall des allerersten Starts. Nachgestellt mit seinen Preset-Werten
(`load_check`, "Preset Startknall"): in 20 Sekunden ein einziger Knall.

Seine Klage vom Vormittag galt dem **Sprung**, also der Ortsveränderung. Die
knallt auch weiterhin nicht - sie läuft über den Schnitt (`CutState`). Was
danach kommt, bestimmt die Startvariante, und "Knall-Start" heißt Knall, bei
jedem Losfliegen. Wer keinen will, wählt "Kontinuierlich"; dort wird gar keine
Marke gesetzt.

**Neu geprüft:** "Rundenwechsel lautlos" - der Startknall-Regler am Anschlag
ändert bei "Kontinuierlich" das Signal um exakt **0.00000**, bei "Knall-Start"
um 1.22228.

### Warum es eine Beule war und kein Knall

Die Länge des Startknalls hing an `nWaveSize`, der Körpergröße für den
Überschallknall - in seinem Preset 15 m. Das sind 87 ms Wellendauer, und damit
sitzt die Energie um **11 Hz**: Infraschall. Man spürt eine Druckbeule und
hört keinen Knall. Mehr Pegel machte es nur spürbarer, und der
Ausgangsbegrenzer reagierte auf eine Auslenkung, die niemand hört - vom
Reglerweg zwischen 1 und 4 kamen nur 1,6× an statt 4×.

Der Startknall bildet aber keinen Körper ab, sondern eine **Beschleunigung**.
Er hat jetzt eine eigene Länge:

- Neuer Regler **"Knall-Länge"** (`jumpBoomSize`), 0,1 bis 60 m, Default
  1,5 m ≈ 9 ms ≈ 115 Hz.
- `triggerNWave()` nimmt die Länge optional von außen (`sizeOverride`), wie
  schon die Entfernung (`radiusOverride`).
- **Ohne Größenkopplung**: der Amplitudenfaktor L^(3/4) kommt aus der
  Körperlänge und gilt hier nicht - sonst machte der Längenregler den Knall
  nebenbei leiser.

Dazu der Regelweg von "Startknall": 0..1 → 0..4, Default 1. Der Deckel bei 1
saß auch noch in `PropagationPath::setJumpBoom()` und machte den halben
Regelweg wirkungslos.

**Neu geprüft:** "Startknall-Wucht" (ohne Begrenzer, mit seinen Preset-Werten):
Regler 1 → 0,6262, Regler 4 → 2,5048, also exakt 4,00×.

### Was in seinem Preset bleibt

Mit Begrenzer steht der Knall schon bei Regler 1 auf 1,0 am Anschlag. Nicht
der Knall ist zu leise, der Ausgang ist zu voll - mehr Wucht kommt dort über
weniger Umfeld, nicht über mehr Knall.

Und ein drittes Rätsel war keines: die Strecke ist zweimal der Anflug (746 m),
bei 47,6 m/s also 15,7 s je Runde. In 20 Sekunden sind das **zwei** Starts,
und genau zwei Knälle stehen da - bei 1,1 s und 16,8 s, jeweils Startzeit plus
1,1 s Laufzeit vom 373 m entfernten Startpunkt. Die erste Fassung der Prüfung
erwartete drei und meldete deshalb einen Fehler, den es nicht gab.

### Gegenprobe

Alle 51 Szenarien grün. Das Knall-Start-Szenario behält seine Spitze (0,314 →
0,306), verliert aber Energie (RMS im Ankunftsfenster 0,100 → 0,032) und wird
steiler (steilster Anstieg 17,1 → 32,2 dB) - genau der Unterschied zwischen
87 ms Beule und 9 ms Schlag.

## Stand 2026-08-25 nachmittags (Startknall, Scope-Perioden, Slew, Meereshöhe)

### Der Startknall fehlte bei Überschall vollständig

@dpa: "er ist wieder nicht hören. Ist der Startknall etwa im 'Tempo' des
Objekts?? ... Ich will einen Knall unabhängig vom M speed." Nachgemessen als
Differenz zweier sample-genau gleicher Läufe (neuer `load_check`-Abschnitt
"Startknall je Tempo"): Mach 0,6 hob die Spitze um Faktor 5,17, Mach 1,5 und
Mach 3,0 um **exakt 1,00** - der Knall war nicht leise, sondern bitgleich
abwesend.

Drei Ursachen:

- **Ausgelöst wurde über die Emissionszeit eines Zweigs.** Das setzt voraus,
  dass ein Zweig die Bahn durchgehend verfolgt. Bei Überschall werden Zweige
  neu GEBOREN, ihre Emissionszeit beginnt jenseits der Marke und läuft nie
  darüber. Jetzt steht die **Ankunftszeit** fest, sobald die Marke gesetzt ist
  (`jumpArrivalTime` = Markenzeit + Laufzeit vom Startpunkt über diesen Weg).
- **Die Amplitude wuchs mit der Sprunghöhe** (`min(1, v/c)`). Jetzt steht die
  Lautstärke am Regler und sonst nirgends.
- **Gerechnet wurde mit der aktuellen Entfernung des Zweigs.** Die Welle
  entstand am Startpunkt; `triggerNWave()` nimmt jetzt optional dessen Abstand
  (`radiusOverride`) - denselben Wert, aus dem auch die Ankunftszeit kommt.

Dazu wird der Puls genau **einmal** ausgelöst, nicht je Zweig - bei Überschall
laufen drei gleichzeitig, und drei übereinandergelegte Pulse waren dreimal so
laut wie bei Unterschall.

Danach: 0,258 / 0,244 / 0,244 bei Mach 0,6 / 1,5 / 3,0, Ankunft jeweils bei
t ≈ 1,0 s - der Laufzeit vom Startpunkt.

### Scope-Sync zeigt ganze Wellen

"Die Wellen sind oft 2 geteilt ... der nächste sync soll 2n später sein oder
so?" Ein Oszilloskop mit fester Zeitbasis zeigt fast nie eine ganze Zahl von
Perioden - bei 220 Hz und 20 ms sind es 4,40, die letzte bricht mittendrin ab.

Der Sync misst jetzt nebenbei die Periodenlänge (mittlerer Abstand der
steigenden Nulldurchgänge im ohnehin gefilterten Fenster) und rundet die
gezeichnete Länge auf ein Vielfaches. Gezeichnet wird ab dem Trigger nach
rechts statt um ihn zentriert; die weniger gezeigten Samples werden auf die
volle Breite gestreckt.

Nachgemessen ("Scope ganze Wellen"): mit Rasten 873 Samples = 4,00 Perioden,
Randabweichung 8,3 %; ohne Rasten 960 Samples = 4,40 Perioden, Randabweichung
**167 %**.

### Slew: ein Regler statt zweier

"Ich habe die besten Ergebnisse, wenn ich sie gleich einstelle." `slewAmax`
entfällt; die Beschleunigungsgrenze ist `v_max / accelTimeSeconds` mit
`accelTimeSeconds` = 1 s. Das ist genau seine Einstellung - a_max numerisch
gleich v_max heißt gerechnet v_max / 1 s. Die Sekunde ist zugleich das tau des
Limiters.

### Meereshöhe

"Höhe" stand neben "Source Z" und "Listener Z" - drei Regler für eine Höhe,
einer davon so benannt. Jetzt "Meereshöhe" (EN "Altitude"), "NN" überall
ausgeschrieben.

### Hilfetexte

@dpa zum Startknall-Tooltip: "was es 'nicht ist' ist völlig irrelevant! bitte
texte nicht zuviel in die Helphints!!" Tooltips bleiben ab jetzt bei ein bis
zwei Sätzen: was der Regler tut, in welchem Bereich, und höchstens eine
Bedingung. Begründungen gehören hierher oder in den Code-Kommentar.

### Gegenprobe

Alle 50 Szenarien grün. Gegenüber dem Stand davor: das Knall-Start-Szenario
wird lauter (Ankunftsspitze 0,187 → 0,314), weil der Knall nicht mehr mit
`min(1, v/c)` gedämpft wird - dort lag er bei Mach 0,58 und damit bei 58 %.
Die Beschriftungszahlen sinken um einen Regler (Slew Amax). Der Rest ist
Wanduhr-Streuung.

## Stand 2026-08-25 mittags (Rakete im Infraschall, Bewegung-Panel kompakt)

### Die Energie der Rakete lag unter der Hörschwelle

@dpa: "eine Rakete im Vollantrieb und alles was man hört ist ein kleines
Stoßen mit hohem Zischen (wie bei einem undichten Ventil am Fahrrad mit
3Bar!!)". Nachgemessen (neuer `load_check`-Abschnitt "Raketen-Bänder",
Vollschub, ohne Stöße) lagen **unter 20 Hz**: 38 % am Startplatz, 70 % in
30 m, 90 % in 300 m. Das ist nicht leise, sondern unhörbar - und es steuert
trotzdem voll aus, treibt also den Begrenzer und drückt alles Hörbare weg.

Ein spektraler Schwerpunkt allein hätte das nie gezeigt: er lag tief, und das
sah nach Erfolg aus. Erst die Aufteilung mit einer **eigenen Spalte für alles
unter 20 Hz** macht den Unterschied zwischen "tief" und "weg" sichtbar.

Drei Ursachen, alle aus dem Umbau vom Vortag:

- **Der Bandbreiten-Ausgleich in `place()`** setzt gleiche Energie gleich
  Lautheit. Unterhalb der Hörschwelle stimmt das nicht mehr - dort fügt mehr
  Pegel keine Lautheit hinzu, nur Auslenkung. Ein Tiefpass bei 16 Hz bekam so
  das rund Vierzigfache an Verstärkung. Jetzt mit Untergrenze `audibleFloorHz`
  (30 Hz). Der Regler kommt weiterhin bis an die 4 Hz.
- **Das Tiefband war ein flacher Tiefpass** (Q 0,6) und ließ damit alles
  unterhalb seiner Eckfrequenz durch. Für die Rakete jetzt `rocketLowQ` = 1,1:
  die Energie sammelt sich AN der Eckfrequenz. Die Düse behält `jetLowQ` = 0,6
  - ihr Tiefband ist ein Fundament unter den Mitten und sänge mit Resonanz;
  ihre Vorlagen bleiben bitgleich.
- **Die Vorlagen lagen eine Oktave zu tief** (Vollschub `lowFc` 32 Hz), jetzt
  38 bis 75 Hz. Und **"Fern-Farbe"** stand auf 1,0 Oktave je Verdopplung, in
  300 m also dreieinhalb Oktaven; jetzt 0,25. Bis 3 bleibt der Regler offen.

Danach unter 20 Hz: 11 % am Startplatz, 14 % in 30 m, 27 % in 300 m; der Bass
zwischen 20 und 80 Hz trägt 63 bis 71 %.

**Zur Lautstärke ohne Referenzdistanz** (@dpas Frage "ist der Hörer 30m
entfernt und hört normallaut..? ist das korrekt?"): die Rechnung stimmt. Bei
30 m dämpft 1/R um 29,5 dB, die dünne Luft in 1472 m Höhe um weitere 1,5 dB;
+12 dB Motorpegel und +21,1 dB Ausgang ergeben in Summe +2,0 dB gegenüber der
Quellamplitude. Der eingestellte Quellpegel gilt also bei 1 m - eine
einstellbare Bezugsentfernung ("Pegel gilt in X Metern") gibt es nicht und
wäre der nächste sinnvolle Schritt, wenn das Nachregeln lästig wird.

### Bewegung-Panel

Reglermaße 100 × 79 → 84 × 67, dieselben wie im Motor-Panel. Damit passt die
Jitter-Zeile samt Schalter wieder in EINE Reihe: Tempo-Deckel, Lücke,
Jitter An, Jitter, Jit Tempo, Z-Anteil = 440 Pixel bei 446 verfügbaren. Der
Schalter steht vor den Reglern, die er schaltet - hier wird von links
weggenommen, und wer am Ende steht, bekommt nur den Rest. Panelhöhe 415 → 306.

### Gegenprobe

Alle 48 Szenarien grün. Gegenüber dem Stand davor ändern sich nur
Raketen-Zahlen (Vorlagen-Schwerpunkte, Bänder, Brüllen-RMS 0,155 → 0,163) und
die Wanduhrmessungen. Die Düse ist bitgleich.

## Stand 2026-08-25 vormittags (Wackler-Tempo, Jitter-Schalter, Startknall)

Vier Meldungen aus @dpas Durchgang. Drei waren echte Fehler, die vierte ein
Missverständnis, das ein falscher Name erzeugt hat.

### Der Jitter-Schalter hatte null Pixel Breite

"Bitte ein Schalter hinzufügen: Jitter on/off" - den gab es seit dem 20.08.
Er stand nur am **Ende** einer Zeile in `MotionPanel::resized()`, die bereits
breiter war als das Panel: 128 (Tempo-Deckel) + 20 (Lücke) + 3 × 104
(Jitter-Regler) sind 460 Pixel, verfügbar sind 446. `jmin (120,
sharedRow.getWidth())` ergab null. Mit vier Jitter-Reglern (vor dem 25.08.)
war die Zeile noch 88 Pixel länger, also schon vorher.

Der Jitter hat jetzt eine eigene Zeile, Schalter ganz vorn. Panelhöhe
330 → 415.

**Neu geprüft:** `load_check`-Abschnitt "Bedienelemente". Jeder sichtbar
geschaltete Regler, Schalter und jedes Auswahlfeld muss mindestens 12 × 8
Pixel innerhalb seines Elternteils haben. Geprüft werden die Panels EINZELN
mit genau den Maßen, die ihnen der Editor gibt - über den ganzen Editor ginge
es nicht, dort hängen sie in einem Viewport, der ohne Fenster keine Breite
meldet. Die Panelmaße in `PluginEditor.h` sind dafür öffentlich geworden.

### Das Jitter-Tempo kam nur zur Hälfte an

"'Jit Tempo'=Mach3, gemessenes Tempo:max Mach 1,5" und "jitter=Mach3,
tatsachlich1,5 aber null Knall". Beides stimmt, beides hat dieselbe Ursache.

Der Umbau vom Vortag rechnete **eine feste Frequenz** aus dem ungünstigsten
denkbaren Fall: alle drei Achsenfaktoren gleichzeitig am oberen Anschlag UND
alle drei Kosinus gleichzeitig eins. Der tritt praktisch nie ein. Nachgemessen
kamen 60 bis 73 Prozent der eingestellten Spitze an, 37 bis 39 Prozent im
Effektivwert.

Jetzt wird die Bahn nach **Bogenlänge** durchlaufen:
`omega = v / sqrt(SUM (A_i · g_i · cos phi_i)²)`, jeden Tick neu aus der
Ableitung an der aktuellen Stelle. Zwei Fallstricke, beide behandelt:

- Nahe einem Umkehrpunkt ist die Ableitung fast null, `omega` wird groß, und
  ein Euler-Schritt schießt weit über das Ziel - gemessen Spitzen vom
  Fünffachen. Der Schritt wird deshalb am tatsächlich zurückgelegten Weg
  nachgemessen und heruntergezogen, wenn er länger wäre als `v · dt`.
- Zeitkonstante der Achsendrift und Takt des Neuwürfelns hängen an einer
  groben Bezugsfrequenz aus den Reglerwerten, nicht am momentanen `omega`.

Kein Kreisel: konstant ist nur der BETRAG der Geschwindigkeit, die Richtung
wechselt weiter unregelmäßig. Für den Doppler zählt allein die Komponente
entlang der Sichtlinie, und die schwankt nach wie vor von +v bis −v.

**Neu geprüft:** "Wackler-Tempo" (eingestellt 343 bzw. 1029 m/s → Spitze und
Effektivwert je 100 %) und "Wackler-Knall" (Regler 1029 m/s → Quelle max
1029 m/s, |M_r| max 2,80, 18 N-Wellen; Gegenprobe bei 100 m/s: |M_r| 0,49,
null N-Wellen). Dass |M_r| unter dem Bahntempo liegt, ist Geometrie: M_r ist
die Komponente entlang der Sichtlinie, nicht der Betrag.

### Der Rundenwechsel knallte

@dpa las den Regler "Sprungknall" als Lautstärke für Positionssprünge und war
entsprechend deutlich. Das war er nie - Sprünge sind lautlos und bleiben es
(Schnitt/`CutState`: ausblenden, umbauen, aufblenden). Der Regler hängt allein
an der Startvariante "Knall-Start", die er am 23.08. selbst bestellt hatte.

Falsch war aber etwas anderes: bei eingeschalteter Dauerschleife rief der
Rundenwechsel denselben `startFlyBy()` auf wie der Start von Hand, **samt
Sprungmarke**. Es knallte also bei jeder Runde - und ein Rundenwechsel ist
genau die Sorte Sprung, die ausgeblendet gehört. `beginCut()`/`startFlyBy()`
kennen jetzt den Unterschied (`loopsFlyBy`/`isLoopRound`).

Umbenannt: Beschriftung "Sprungknall" → "Startknall" (EN "Start Boom").
Parameter-ID und Automationsname bleiben, damit Presets weiterlaufen. Der
Hinweistext sagt jetzt zuerst, was der Regler NICHT tut.

**Neu geprüft:** "Rundenwechsel stumm", vier Runden - vorher 3 Knälle, jetzt
1. Gemessen als Differenz zweier sonst gleicher Läufe (mit und ohne
Startknall), weil die Vorbeiflüge selbst ebenfalls laute Stellen sind. Zwei
frühere Anläufe dieser Prüfung lagen daneben und meldeten fälschlich "in
Ordnung"; die Gegenprobe mit dem alten Verhalten hat das aufgedeckt - eine
neue Prüfung ist erst dann eine, wenn sie am alten Stand auch anschlägt.

### Gegenprobe

Alle 47 Szenarien grün. Gegenüber dem Stand vom Vortag ändern sich nur
Wackler-abhängige Zahlen (die Bahn läuft jetzt mit konstantem Betrag) sowie
die Wanduhrmessungen, die ohnehin streuen. Auffällig im Extremszenario
"Front-Duck 1 + Wackler": |M_r| 2,05 → 2,39 und der steilste Pegelsturz
−38 → −48 dB, weil der Wackler dort jetzt tatsächlich seine 340 m/s fährt
statt gebremst zu werden. Harte Abbrüche bleiben 0 von 11.

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

Beim Vorbeiflug nahe Mach 1 kostet der teuerste Block ein Mehrfaches des
Blockschnitts. Gemessen im Szenario "Realistisch nahe Mach1": 9661 gegen 3416
Löser-Auswertungen, der teuerste Block 6106 µs gegen 10667 µs Budget. Das
bleibt unter der Decke, ist aber die Stelle, an der sie zuerst erreicht wird -
und die Statuszeile zählt seit v0.4.0 mit, wie oft ein einzelner Block wirklich
darüber geht. Der Test in `Tests/load_check.cpp` misst und meldet das, lässt den
Lauf aber nicht daran fehlschlagen (@dpa 20260820), damit gepusht werden kann.

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
