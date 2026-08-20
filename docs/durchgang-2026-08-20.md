# Dopplerfeld, Durchgang vom 20.08.2026

Arbeitsbericht eines Chats. Was gebaut wurde, was gemessen wurde, was offen ist.
Der git log ist die verbindliche Historie, hier steht das Warum.

---

## 1. Klone schwirren als Wolke, nicht um einen König

**Auftrag:** "Ich will dass der Jitter allgemein ist und dass die Clone genau wie
das Orginal um den selben punkt in EINER Cloud umherschwirren. Also nicht König
und Diener drumherum, sondern Fliegen: jede wie alle anderen durcheinander."

**Was falsch war:** Die Klone saßen als Versatz auf der bereits gewackelten
Quellposition. Sie erbten deren Wackler und legten ihren eigenen obendrauf, die
Quelle war dadurch die ruhige Mitte.

**Lösung:** Der Quell-Wackler läuft separat mit und wird im Klon-Versatz
abgezogen. Aus der fertigen Quellposition ließe er sich nicht mehr
herausrechnen. Übrig bleibt je Fliege genau ein eigener Wackler um einen
gemeinsamen, ruhenden Ankerpunkt.

**Gemessen** (neue Messung "Jitter-Wolke" in load_check, Streuung jeder Fliege
um ihren eigenen Mittelwert):

| | Quelle | Klone | größte/kleinste |
|---|---|---|---|
| vorher | 1,347 m | 1,82 - 1,95 m | 1,445 (der König) |
| nachher | 1,347 m | 1,26 - 1,38 m | 1,092 |

Die 1,445 sind das vorhergesagte Wurzel-2 aus zwei übereinanderliegenden
Wacklern. Gegenprobe gemacht: mit ausgebautem Abzug wird die Messung wieder rot.

Dazu ein Schalter "Jitter An" für das Wackeln insgesamt. Er geht über den
Ausschlag, nicht über ein Überspringen: die Oszillatoren laufen weiter, beim
Wiedereinschalten setzt es dort ein, wo es gerade wäre.

---

## 2. Der Wackler kam gar nicht an

**Befund aus dem Preset "kein CLone, jitter an, kaum Bewegung":**
Eingestellt waren 5 m Ausschlag, angekommen ist fast nichts.

Zwei Ursachen, beide beseitigt:

- Der Wackler lief durch die Bewegungsglättung. Bei tau 0,145 s und 2 Hz bleiben
  davon 23 %.
- Danach lief er unter den Tempo-Deckel, der auf 1 m/s stand. Ein Ausschlag von
  5 m bei 2 Hz braucht rund 63 m/s Spitzengeschwindigkeit. Übrig blieb ein
  Kriechen.

Die Klon-Versätze gingen an beidem vorbei, daher der große Unterschied, sobald
Klone dazukamen.

Der Wackler läuft jetzt an beidem vorbei und kommt erst bei der Übergabe an die
Bahn obendrauf. Begründung: der Deckel heißt "max Fly speed" und meint die
Fluggeschwindigkeit, nicht das Zittern auf der Stelle. Eine Glättung braucht der
Wackler nicht, weil er als Summe stetig driftender Sinusse schon C1-stetig und
damit klickfrei gebaut ist.

**Gemessen:** Quelle wackelt 0,362 m -> 3,464 m (das ist der volle rechnerische
Ausschlag). Nebeneffekt: zwei Glättersätze wurden überflüssig, der Klon-Abzug
ist jetzt exakt statt genähert.

---

## 3. UI-Regressionen und Bedienbarkeit

- **Regler verschwunden.** Der neue Jitter-Schalter saß in einer eigenen Reihe
  und nahm der Knopfreihe 30 der 290 verfügbaren Pixel weg, worauf der Slider
  keine Höhe mehr bekam. Nur noch Beschriftung und Wertfeld waren übrig, Eingabe
  ging nur getippt. Der Schalter sitzt jetzt neben den Knöpfen.
- **Mausrad zerstörte Werte.** Jeder Regler, der beim Scrollen unter den Zeiger
  geriet, schluckte das Rad: Scrollen brach ab, der Wert verstellte sich
  unbemerkt. Regler reichen das Rad jetzt durch, Wert ändern nur mit Cmd.
- **Knopf-Optik.** Ring von 8 px auf 2,2 px, gefüllter Zeiger-Klecks durch eine
  dünne Linie ersetzt, Wertbogen kontrastreicher.
- **Bauzeit unsichtbar.** Sie wurde bei margin+104 gezeichnet, dort beginnt aber
  der erste Knopf der Kopfzeile. Steht jetzt rechts außen.
- **Bauzeit konnte einfrieren.** Wurde an PluginEditor.cpp nichts geändert,
  übersetzte make sie nicht neu und das Fenster zeigte die Zeit eines älteren
  Laufs. Ausgerechnet die Anzeige, an der man alte Fassungen erkennt. Ein
  CMake-Ziel fasst die Datei jetzt vor jedem Bau an.
- **M ließ sich nicht mehr fangen.** Bei großem Wackler zappelte die Quelle zu
  stark für die Maus. Der Trefferbereich sitzt jetzt an der ruhenden
  Ankerposition, Fangradius für M auf 28 px (Hörer bleibt bei 16), und während
  des Ziehens folgt der Punkt der Maus ohne Wackel-Versatz.

---

## 4. Klone vereinfacht

"nur echte Klones, alles andere weg, keine 'billigen', die bringen nichts."

CloneSpray gelöscht, mit ihr die Regler "davon echt" und "Pegel billig" sowie
der Schalter "Automatik" samt Hysterese. Die Zahl der echten Klone ist jetzt die
Gesamtzahl. Der Pegel heißt "Gain" und läuft von -36 bis +36 dB.

Zwei load_check-Prüfungen auf die billige Nachbildung sind entfallen, sie haben
keinen Gegenstand mehr.

---

## 5. Panels und Reglerwege

- Jitter, Hektik und Jitter An sind ins Bewegungs-Panel gezogen.
- Das Bewegungs-Panel zeigt jetzt entweder Vorbeiflug oder Record/Play statt
  beides zugleich. Der gewonnene Platz ging an die Regler, die dort zu klein
  gequetscht waren: 100x100 statt 84x82.
- Im Feld-Panel füllen N-Wave-Größe und Amp-Verlauf die frei gewordene Reihe.
- z von M und L bekommt ein bipolares Skew mit Null in der Mitte. Vorher war der
  Weg durch die negativen Werte so lang, dass sich um die Null herum nichts mehr
  fein einstellen ließ.
- Noise Gain Lo geht jetzt bis -48 .. 24 dB.

---

## 6. N-Welle

**Zuerst:** sie löste überhaupt nie aus. Die Auslösung hing daran, dass das M_r
eines BESTEHENDEN Zweigs die 1 durchquert. Auf einer sauberen Überschall-Geraden
passiert das nie: vor der Kegelankunft ist es still, dann entstehen zwei Zweige
gleichzeitig aus dem Nichts. Der Knall IST die Kegelankunft, also die Geburt des
Zweigpaars. Dorthin gehört die Auslösung, und dort hängt sie jetzt.

**Dann:** sie löste aus, war aber kein Knall. Aus den Aufnahmen gemessen:

| | mit N-Welle | ohne |
|---|---|---|
| Spitze | -21,05 dB | -20,97 dB |
| steilste Flanke | -25,19 dB/Sample | -26,36 dB/Sample |

Der Knall hob den Transienten um gut 1 dB. Nachgerechnet lag seine Amplitude auf
500 m bei 0,036, das sind 8 dB UNTER dem Motorgeräusch derselben Szene.

Zwei Ursachen:

- Pegel: nWaveLevel 8,0 -> 40,0. Auf 500 m sind das jetzt 0,18, rund 6 dB über
  der Szene. In der Nähe übersteuert das und läuft in den Limiter. Das ist
  beabsichtigt, ein Knall aus 20 m ist ohrenbetäubend.
- Flanke: der entfernungsunabhängige Teil der Anstiegszeit lag bei 5 % der
  Pulsdauer, auf 87 ms also 4,4 ms. Was so weich einsetzt, klingt nach Wusch
  statt nach Schlag. Jetzt 2 %, also 1,7 ms.

Dazu die Größenkopplung der Lautstärke (Überdruck proportional Körperlänge^3/4,
dieselbe Asymptotik wie das Abstandsgesetz R^-3/4) und ein Regler
Params::nWaveGainDb mit eigenem Knopf im Feld-Panel.

---

## 7. Kaustik-Lastspitze: als TODO abgelegt

Beim Vorbeiflug nahe Mach 1 kostet der teuerste Block das rund 12-fache des
Blockschnitts. Der Test meldet das weiterhin, lässt den Lauf aber nicht mehr
fehlschlagen, damit gepusht werden kann.

**Wichtigster Befund für den nächsten Anlauf:** die Länge eines einzelnen
Suchfensters ist NICHT die Ursache. Ein testweise auf den Beginn der Geraden
angehobenes Fenster halbierte den Schnitt (12354 -> 7422 Auswertungen), ließ den
teuersten Block aber bei exakt denselben 143994. Die Spitze kommt aus mehreren
Vollscans innerhalb EINES Blocks. Die Fensterverkleinerung selbst wäre als
CPU-Gewinn im Mittel trotzdem zu haben, sie ist nur kein Aussetzer-Fix.

---

## 8. Offener Hauptbefund: der Überschall-Vorbeiflug klingt nicht

@dpa: "warum ist das Rückwärts noch laut und danach ist plötzlich stille.. das
kann doch nicht wahr sein!" und "bei hohen Geschwindigkeiten ist es ein mini
mini kurzer Peak.. das ist kein Überschall!"

Gemessen aus seinen Aufnahmen, Hüllkurve in 50-ms-Fenstern. In JEDER
Überschall-Aufnahme dasselbe Muster: rund -60 dB Stille, dann ein Sprung auf
-21 dB, dann innerhalb einer halben Sekunde 15 bis 20 dB Abfall. Kein
Nachdröhnen.

Drei Dinge stecken dahinter:

**a) Das Boom Limit kann prinzipiell nicht wuchtig klingen.**
Die Amplitude läuft über `denom = sqrt((1-M_r)^2 + eps^2)` mit
`eps = 10^(-dB/20)`. Auf dem Kegel (M_r = 1) ist die Verstärkung damit genau
`1/eps`, also `10^(dB/20)`:

| Boom Limit | Verstärkung auf dem Kegel | Fenster, in dem sie wirkt |
|---|---|---|
| 0 dB | 1-fach (gar keine) | - |
| 12 dB | 4-fach | breit |
| 30 dB | 32-fach | schmal |
| 60 dB | 1000-fach | extrem schmal |

Der Regler steuert Höhe und Dauer des Peaks GEGENLÄUFIG: je höher der Wert,
desto lauter, aber auch desto kürzer. Die Fläche unter dem Peak, also die
Energie, wächst dabei nur logarithmisch. Deshalb wird es nie wuchtig, sondern
mit steigendem Wert nur ein immer schärferer, kürzerer Knacks. Genau das
beschreibt @dpa: bei 0 nichts, bei 12 härterer Attack aber kaum lauter, bei
40-60 ein kurzer Scratch.

Und bei 0 dB gibt es tatsächlich null Kaustik-Verstärkung. Der Überflug klingt
dann wie Unterschall.

**b) Der Kaustik-Boost ist nicht der Knall.**
Er ist ein Artefakt der Punktquellen-Idealisierung: eine echte Quelle hat
Ausdehnung, die Divergenz ist eine mathematische, keine physikalische. Der echte
Knall ist die N-Welle, eine Druckwelle mit endlicher Dauer und echter Energie.
Die Wucht gehört dorthin, nicht in den Regularisierungsparameter.

**c) Nach dem Knall sterben Zweige bei voller Lautstärke.**
Das ist der eigentliche Fehler und steht längst in load_check:

    Zweig-Tode ... env beim Tod Ø 1.000 max 1.000 | env >= 0,5: 100,0 %
    HARTE ABBRUECHE 61 von 208 lauten Toden (29,3 %)
    HARTE ABBRUECHE 91 von 91 lauten Toden (100,0 %)
    längste Stille 0,717 s

Ein Zweig, dessen Hüllkurve bei 1,0 steht, wird abgeschnitten. In der Wirklichkeit
hört man einen Jet nach dem Überflug sekundenlang laut nachdröhnen, und zwar aus
zwei gleichzeitig eintreffenden Zweigen. Wenn einer davon hart stirbt, entsteht
genau die Stille, die @dpa hört.

Die harten Abbrüche sind nicht neu und stammen nicht von der N-Wellen-Änderung,
das wurde mit zurückgenommener Änderung gegengeprüft.

---

## Stand

Alles gepusht, CI grün, load_check grün (die Kaustik meldet sich als OFFEN, ohne
fehlzuschlagen). VST3, AU und Standalone installiert.

**Offen:**

- Der Überschall-Vorbeiflug klingt nicht nach Überschall, siehe Abschnitt 8c.
  Das ist der nächste ernsthafte Brocken.
- Kaustik-Lastspitze, siehe Abschnitt 7 und ARCHITEKTUR.md.
- Der Klang der N-Welle ist gebaut und gerechnet, aber nicht von @dpa abgenommen.
