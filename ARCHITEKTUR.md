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
  Boden- und Wandspiegelung sind genau das und kein Sonderweg: dieselbe
  Klasse mit einem anderen `PathTransform` (Spiegelung an einer beliebigen
  Ebene, siehe `PathTransform.h`), dazu ein zweiter, streckenunabhängiger
  Dämpfungsgrad (`setReflectionDamping`) für die Reflexionsfläche.
  Der Löser trennt **Nachführen** (jeder Solver-Punkt) vom **Entdecken**
  (Vollscan, höchstens alle 0,5 ms, `setDiscoveryIntervalSeconds`) - siehe
  Stand-Abschnitt zur Löser-Last.
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
  die `CollapsiblePanel` mit den `XyzPanel`s rechts in einem Viewport (Motor,
  Sample, Bewegung, Feld/Physik/Ausgang, Reflexionen/Wände). 30-Hz-Timer holt
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

Zuerst zwei Vereinfachungen, die mit den Wänden vom 2026-08-17 neu dazukommen
und bewusst nicht gelöst sind - sie gehören zusammen und wären ein eigener Lauf:

- **Keine Verdeckung.** Eine Wand ist akustisch nur ein Spiegel, kein
  Hindernis. Steht sie zwischen Quelle und Hörer, kommt der Direktschall
  trotzdem ungehindert durch. Verdeckung richtig zu machen heißt Beugung an
  der Kante, und eine unendliche Ebene hat keine.
- **Keine Seitenprüfung der Reflexion.** Wandert die Quelle auf die andere
  Seite einer Wand, wird die Spiegelung weiter gerechnet, obwohl es diesen Weg
  physikalisch nicht gibt. Ein reiner Seitentest wäre billig, aber halb: ohne
  Verdeckung würde er die Reflexion wegnehmen und den (dann falschen)
  Direktschall stehen lassen. Deshalb erst zusammen mit dem Punkt darüber.
- **Große Felder bei Mach 2 sind teuer.** Gemessen im
  N-Wellen-Szenario (Feld 2000 m, Mach 2, Vorbeiflugabstand 300 m): rund 24000
  Löser-Auswertungen pro Block, im Mittel 26 % vom Echtzeitbudget, einzelne
  Blöcke über dem Budget. Ursache ist das lange Suchfenster auf großen Feldern,
  nicht ein Defekt - auf einem 150-m-Feld liegt derselbe Fall bei 6700. Der
  CPU-Balken und der Notaus im Schwarm-Panel sind die Antwort darauf; ein
  weiterer Löser-Sprung wäre ein eigener Lauf (Ansätze siehe unten unter
  "Was am Scan nicht half").

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
