# Physik-Grundlage

Die Herleitung, auf der der Löser steht: Retarded-Time-Gleichung, Eindeutigkeit
und der Übergang zum Überschall, die geschlossene Lösung für den Referenzfall,
Amplitude samt Fokussierungsfaktor, Luftdämpfung, Löser-Algorithmus,
Solver-Rate und Puffergrößen.

Sie steht getrennt vom Code, weil sie nicht aus ihm hervorgeht. Aus
`RetardedTimeSolver.cpp` lässt sich ablesen, WAS gerechnet wird, aber nicht,
warum genau 0, 1 oder 2 Wurzeln auftreten dürfen und warum Brent und nicht
Newton. Der Code verweist an mehreren Stellen hierher, deshalb bleibt die
Nummerierung der Abschnitte, wie sie ist.

Abschnitt 2.5 ist zugleich der Maßstab, gegen den `Tests/solver_check.cpp`
misst: eine geradlinig-gleichförmig bewegte Quelle muss auf Sample-Genauigkeit
dieselben Wurzeln liefern wie die Formel dort.

Was das Plugin an Klassen und Bausteinen hat, steht nicht hier, sondern in
[ARCHITEKTUR.md](../ARCHITEKTUR.md).

## 2. Physik

### 2.1 Koordinaten und Einheiten

Alle internen Positionen sind `Vec3` in Metern. Weltachsen: x nach rechts, y nach vorn, z nach oben. Das GUI zeichnet x nach rechts und y nach oben, also mit invertierter Bildschirm-y-Achse. Dieser Vorzeichenwechsel liegt genau an einer Stelle (`FieldComponent::worldToScreen` / `screenToWorld`) und darf nirgends sonst auftauchen, sonst vertauschen sich links und rechts.

z ist in Phase 1 überall 0, wird aber in allen Formeln, Puffern und Signaturen mitgeführt. Kein Code darf annehmen, dass z konstant ist.

Feldabbildung: n ist die Feldbreite in Metern und entspricht 700 px. Die Höhe ist damit n·400/700 ≈ 0,571·n. Der Maßstab ist isotrop, m_pro_px = n/700. Die Felddiagonale ist 1,152·n, bei n = 10000 also 11520 m.

Gespeichert werden Positionen als normierte Koordinaten in [0,1]² (Parameter `srcX`, `srcY`, `lisX`, `lisY`), umgerechnet erst beim Lesen. Damit ist das Clampen am Rand automatisch, und eine Änderung von n skaliert die Szene, statt Objekte aus dem Feld zu werfen.

### 2.2 Schallgeschwindigkeit

```
c(T) = 331.3 * sqrt(1 + T / 273.15)   [m/s], T in °C
```

Bei T = 20 °C ergibt das 343,21 m/s. Die Formel liegt in `Medium::speedOfSound(double tempCelsius)`. T ist in Phase 1 fest auf 20 °C, aber bereits als Feld in einer `MediumState`-Struktur, nicht als Konstante im Code verstreut. Wenn T später ein Parameter wird, ändert sich nur, wer `MediumState` beschreibt.

Wichtig für Phase 2: eine Änderung von c verschiebt sämtliche Laufzeiten und ist damit ein unstetiges Ereignis im Sinn der Crossfade-Engine.

### 2.3 Die Retarded-Time-Gleichung

Gesucht ist zu einem Hörzeitpunkt t_h derjenige Emissionszeitpunkt t_e, dessen Schall gerade eintrifft. Die Laufzeit muss der Distanz entsprechen, die zwischen Emissionsort und Empfangsort liegt:

```
t_h - t_e = |L(t_h) - M(t_e)| / c
```

M(t_e) kommt aus dem Trajektorien-Ringpuffer. Da M beliebig bewegt wird, existiert kein geschlossener Ausdruck; die Gleichung ist implizit und muss numerisch gelöst werden.

Als Residuum geschrieben, mit t_h fest und t_e als Unbekannter:

```
F(t_e) = c * (t_h - t_e) - |L(t_h) - M(t_e)|
```

Nullstellen von F sind die gültigen Emissionszeitpunkte. Wegen |·| ≥ 0 gilt für jede Nullstelle t_e ≤ t_h, Kausalität ist also eingebaut und muss nicht geprüft werden.

### 2.4 Ableitung, Eindeutigkeit und der Übergang zum Überschall

Mit dem Einheitsvektor von der Quelle zum Empfänger

```
û(t_e) = (L(t_h) - M(t_e)) / |L(t_h) - M(t_e)|
```

und der Radialgeschwindigkeit v_r = û · v_M (positiv, wenn M sich auf L zubewegt) gilt

```
d/dt_e |L - M(t_e)| = -v_r
F'(t_e) = -c + v_r = -c * (1 - M_r),    M_r = v_r / c
```

Solange |v_M| < c ist auch v_r < c, also F' < 0 überall. F fällt streng monoton, es gibt höchstens eine Nullstelle. Unterschall ist damit beweisbar eindeutig, und der Löser darf dort die billige Variante nehmen.

Sobald |v_M| > c werden kann, kann v_r > c werden, F' wechselt das Vorzeichen, und es entstehen zusätzliche Nullstellen. Genau das ist der Mach-Kegel.

### 2.5 Geschlossene Lösung für gleichförmige Bewegung (Referenzfall)

Für M(t) = M0 + v·t mit konstantem v und ruhendem L lässt sich die Gleichung quadrieren. Mit A = L - M(t_h) und der Laufzeit τ = t_h - t_e:

```
c²τ² = |A + v τ|²
(c² - |v|²) τ² - 2 (A·v) τ - |A|² = 0

τ = [ (A·v) ± sqrt( (A·v)² + (c² - |v|²) |A|² ) ] / (c² - |v|²)
```

Bei |v| < c ist der Nenner positiv und die Diskriminante immer positiv: genau eine positive Lösung (Vorzeichen +). Das bestätigt die Monotonie-Aussage.

Bei |v| > c ist der Nenner negativ. Die Diskriminante

```
(A·v)² - (|v|² - c²) |A|²  ≥ 0
```

lässt sich mit dem Winkel α zwischen A und v umschreiben zu

```
sin²α ≤ c² / |v|²   ⟺   |sin α| ≤ 1 / Mach
```

Das ist exakt die Bedingung, dass der Empfänger innerhalb des Mach-Kegels mit Halbwinkel θ = arcsin(c/|v|) liegt. Außerhalb: keine Lösung. Auf dem Kegelmantel: eine Doppelwurzel. Innerhalb: zwei Lösungen. Damit ist das geforderte 0/1/2-Verhalten hergeleitet, nicht behauptet.

Dieser Fall ist zugleich der wichtigste Testfall für den numerischen Löser: eine geradlinig-gleichförmig bewegte Quelle muss auf Sample-Genauigkeit dieselben Wurzeln liefern wie die Formel oben.

### 2.6 Randbedingung vor Pufferbeginn

Der Trajektorien-Ringpuffer reicht nur T_max Sekunden zurück. Die "0 Lösungen"-Situation aus 2.5 gilt für eine seit unendlich langer Zeit fliegende Quelle. Für ein endliches Modell braucht es eine Festlegung, was davor war.

Entscheidung: Vor dem ältesten Puffereintrag ruhte die Quelle an ihrer ältesten bekannten Position. Damit gilt am linken Rand F(t_h − T_max) = c·T_max − R ≥ 0, sofern T_max > D_max/c dimensioniert ist, und am rechten Rand F(t_h) = −|L − M(t_h)| ≤ 0. Es existiert also immer mindestens eine Wurzel, und es gibt nie ein abrupt aus der Stille einsetzendes Signal. Der Überschallknall entsteht dann als das Hinzukommen eines Wurzelpaars zu einer bereits klingenden Wurzel, nicht als Übergang von Stille zu Vollpegel.

Der Löser macht trotzdem keine Annahme über die Anzahl. Ein Vollscan sammelt bis zu `scanCapacity` = 16 Vorzeichenwechsel und behält davon höchstens `maxBranches` = 8 Zweige. Die Reihenfolge ist Absicht: erst zählen, dann verwerfen, statt mitten im Fenster abzubrechen. Was verworfen wird, zählt `droppedRootCount` und meldet `load_check` als "Wurzeln verworfen".

### 2.7 Amplitude

Fernfeld, geometrische Ausbreitung:

```
A_geo = 1 / R,   R = |L(t_h) - M(t_e)|
```

Dazu kommt ein zweiter Faktor, der in der Praxis am häufigsten vergessen wird. Beim Auslesen des Signalpuffers an der fraktionalen Stelle t_e bekommt man die richtige Zeitachse und damit die richtige Tonhöhe, aber nicht die richtige Amplitude. Der Grund: was die Quelle im Emissionsintervall dt_e abstrahlt, kommt im Hörintervall dt_h an, und

```
dt_h / dt_e = 1 - M_r
```

Die Energie wird also zeitlich zusammengedrängt oder gedehnt. Der zugehörige Amplitudenfaktor ist der Kehrwert dieses Jacobi-Faktors:

```
A_focus = 1 / |1 - M_r|
```

Bei M_r → 1 divergiert er. Das ist der physikalische Ursprung des Überschallknalls, kein numerischer Defekt. Gesamtamplitude eines Wurzelzweigs:

```
A = A_geo * A_focus = 1 / (R * |1 - M_r|)
```

Wichtig für die Implementierung: `A_focus` entsteht beim einfachen fraktionalen Auslesen einer Delay-Line nicht von allein und muss als expliziter Gain gesetzt werden.

Regularisierung gegen die Divergenz, glatt statt hart geclampt:

```
denom = sqrt( (1 - M_r)² + eps² )
A     = 1 / (R * denom)
```

eps ist als Parameter "Boom-Limit" in dB exponiert, eps = 10^(−L_dB/20), Default L_dB = 30 (eps ≈ 0,0316). Der Ausdruck hat keine Kante, ist überall differenzierbar und entspricht der Vorstellung einer Quelle mit endlicher Ausdehnung statt eines mathematischen Punkts.

Zusätzlich wird R nach unten begrenzt (`R_min`, Default 0,05 m), damit ein Ohr, das auf der Quelle liegt, nicht in eine Division durch Null läuft.

Der Nahfeldterm 1/R² ist Phase 2. Er ist relevant, wenn R in der Größenordnung der Wellenlänge liegt (bei 100 Hz sind das 3,4 m). Vorbereitung in `PropagationPath`: ein optionaler Term `nearFieldGain(R, dominantFreq)`, in Phase 1 als Funktion vorhanden und konstant 0 liefernd, plus ein Bool `nearFieldEnabled`. Kein Aufrufer muss dafür später geändert werden.

### 2.8 Zeitverkehrter Zweig bei Überschall

Bei einer Wurzel mit M_r > 1 ist 1 − M_r < 0, die Abbildung t_e → t_h läuft rückwärts. Das Signal dieses Zweigs wird zeitverkehrt gehört: die Quelle hat ihren eigenen Schall überholt, man hört die ältere Emission später. Das ist physikalisch korrekt und erzeugt den charakteristischen Doppelschlag. Es braucht dafür keinen Sondercode: der Zweig liefert einfach eine mit der Zeit fallende Emissionszeit, und die fraktionale Leseposition wandert rückwärts durch den Puffer. Die Implementierung darf nur nirgends voraussetzen, dass die Leseposition monoton wächst.

### 2.9 Luftdämpfung

Über die möglichen Distanzen (bis 11,5 km) ist die frequenzabhängige Luftabsorption stark hörbar. `PropagationPath` enthält deshalb pro Zweig einen One-Pole-Tiefpass, dessen Grenzfrequenz aus R abgeleitet wird:

```
fc(R) = clamp( fc0 * (R_ref / R)^k , 200 Hz, 18000 Hz )
```

Defaults: fc0 = 18 kHz, R_ref = 10 m, k = 0,7. Das ist eine hörbar plausible Näherung, keine Normrechnung. Eine genauere Nachbildung nach ISO 9613-1, mit Temperatur- und Feuchteabhängigkeit, ist nicht gebaut. Sie passt in dieselbe Schnittstelle, weil `MediumState` T trägt.

Die Filterkoeffizienten werden pro Solver-Punkt aktualisiert, nicht pro Sample. Der Filterzustand gehört zum Zweig, nicht zum Pfad, damit ein neu erscheinender Zweig sauber bei Null anfängt.

### 2.10 Löser-Algorithmus

Kostenrahmen: ein Vollscan über den gesamten Trajektorienpuffer (bei n = 10000 sind das über 33 s Historie) pro Solver-Punkt und Ohr ist nicht bezahlbar. Der Algorithmus nutzt deshalb die in 2.4 bewiesene Eindeutigkeit im Unterschall.

Zustand pro Pfad: eine Liste bekannter Zweige, jeder mit letzter Wurzel t_e, letztem M_r, letztem R und Filterzustand.

```cpp
struct TrajectorySample {
    double t;          // Sekunden seit Epoch
    Vec3   p;          // Meter
    Vec3   v;          // m/s, beim Schreiben mitgerechnet
    float  speed;      // |v|, für den Überschall-Max-Test
};

struct Root {
    double t_e;        // Emissionszeit
    double R;          // Abstand zum Empfänger
    double machRadial; // M_r
    int    id;         // stabile Zweig-Identität über Blöcke hinweg
};
```

Ablauf pro Solver-Punkt (t_h fest):

```
solve(t_h, L_now, out roots):

  1) Schnelltest: gab es Überschall im relevanten Fenster?
     maxSpeed = trajectory.maxSpeedInWindow(t_h - T_max, t_h)   // O(1)
     supersonicPossible = (maxSpeed > c)

  2) Bekannte Zweige nachführen (immer, billig):
     für jeden bekannten Zweig b:
        // Startwert aus der analytischen Ableitung fortschreiben
        dt_h  = t_h - b.lastT_h
        guess = b.t_e + dt_h / (1 - b.machRadial)
        guess = clamp(guess, windowStart, t_h)
        // Bracket um guess legen und mit Brent verfeinern,
        // NICHT reines Newton: nahe M_r = 1 ist F' ~ 0.
        root  = refine(bracketAround(guess))
        wenn root gültig -> übernehmen, sonst Zweig als "verschwindend" markieren

  3) Wenn !supersonicPossible und genau ein Zweig lebt:
        fertig. Eindeutigkeit ist bewiesen, kein Scan nötig.

  4) Sonst Vollscan über [windowStart, t_h] mit Lipschitz-Sprüngen:
        vmax = maxSpeed
        Lip  = c + vmax          // |F'| <= c + |v_M|
        t = t_h
        F_prev = F(t)
        solange t > windowStart:
            step = max( |F_prev| / Lip , minStep )
            t_next = t - step
            F_next = F(t_next)
            wenn sign(F_next) != sign(F_prev):
                root = refine(bracket t_next .. t)
                roots.add(root)
            F_prev = F_next; t = t_next
        // Begründung: F ist Lipschitz-stetig mit Konstante Lip.
        // Ist |F(t)| > Lip*step, kann im Intervall keine Nullstelle liegen.
        // Weit von der Wurzel entfernt macht der Scan große Sprünge,
        // in der Nähe wird er automatisch feiner.

  5) Zweige abgleichen: neue Wurzeln bekommen eine neue id und einen
     Envelope, der bei 0 startet. Verschwundene Zweige bekommen einen
     Envelope, der auf 0 läuft und danach freigegeben wird.
```

`minStep` ist die Trajektorien-Rasterweite, damit der Scan terminiert. Der Vollscan läuft nur, wenn im Fenster überhaupt Überschall vorkam; das laufende Maximum über `speed` wird als monotone Deque beim Schreiben gepflegt und ist in O(1) abfragbar.

`refine` ist Brent (Bisektion mit inverser quadratischer Interpolation), nicht Newton. Begründung: an der Mach-Front geht F' gegen 0, Newton springt dort unkontrolliert weg. Brent behält das Bracket und kann nicht divergieren. 6 bis 8 Iterationen reichen für Sample-Genauigkeit.

`F(t)` wertet die Trajektorie an beliebigem t aus. Die Stützstellen liegen auf einem festen Raster; dazwischen wird Catmull-Rom interpoliert, damit M(t) C1-stetig ist. Eine nur linear interpolierte Trajektorie hätte an jeder Stützstelle einen Geschwindigkeitssprung und damit einen hörbaren Tonhöhensprung.

Doppelwurzeln genau auf der Mach-Front findet die Vorzeichensuche nicht, weil dort kein Wechsel stattfindet. Das ist unkritisch: es ist ein Ereignis von Maß Null, und die Amplitude ist dort ohnehin durch eps begrenzt. Praktisch findet der Scan das Paar einen Solver-Punkt später, wenn es sich getrennt hat.

### 2.11 Solver-Rate und Delay-Interpolation

Der Löser läuft nicht pro Audio-Sample, sondern in Blöcken von `solverStride` Samples (Default 64, also 750 Hz bei 48 kHz). Zwischen zwei Solver-Punkten muss die Verzögerungszeit interpoliert werden. Lineare Interpolation der Verzögerung ergäbe stückweise konstanten Doppler und damit an jedem Solver-Punkt einen Tonhöhensprung mit 750 Hz Wiederholrate.

Stattdessen wird kubisch nach Hermite interpoliert, und die Endableitungen sind analytisch bekannt. Mit τ = t_h − t_e gilt

```
dτ / dt_h = 1 - 1/(1 - M_r) = -M_r / (1 - M_r)
```

Der Löser liefert also pro Punkt nicht nur τ, sondern auch dτ/dt_h. Damit ist der interpolierte Verzögerungsverlauf C1-stetig und der Doppler springt nicht.

Bei erkanntem Überschall im Fenster wird `solverStride` adaptiv auf 8 verkleinert, weil M_r dort schnell wechselt.

### 2.12 Puffergrößen

Maximale Laufzeit bei n = 10000: 11520 m / 343,2 m/s = 33,6 s. Mit Reserve wird auf 40 s dimensioniert.

Signal-Ringpuffer, mono, 40 s bei 96 kHz: 3,84 M Samples, 15,4 MB als float. Wird einmal in `prepareToPlay` auf die Maximallänge allokiert und danach nie neu, analog zum Vorgehen in `granular/Source/PluginProcessor.cpp` (dort: "Ringpuffer ist physisch immer auf die maximale Länge angelegt").

Trajektorien-Ringpuffer bei 1000 Hz Rasterrate, 40 s: 40000 Einträge à 10 floats, etwa 1,6 MB.

Beide Puffer sind pro Quelle einmal vorhanden und werden von allen `PropagationPath`-Instanzen gelesen. Bei 2 Ohren, später plus Bodenspiegel plus vier Wände, sind das 12 Leser auf einem Datensatz.

Schreibpositionen werden als `int64` fortlaufend geführt und nie gewrappt; das Modulo passiert erst beim Zugriff. Dasselbe Muster wie in `granular`.

---

Zwei Punkte, die beim Umsetzen leicht falsch laufen und deshalb im Plan ausdrücklich begründet stehen: der Amplituden-Fokussierungsfaktor `1/|1 − M_r|` entsteht beim fraktionalen Auslesen einer Delay-Line nicht von allein und muss als expliziter Gain gesetzt werden (Abschnitt 2.7), und der Löser muss Brent statt Newton benutzen, weil `F'(t_e) = −c·(1 − M_r)` an der Mach-Front gegen null geht (Abschnitt 2.10).
</content>
