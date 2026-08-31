# Warum klingt es anders als erwartet

Überschall-Vorbeiflüge klingen in Dopplerfeld anders, als man es erwartet: nach
dem Knall wird es schlagartig sehr viel leiser, und danach hängt ein Nachlauf
dran, in dem ein Teil des Klangs zeitverkehrt läuft. Der steile Abfall ist
echte Physik. Der Nachlauf - im Plugin die "Fahne" - folgt zwar aus derselben
Gleichung, überzeugt aber niemanden, der ihn hört, und steht deshalb im
Grundzustand auf -60 dB.

Dieses Dokument sagt, woher das kommt, damit man beim Hören weiß, worüber man
sich wundern soll und worüber nicht. Am Ende steht die Messung, an der die
einzige Stelle hängt, die wirklich ein Fehler wäre: ein Hörweg, der bei vollem
Pegel abgeschnitten wird.

Alle Zahlen sind für **Mach 2,04** (700 m/s bei 343 m/s Schallgeschwindigkeit)
in **150 m** seitlichem Abstand gerechnet.

## Die Geometrie in einem Augenblick

![Draufsicht: Flugbahn, Hörer, Mach-Kegel und die zwei Emissionspunkte, deren Schall gleichzeitig eintrifft](mach-kegel.svg)

Das Bild zeigt 0,5 Sekunden nach der dichtesten Annäherung. Der Hörer liegt
innerhalb des Kegels und empfängt in diesem Moment Schall von **zwei** Punkten
der Bahn gleichzeitig: einem weit vor der Annäherung und einem kurz danach.

Die Kegelfläche selbst hat ihn bei 0,381 s überstrichen. Das war der Knall.

Der Grund für die zwei Wege steht in der Retarded-Time-Gleichung selbst:

```
c * (t_h - t_e) = |L - M(t_e)|
```

Bei Unterschall hat sie genau eine Lösung, bei Überschall innerhalb des Kegels
zwei. Beide sind gleichberechtigte Hörwege, sie werden addiert. Genau so ist es
in [`RetardedTimeSolver`](../Source/Physics/RetardedTimeSolver.h) und
[`PropagationPath`](../Source/Physics/PropagationPath.h) gebaut, ohne Sondercode
für den Überschallfall.

## Die zwei Wege laufen auseinander

Ab der Kegelankunft wandern die beiden Lösungen in entgegengesetzte Richtungen
durch die Quellgeschichte.

**Weg A läuft rückwärts**, mit 0,96-facher Geschwindigkeit, also fast in
Echtzeit. Er arbeitet die gesamte Anflugphase rückwärts ab und kommt nie an ein
Ende, solange die Quelle Geschichte hat.

**Weg B läuft vorwärts, auf ein Drittel verlangsamt** (Faktor 0,33). Das sind
gut anderthalb Oktaven tiefer. Er folgt der Quelle beim Wegfliegen.

Direkt an der Kegelfläche ist der Faktor beider Wege `±281`. In dem Augenblick
wird fast eine halbe Minute Quellgeschichte in wenige Millisekunden gepresst.
Das ist die Kaustik, und das ist der Grund, warum der Knall so laut ist: nicht
weil dort mehr Energie entsteht, sondern weil dort sehr viel Zeit auf sehr wenig
Zeit fällt.

Dass Weg A rückwärts läuft, sollte man **normalerweise nicht hören.**
Zeitumgekehrtes Rauschen klingt wie vorwärts laufendes; unser Gehör hat keinen
Richtungssinn für die Zeitachse. Hörbar wird die Umkehr erst bei einem Signal
mit deutlicher Hüllkurven-Asymmetrie, also bei einem Sample mit erkennbarem
Anschlag.

Das ist das Argument. Es erklärt allerdings nur, warum die Umkehr als solche
nicht auffallen sollte - nicht, warum der Nachlauf insgesamt so klingt, wie er
klingt. Beide Wege zusammen ergeben die "Fahne", und die ist der offene Punkt
dieses Dokuments (siehe unten).

## Wie lange es dauern muss

Amplitude beider Wege aus `1/(R * sqrt((1-M_r)² + eps²))`, bezogen auf den
Spitzenwert an der Kegelfläche:

| Zeit nach Annäherung | Abstand Weg A | Pegel |
|---|---:|---:|
| 0,381 s (Knall) | 172 m | 0,0 dB |
| 0,400 s | 207 m | -23,8 dB |
| 0,500 s | 297 m | -32,4 dB |
| 0,750 s | 481 m | -38,4 dB |
| 1,000 s | 656 m | -41,5 dB |
| 2,000 s | 1337 m | -48,0 dB |
| 4,000 s | 2686 m | -54,1 dB |
| 8,000 s | 5378 m | -60,2 dB |

Zwei Dinge stehen da drin.

Erstens fällt der Pegel unmittelbar nach dem Knall **sehr** steil, fast 24 dB in
19 Millisekunden. Das ist die zusammenbrechende Fokussierung.

Zweitens geht es danach in einen langen, flachen Ausklang über, rund 20 dB pro
Zehnerpotenz Zeit. Nach acht Sekunden ist immer noch etwas da.

Die Tabelle sagt: Knall, dann schlagartig viel leiser, dann ein anhaltendes
Rollen, das lange ausklingt, und nichts bricht ab.

**Das ist eine Rechnung, keine Beobachtung.** Die Zahlen stehen so in der
Amplitudenformel, aber sie sind nie gegen eine echte Aufnahme gehalten worden.
Wie laut ein Überflug acht Sekunden später wirklich noch ist, ist damit hier
nicht beantwortet, sondern nur hergeleitet. Der Abschnitt weiter unten sagt,
warum das offen ist.

## Die Fahne

Bei Überschall liefert der Löser mehrere Hörwege gleichzeitig. Einer davon ist
der jüngste - über ihn hört man die Quelle dort, wo sie gerade ist. Alle anderen
tragen ältere Emissionen nach. Zusammen sind sie der Nachlauf nach dem
Vorbeiflug, und im Plugin heißt der die **Fahne**.

Der Regler dafür ist "Fahne" (`extraPathGainDb`), er dämpft alle Wege außer dem
jüngsten. Sein Grundwert steht auf **-60 dB**, also praktisch aus.

Warum es diesen Regler zusätzlich zu dem für den rückwärts laufenden Anteil
gibt, ist der interessante Teil: der ältere Regler griff nur bei zeitverkehrt
gelesenen Wegen (dτ > 1). Gemessen kommt der lauteste Beitrag der Fahne im
Kreisflug-Szenario aber bei dτ = -0,271 - er läuft **vorwärts** und wurde von
jener Blende nie erfasst, auch am Anschlag nicht. Mit -60 dB eingestellt maß
`trail_probe` immer noch +2,3 dB.

Die Fahne ist also nicht der Rückwärtsanteil. Der ist ein Teil davon, und nicht
einmal der lauteste.

### Was daran offen ist

Die Herleitung oben sagt, die zusätzlichen Wege gehören dahin: die
Retarded-Time-Gleichung hat im Kegel mehr als eine Lösung, alle sind
gleichberechtigte Hörwege, also werden sie addiert. Kein Sondercode, keine
Annahme.

@dpa glaubt das nicht. Er dreht die Fahne in jedem Durchgang ganz zu, und der
Grundwert steht deshalb auf -60 dB - was hier herauskommt, klingt für ihn nicht
nach einem Überflug, sondern nach einem Artefakt. Das ist keine
Geschmacksfrage, die man mit einem Regler zudeckt, sondern ein Widerspruch
zwischen Rechnung und Ohr, und er ist nicht aufgelöst.

Was fehlt, um ihn aufzulösen, ist eine Gegenprobe gegen eine echte Aufnahme -
nicht gegen die Formel, aus der das Modell ohnehin gebaut ist. Solange die
aussteht, ist der ganze Abschnitt "wie lange es dauern muss" eine Behauptung
des Modells über sich selbst.

**Dieses Thema hat keinen Abschluss.**

## Was Physik ist und was nicht

| | |
|---|---|
| **Physik** | Der steile Abfall direkt nach dem Knall, rund 24 dB in 19 ms. |
| **Physik** | Der Knall selbst als eigene, additive Druckwellen-Schicht (N-Welle). |
| **Physik** | Der lange Ausklang danach. Er kommt aus dem Schattenausläufer der Hüllkurve, siehe unten. |
| **Offen** | Die Fahne, und darin der rückwärts laufende Weg. Rechnerisch gehört sie dahin, nach dem Ohr nicht. Grundwert -60 dB, siehe oben. |
| **Fehler** | Ein Zweig, der aus einem anderen Grund als der Kaustik stirbt, bekommt weiterhin nur die lineare Rampe. Im Überschall kommt das kaum vor, ausgeschlossen ist es nicht. |

## Die Anti-Klick-Hüllkurve

Jeder Zweig hat einen eigenen Faktor `env` zwischen 0 und 1, mit dem sein
**gesamter** Beitrag multipliziert wird. Meldet der Löser den Zweig, läuft `env`
linear auf 1. Meldet er ihn nicht mehr, läuft `env` auf 0.

Der Grund: ein Zweig, der von einem Sample auf das nächste erscheint oder
verschwindet, ist ein Sprung im Ausgangssignal. Ein Sprung ist Energie über die
gesamte Bandbreite, also ein Knacks. Die Rampe verteilt ihn über eine
Millisekunde.

Eine Millisekunde reicht dafür nur am **Einsatz**. Ein Zweig, der an der
Kaustik stirbt, steht in diesem Moment auf vollem Pegel - eine Rampe von einer
Millisekunde wäre dort kein Ausblenden, sondern ein Schnitt.

Einsatz und Ende sind deshalb nicht symmetrisch. Der Einsatz bleibt die lineare
Rampe: eine Kegelankunft ist eine echte Stoßfront und darf steil sein. Stirbt
ein Zweig dagegen an der Kaustik, also weil `|1 - M_r|` unter
`causticWidths * eps` fällt, bekommt er einen exponentiellen
**Schattenausläufer** mit

```
tau = eps / |dM_r/dt|
```

also der Zeit, in der sich `M_r` um genau eine Regularisierungsbreite bewegt.
Das ist derselbe Verlauf, den die Amplitudenformel selbst vorgibt, und damit
genau der lange Ausklang, den ein echter Überflug hat. Tode aus anderen Gründen
- verlorene Nachführung - behalten die lineare Rampe.

Die N-Welle ist von dieser Hüllkurve entkoppelt: eine ausgelöste
Stoßfront ist unterwegs, unabhängig davon, ob der Löser danach noch eine Wurzel
für diesen Hörweg findet.

## Der Stand messen statt hören

`load_check` misst das alles, damit man nicht hinhören muss, um zu wissen ob es
besser geworden ist. Pro Szenario:

```
Zweig-Tode        4 (   0.8 /s) | env beim Tod Ø 1.000 max 1.000 | env >= 0,5: 100.0 %
davon an der Kaustik 4 (100.0 %) | Ausklang tau Ø 13.795 ms max 13.804 ms
HARTE ABBRUECHE 0 von 4 lauten Toden (  0.0 %)
Ursachen: Nachfuehrung verloren 4 | neue Identitaet 6 | Wurzeln verworfen 0
```

Das Kriterium, an dem der Test hängt:

> Ein Zweig, der mit `env >= 0,5` stirbt, darf nicht in unter 2 ms auf null
> gehen.

Solange dort etwas anderes als null steht, ist der Abbruch da. Das Szenario
heißt `Bremsflug` und fliegt dieselbe Bahn zweimal, einmal mit Mach 2,04 und
einmal mit Mach 0,87, damit der Unterschall-Lauf beweist, dass Bahn und Messung
selbst keinen Sturz erzeugen. Er stirbt dort kein einziges Mal.
