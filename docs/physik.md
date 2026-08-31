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

Abschnitt 3 beschreibt die Bausteine, wie sie entworfen wurden - der Code
verweist auch darauf. Welche Klassen es heute wirklich gibt und wie sie
zusammenhaengen, steht in [ARCHITEKTUR.md](../ARCHITEKTUR.md).

Abschnitt 4 sammelt, wo das Modell absichtlich neben der Physik liegt.

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

## 3. Kernklassen

### 3.1 Vec3 und Medium

```cpp
struct Vec3 { double x = 0, y = 0, z = 0; /* +, -, *, dot, length, normalised */ };

struct MediumState {
    double tempCelsius = 20.0;
    double speedOfSound() const;   // 331.3 * sqrt(1 + T/273.15)
};
```

`MediumState` wird pro Block einmal aus den Parametern gelesen und als Wert an alle Rechenteile durchgereicht. Keine globale Konstante, kein Singleton.

### 3.2 SourceTrajectory

Der geteilte Bewegungsverlauf der Quelle.

```cpp
class SourceTrajectory {
public:
    void prepare (double trajRateHz, double maxSeconds);
    void reset   (Vec3 initialPos, double startTime);   // füllt rückwärts konstant

    void push (Vec3 pos, double time);        // rechnet v selbst aus
    void jumpTo (Vec3 pos, double time);      // Vorgeschichte konstant überschreiben

    bool  sampleAt (double t, Vec3& pos, Vec3& vel) const;   // Catmull-Rom
    double maxSpeedInWindow (double t0, double t1) const;    // O(1), monotone Deque
    double oldestTime() const;
    double newestTime() const;
};
```

`jumpTo` ist der Baustein für Positionssprünge: eine zweite Trajektorie wird mit konstanter Vorgeschichte an der neuen Stelle befüllt, sodass der neue Pfad sofort klingt statt erst nach der Laufzeit einzusetzen.

### 3.3 SourceSignalBuffer

```cpp
class SourceSignalBuffer {
public:
    void  prepare (double sampleRate, double maxSeconds);
    void  write (const float* mono, int numSamples);   // schiebt writePos
    float readAt (double absoluteSampleIndex) const;   // Lagrange 4. Ordnung
    int64 writePosition() const;
    double timeToIndex (double t) const;
};
```

Mono, weil die Quelle ein Punkt ist. Stereo entsteht ausschließlich durch die zwei Ohren.

Interpolation: Lagrange 4. Ordnung. Linear wäre bei langsamer Bewegung zu dumpf, und bei schneller Annäherung (Zeitkompression, Frequenzen steigen) entsteht ohnehin Aliasing. Gegenmaßnahmen: der Generator erzeugt bandbegrenzte Sägezähne (PolyBLEP), und optional läuft die Quellstufe mit 2-fachem Oversampling. Beide Maßnahmen bleiben Parameter, keine Pflicht.

### 3.4 PropagationPath

Die zentrale, beliebig oft instanziierbare Einheit.

```cpp
struct PathTransform {
    Vec3  scale  { 1, 1, 1 };   // Spiegelung: scale.z = -1 für Boden
    Vec3  offset { 0, 0, 0 };   // Wand bei x=w: scale.x=-1, offset.x=2w
    float gain   = 1.0f;        // Reflexionsgrad
};

class PropagationPath {
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void setTransform (const PathTransform&);       // identisch für Direktschall
    void setNearFieldEnabled (bool);                // Phase 2, in Phase 1 no-op

    // Der einzige Rechenaufruf. Liest aus geteilten Puffern, schreibt additiv.
    void process (const SourceTrajectory& traj,
                  const SourceSignalBuffer& sig,
                  const MediumState& medium,
                  Vec3 receiverPos,        // aktuelle Ohrposition
                  Vec3 receiverVel,        // für bewegten Empfänger
                  double blockStartTime,
                  float* out, int numSamples);

    // Nur für die Anzeige, aus dem Message-Thread über einen Snapshot.
    int    numActiveBranches() const;
    double lastDelaySeconds() const;
    double lastMachRadial() const;
};
```

Was in `process` passiert: für jeden Solver-Punkt im Block wird `receiverPos` durch `PathTransform` geschickt, `RetardedTimeSolver::solve` gerufen, pro Zweig τ, dτ/dt_h, R und M_r ermittelt. Dann rendert der Pfad samplerweise, liest über den hermite-interpolierten Verzögerungsverlauf aus `SourceSignalBuffer`, multipliziert mit A_geo · A_focus · Zweig-Envelope · `transform.gain`, schickt es durch den Luftdämpfungsfilter des Zweigs und addiert auf `out`.

Der Pfad besitzt keine Kopie der Trajektorie und keine Kopie des Signals. Beide kommen als const-Referenz herein. Das ist die Bedingung dafür, dass sich Instanzen billig vervielfachen lassen.

Der Löser steckt in einer eigenen Klasse, damit er ohne JUCE testbar ist:

```cpp
class RetardedTimeSolver {
public:
    void  reset();
    int   solve (const SourceTrajectory&, const MediumState&,
                 Vec3 receiverPos, double t_h,
                 Root* outRoots, int maxRoots);   // Rückgabe: Anzahl
};
```

`RetardedTimeSolver` hängt nur von `Vec3`, `SourceTrajectory` und `MediumState` ab, nicht von `juce::AudioBuffer`. Damit lässt er sich in einem kleinen Konsolen-Testprogramm gegen die geschlossene Lösung aus 2.5 prüfen.

### 3.5 Listener und Ohren

```cpp
struct ListenerState {
    Vec3   head { 0, 0, 0 };
    double yaw = 0.0;         // Radiant, 0 = Nase in +y
    double earSpacing = 0.17; // Meter
};

// Nase:  n = ( sin(yaw),  cos(yaw), 0)
// Rechts: r = ( cos(yaw), -sin(yaw), 0)
// L_right = head + (earSpacing/2) * r
// L_left  = head - (earSpacing/2) * r
```

Die Ohrgeschwindigkeit wird aus der geglätteten Kopfbewegung plus dem Rotationsanteil ω × (Ohr − Kopf) gebildet, weil eine reine Kopfdrehung die Ohren tatsächlich bewegt und dabei Doppler erzeugt. Das ist bei schnellem Drehen hörbar und gehört mit hinein.

Die Ohrpositionen werden pro Solver-Punkt neu ausgewertet, nicht pro Block. Die Kopfposition selbst läuft durch denselben `MotionSmoother` wie die Quelle, sonst knackt jede Kopfbewegung.

Nicht enthalten und bewusst nicht ergänzt: Kopfabschattung und Ohrmuschelfilterung. Das sind Beugungs- und Streueffekte, die aus reiner Laufzeitgeometrie nicht folgen. Sie stehen in Abschnitt 7.

### 3.6 DopplerEngine

Klammert Quelle, Puffer und Pfadliste.

```cpp
class DopplerEngine {
public:
    void prepare (double sampleRate, int maxBlock, double maxFieldMetres);
    void reset();

    void setSource (SoundSource* src);           // Zeigertausch, kein Besitz
    void setFieldMetres (double n);
    void setSourceTarget (Vec3 posMetres);       // Ziel, geht in den Smoother
    void setListener (const ListenerState&);
    void jumpSourceTo (Vec3 posMetres);          // unstetig, Aufrufer regelt Crossfade

    void process (juce::AudioBuffer<float>& stereoOut, const MediumState&);

    // Snapshot für die Anzeige, doppelt gepuffert
    void fillSnapshot (FieldSnapshot&) const;

private:
    SourceTrajectory  trajectory;
    SourceSignalBuffer signal;
    PathSet geometry;                     // ein kompletter Geometriesatz
};
```

Die Pfade stehen bewusst in einer Liste und nicht als einzelne Member. Ein `PathSet` traegt sie alle: `pathEar` sagt, auf welchen Ausgangskanal ein Pfad addiert, `pathSurface`, an welcher Flaeche er gespiegelt ist. Eine Flaeche mehr heisst deshalb ein Eintrag mehr, sonst nichts. Zwei `PathSet` laufen bei Positionssprüngen und Feldgrößenwechseln gegeneinander gecrossfadet (`DualPathCrossfader`). Den heutigen Aufbau samt Abgriffpunkten beschreibt `ARCHITEKTUR.md`.

Reihenfolge im Block: Quelle rendert `numSamples` Mono-Samples, diese gehen in `signal.write`. Parallel tickt der Bewegungspfad und schreibt Trajektorienpunkte. Danach laufen alle Pfade über denselben, jetzt vollständigen Datenbestand.

### 3.7 Crossfade-Engine

Ein eigenständiges Bauteil, das an mehreren Stellen instanziiert wird.

```cpp
enum class FadeReason { SourcePosition, SourceTimbre, FieldSize, MediumChange, Manual };

struct FadeContext {
    FadeReason reason;
    double     sampleRate;
    double     positionDeltaMetres = 0;  // für SourcePosition
    double     baseFrequencyHz     = 0;  // für SourceTimbre
    double     smootherTauSeconds  = 0;
    double     manualSeconds       = 0.05;
    bool       useManual           = false;
};

int computeFadeSamples (const FadeContext&);   // freie Funktion, keine Allokation

template <typename Renderer>
class DualPathCrossfader {
public:
    void prepare (double sampleRate, int maxBlock);
    Renderer& active();
    Renderer& pending();

    bool isFading() const;
    void beginSwitch (int fadeSamples);   // pending() ist vorher zu konfigurieren
    void process (juce::AudioBuffer<float>& out);
};
```

Der Ablauf: der Aufrufer konfiguriert `pending()` fertig, ruft `beginSwitch`. Ab dann rendern beide Renderer in getrennte Zwischenpuffer und werden mit Equal-Power gemischt (gA = cos(θ), gB = sin(θ), θ = (π/2)·p, p von 0 auf 1). Nach Abschluss werden active und pending getauscht, der neue pending wird stillgelegt und rendert nicht mehr, kostet also keine CPU.

Retrigger während eines laufenden Fades startet nicht neu. Stattdessen liegt genau ein Zielzustand in einer Warteschlange der Länge eins; der letzte gewinnt. Ohne diese Regel kaskadieren schnelle Reglerbewegungen zu einem Dauerfade.

Natürliche Fadedauern pro Grund, so berechnet `computeFadeSamples`:

Für `SourcePosition` ist das natürliche Maß die Zeit, die eine echte Bewegung für diese Strecke gebraucht hätte: t = positionDeltaMetres / v_ref, mit v_ref als "typische Modellgeschwindigkeit" (Default 30 m/s), geklemmt auf 5 ms bis 500 ms. Kleine Sprünge werden schnell, große Sprünge werden langsamer überblendet.

Für `SourceTimbre` ist das natürliche Maß die Periodendauer der Grundfrequenz: t = k / baseFrequencyHz mit k = 3, geklemmt auf 10 ms bis 300 ms. Bei 600 RPM (10 Hz Grundfrequenz) sind das 300 ms, bei 6000 RPM 30 ms. Das entspricht der Intuition, dass ein langsam laufender Motor länger braucht, um seinen Klang zu wechseln.

Für `FieldSize` gibt es kein Echtzeit-Äquivalent, weil eine Feldgrößenänderung physikalisch nichts ist, was passieren kann. Fester Default 60 ms.

Für `MediumChange` (Phase 2, Temperatur) dasselbe, Default 80 ms.

Bei `useManual` überschreibt `manualSeconds` alles. Der Umschalter zwischen automatisch und manuell ist ein globaler Parameter plus ein Zeitregler.

Konkrete Instanziierungen:

Der Quellwechsel Generator gegen Sample läuft als `DualPathCrossfader<SoundSourceHolder>` vor dem Schreiben in den Signalpuffer. Damit bleibt der gesamte Propagationsteil einfach vorhanden; nur die billige Quellstufe existiert doppelt.

Der Positionssprung von M läuft über zwei `SourceTrajectory`-Objekte, die beide denselben `SourceSignalBuffer` lesen. Das ist der Grund, warum Trajektorie und Signal getrennte Klassen sind. Zwei Trajektorien plus zwei Pfadsätze kosten kurzzeitig doppelte Solver-Last, aber keinen doppelten Speicher für das Signal.

Die Feldgrößenänderung läuft als `DualPathCrossfader<DopplerEngine>` über die komplette Engine. Das ist der teuerste Fall (zweifache CPU während des Fades), aber er tritt selten auf.

Der Solver-Regimewechsel läuft ausdrücklich nicht über den Doppelpfad. Ein globaler Crossfade würde den Knall wegmitteln, den man gerade hören will. Stattdessen bekommt jeder Wurzelzweig einen eigenen kurzen Envelope von 0,5 bis 2 ms beim Erscheinen und Verschwinden. Begründung für die Rampe: ein echter Sprung enthält Energie oberhalb Nyquist; die Rampe ist die Antialiasing-Maßnahme, nicht das Weichspülen des Effekts.

### 3.8 Bewegungsglättung

```cpp
class MotionSmoother {
public:
    virtual ~MotionSmoother() = default;
    virtual void prepare (double tickRateHz) = 0;
    virtual void reset   (Vec3 pos) = 0;
    virtual void setTarget (Vec3 pos) = 0;
    virtual void tick (Vec3& outPos, Vec3& outVel) = 0;
    virtual double naturalTauSeconds() const = 0;   // für computeFadeSamples
};
```

Implementierungen:

`OnePoleSmoother`, exponentiell. p += (target − p)·(1 − exp(−dt/τ)). Einfach, ohne Überschwingen, aber die Geschwindigkeit springt beim Setzen eines neuen Ziels. Da der Doppler direkt an der Geschwindigkeit hängt, ist das hörbar. Deshalb nicht der Default.

`CriticallyDampedSpring`, zweite Ordnung. a = ω²(target − p) − 2ω·v, mit ω = 1/τ. Position und Geschwindigkeit sind beide stetig, es gibt kein Überschwingen. Das ist der Default, weil ein C0-, aber nicht C1-stetiger Pfad einen Tonhöhensprung erzeugt.

`SlewLimiter`, mit getrennten Grenzen für Geschwindigkeit und Beschleunigung (v_max, a_max). Das entspricht am ehesten der Vorstellung einer bewegten Maschine mit Trägheit: sie kann nicht beliebig schnell anfahren und nicht beliebig schnell bremsen. Gut geeignet, wenn man Überschall gezielt erreichen will, weil v_max direkt einstellbar ist.

`OneEuroSmoother`, adaptiv. Bei langsamer Bewegung stark glättend, bei schneller Bewegung wenig verzögernd. Sinnvoll gegen Mauszittern beim langsamen Ziehen.

Der Smoother tickt auf der Trajektorienrate im Audiothread, nicht auf GUI-Events. Die Maus schreibt nur ein Ziel. Dieselbe Kette gilt für Hostautomation und für den Wiedergabepfad, damit alle drei Quellen identisch geglättet werden.

### 3.9 Aufzeichnung und Wiedergabe

```cpp
class MotionRecorder {
public:
    void prepare (double controlRateHz, double maxSeconds);  // 200 Hz, 120 s
    void startRecording (double now);
    void stopRecording();
    void pushSmoothed (Vec3 pos, double t);   // GEGLÄTTETE Position, nicht die rohe Maus
    int  numFrames() const;
    const std::vector<Vec3>& frames() const;
};

class MotionPlayer {
public:
    enum class Interp { Linear, CatmullRom };
    void  setClip (const std::vector<Vec3>&, double controlRateHz);
    void  setSpeed (double);      // 0.25 .. 4.0
    void  setLooping (bool);
    void  trigger (double now);
    bool  isPlaying() const;
    Vec3  tick (double dt);
};
```

Aufgezeichnet wird die geglättete Position, nicht die rohe Mauseingabe. Sonst klingt die Wiedergabe anders als die Live-Bewegung, obwohl beide "dieselbe Bewegung" sein sollen.

Bei `Linear` ist der abgespielte Pfad nur C0-stetig, an jeder Stützstelle springt die Geschwindigkeit und damit die Tonhöhe. Deshalb gilt: bei `Linear` muss der Player-Ausgang durch den Smoother; bei `CatmullRom` ist der Pfad C1 und kann direkt als Position gesetzt werden. Catmull-Rom ist nicht C2, die Beschleunigung springt also noch, das ist akustisch folgenlos.

Der Geschwindigkeitsregler skaliert die Bewegungsgeschwindigkeit und damit den Doppler mit. Bei 2-facher Wiedergabe einer schnellen Aufnahme kann Überschall entstehen. Das ist gewollt und funktioniert nur, weil der Löser von Anfang an überschallfähig ist.

### 3.10 Klangquellen

```cpp
class SoundSource {
public:
    virtual ~SoundSource() = default;
    virtual void prepare (double sampleRate, int maxBlock) = 0;
    virtual void reset() = 0;
    virtual void renderMono (float* out, int numSamples) = 0;
    virtual double dominantFrequencyHz() const = 0;   // für Fade-Policy und Nahfeld
};
```

Mono ist Pflicht. Eine Punktquelle hat kein Stereobild.

`EngineGenerator`:

Grundfrequenz f_base = RPM / 60. Vier Sägezahn-Teiltöne mit Verhältnis r_i, Detune d_i in Cent und Tracking t_i in [0,1]:

```
f_i = r_i * (RPM_ref / 60) * (RPM / RPM_ref)^t_i * 2^(d_i / 1200)
```

Bei t_i = 1 folgt der Teilton exakt proportional. Bei t_i = 0 bleibt er auf fester Frequenz stehen. Dazwischen verschiebt sich das Verhältnis zu f_base beim Hochdrehen, was den mechanischen Charakter erzeugt. RPM_ref ist eine Konstante (Default 1000).

Default-Verhältnisse bewusst leicht schief: 1,000 / 2,017 / 2,981 / 4,043. Exakt ganzzahlige Verhältnisse klingen elektronisch.

Alle Sägezähne per PolyBLEP bandbegrenzt. Phasenakkumulatoren wrappen durch Subtraktion von 1,0, nicht per fmod und nicht per Clamp.

Rauschanteil: weißes Rauschen durch einen Bandpass (`juce::dsp::StateVariableTPTFilter`, Bandpass-Modus). Mittenfrequenz und Pegel wandern mit RPM:

```
fc_noise = fc_lo * (fc_hi / fc_lo)^u,   u = normalisierte RPM in [0,1]
g_noise  = g_lo  + (g_hi  - g_lo) * u^1.5
```

Defaults: fc_lo = 400 Hz, fc_hi = 3000 Hz, g_lo = −24 dB, g_hi = −6 dB, Q = 1,2. Alle vier als Parameter.

Jitter: langsam gefiltertes Rauschen (Tiefpass 3 bis 15 Hz) moduliert f_base um ±j Prozent, j = j0 · u. Default j0 = 1,5 %. Optional eine periodische Komponente bei f_base/2 für den Zündtakt eines Viertakters; als Parameter "Unwucht", Default 0.

`SampleSource`:

Datei laden im Message-Thread über `juce::AudioFormatManager`, Ergebnis in einen `juce::ReferenceCountedObjectPtr` auf ein Puffer-Objekt legen und atomar tauschen. Der alte Puffer wird im Message-Thread freigegeben, niemals im Audiothread. Loop-Punkte als Sample-Indizes mit Naht-Crossfade von 2 bis 20 ms. Pitch als Resampling-Verhältnis mit Lagrange-Interpolation. EQ als Low-Shelf, Peak und High-Shelf über `juce::dsp::IIR`.

### 3.11 Parameter-Layout

Alles in einer Datei `Source/Params.cpp` mit `createParameterLayout()`, so wie in `granular/Source/PluginProcessor.cpp` (Zeilen 12 bis 36), nur ausgelagert, weil es hier deutlich mehr Einträge werden. Jeder Regler ist genau eine Zeile plus `NormalisableRange`. IDs als `constexpr` in `Params.h`, damit sich Tippfehler nicht als stille Fehlfunktion tarnen.

Auszug der Gruppen: Feld (`fieldMetres` 1–10000 mit Skew auf 100 als Mitte, `airTempC` vorbereitet aber in Phase 1 nicht in der UI), Quelle (`srcX`, `srcY` normiert 0–1), Hörer (`lisX`, `lisY`, `lisYaw` in Grad, `earSpacing` 0,10–0,25 m), Motor (`rpm` 0–12000 mit Skew, pro Teilton `harmRatio_i`, `harmDetune_i`, `harmTrack_i`, `harmLevel_i`, dazu `noiseFcLo`, `noiseFcHi`, `noiseGainLo`, `noiseGainHi`, `noiseQ`, `jitterAmount`, `jitterRateHz`, `imbalance`), Sample (`sampleGain`, `samplePitch`, `loopStart`, `loopEnd`, `loopXfadeMs`, `eqLowGain`, `eqMidGain`, `eqMidFreq`, `eqHighGain`), Bewegung (`smootherType`, `smootherTau`, `slewVmax`, `slewAmax`, `playSpeed`, `playInterp`, `playLoop`), Physik (`boomLimitDb`, `airAbsorbAmount`, `solverStride` nur als Debug), Crossfade (`fadeAuto`, `fadeManualMs`), Ausgang (`outputGain`, `limiterOn`).

Für die Zieh-Empfindlichkeit gilt das JUCE-Standardmittel: `slider.setMouseDragSensitivity(...)` beim Anlegen im Editor. Kein eigenes Layout- oder Edit-Mode-System.

### 3.12 Threading und Anzeige

GUI-Thread schreibt nur Ziele: normierte Positionen und Yaw gehen über die APVTS-Parameter, alles andere über eine kleine lock-freie SPSC-Queue mit Kommandos (Sprung, Aufnahme starten, Wiedergabe triggern, Sample getauscht).

Audiothread rechnet alles, allokiert nichts, nimmt keine Locks.

Für die Anzeige schreibt der Audiothread nach jedem Block einen `FieldSnapshot` in einen von zwei Puffern und tauscht einen Atomic-Index. Der Editor liest den jeweils anderen. Der Snapshot enthält die letzten N Trajektorienpunkte (dezimiert auf etwa 100), eine Liste von Wellenfront-Radien, die aktuelle Ohrgeometrie und pro Pfad Verzögerung, Amplitude und M_r.

Die Wellenfronten werden nicht separat modelliert. Der Editor merkt sich einige zurückliegende Emissionszeiten t_k und zeichnet Kreise um M(t_k) mit Radius c·(t_now − t_k). Bei Unterschall ergibt das die bekannten zusammengedrängten Fronten vorne, bei Überschall bildet die Einhüllende den Mach-Kegel von selbst. Es braucht keine eigene Kegelgeometrie.

### 3.13 UI

`FieldComponent`, 700×400, zeichnet Gitter mit Meterbeschriftung, die Wellenfronten, die Spur der letzten Sekunden, M als Punkt und L als Kopfsymbol nach der Skizze: Kreis für den Kopf, ein Dreieck als Nase in Blickrichtung, zwei kleine Bögen seitlich als Ohren, alles um `lisYaw` gedreht. Ziehen an M verschiebt die Quelle, Ziehen am Kopf verschiebt den Hörer, Ziehen an der Nase dreht ihn.

Die Panels für Motor und Sample sind einklappbar. Dafür reicht `juce::ConcertinaPanel` oder eine schlanke eigene `CollapsiblePanel` mit Header-Button; beides ist Standard-JUCE, kein eigenes System.

---

## 4. Wo das Modell absichtlich neben der Physik liegt

Alles bis hierher beschreibt, was aus der Laufzeit von selbst folgt. Dieser
Abschnitt sammelt die Stellen, an denen etwas anderes passiert - weil die Physik
dort einen unendlichen Wert liefert, weil der ehrliche Weg unbezahlbar wäre,
oder weil ein Regler mehr hergeben soll als die Wirklichkeit.

### 4.1 Ausbreitung

**Wände sind unendliche Ebenen.** Eine echte Wand hat Kanten, und an Kanten wird
gebeugt. Hier gibt es nur zwei Seiten: die Spiegelquelle wirkt, solange Sender
und Empfänger auf derselben Seite liegen, und verschwindet sonst. Kein
Beugungssaum, keine Abschattung dahinter.

**Genau eine zusätzliche Reflexionsgeneration.** Quelle → Fläche X → Fläche Y →
Ohr, mit X ≠ Y. Real hört ein Raum nicht nach der zweiten Ordnung auf. Jede
weitere kostet ein Pfadpaar Löserlast, und der Hall ist der bessere Weg dorthin.

**Reflexionen dürfen lauter sein als das Original.** `bounceGainDb` und die
Pegelregler der beiden Wände gehen bis +36 dB. Physikalisch reflektiert keine
Fläche mehr, als auf sie fällt.

**Der Abstandsverlauf ist verstellbar.** `distanceCurve` verschiebt den
Exponenten k in 1/R^k. k = 1 ist die Physik und der Grundwert; alles andere ist
Geschmack, und zwar hörbar.

**Die Luftdämpfung ist eine Näherung** (siehe 2.9): ein One-Pole, dessen
Grenzfrequenz aus der Entfernung folgt. Keine Frequenzbänder, keine Feuchte,
keine Normrechnung.

**Temperatur und Höhe hängen nicht zusammen.** Die Temperatur bestimmt c und
damit die Mach-Schwelle, die Höhe über die Luftdichte den Pegel - zwei getrennte
Regler. In der Atmosphäre sind es keine zwei unabhängigen Größen.

**Keine Kopfabschattung, keine Ohrmuschel, keine HRTF.** Die zwei Ohren sind
zwei Empfangspunkte. Laufzeitdifferenz, unterschiedlicher Doppler und
unterschiedliche Entfernung ergeben sich aus der Geometrie; um den Kopf herum
wird nichts gebeugt und nichts gefiltert.

### 4.2 Die Kaustik

**Der Fokussierungsfaktor wird gedeckelt.** An der Mach-Front geht 1/|1 − M_r|
gegen unendlich (2.7). Gerechnet wird stattdessen
1/sqrt((1 − M_r)² + eps²), mit eps = 10^(−L/20) aus dem Regler "Boom Limit"
(Grundwert 30 dB, also eps ≈ 0,0316). Wie hoch der Knall wird, ist damit eine
Einstellung und kein Ergebnis.

**Der Schattenausläufer ist begrenzt.** Seine Form folgt aus der Geometrie
(tau = eps/|dM_r/dt|, siehe 2.7), seine Länge nicht: nach unten begrenzt ihn die
Anti-Klick-Rampe, nach oben 100 ms. Ohne die obere Grenze bliebe ein Zweig bei
langsam durchlaufender Kaustik für immer hörbar und belegte dauerhaft einen
Steckplatz.

**Es gibt höchstens acht Zweige.** Der Scan sammelt bis zu sechzehn
Vorzeichenwechsel und behält acht (2.6). Was darüber liegt, fällt weg und wird
gezählt.

### 4.3 Der Knall

**Die N-Welle ist eine eigene Schicht.** Sie wird beim Kegeldurchgang ausgelöst
und additiv dazugemischt, statt aus der Ausbreitung zu entstehen. Größe,
Schärfe, Druckwellenanteil und Pegel sind vier Regler. "Wie laut ist ein
Überschallknall" ist damit ausdrücklich keine Frage, die eine Konstante im Code
beantwortet.

**Die Knall-Sperre wirft echte Auslöser weg.** Wenn zwei Fronten dicht
hintereinander eintreffen, sind beide gerechnet und beide da; die Sperre
unterdrückt die zweite trotzdem, weil das Zusammentreffen als Schlag
unangenehmer ist als der Verlust.

**Der zeitverkehrt gehörte Anteil ist regelbar leiser.** Er ist echte Physik
(2.8), steht im Modell aber gleich laut neben dem vorwärts laufenden Weg. Real
geht er darin weitgehend unter - `reverseGain` und `extraPathGainDb` sind die
Regler dafür, letzterer mit Grundwert −60 dB.

### 4.4 Der Hall

**Der Hall läuft nicht durch die Physik.** Ein Abgriffpunkt hört mit voller
Ausbreitung an seiner Stelle im Feld - alles danach nicht mehr. Der Rückweg zum
Hörer ist ein Vorlauf plus Panorama aus der Richtung, in der der Punkt liegt,
keine zweite Ausbreitung. Bewegt sich der Hörer, wandert der Hall in der
Tonhöhe nicht mit; sein Doppler steckt bereits im Eingang. Eine ehrliche
Rückausbreitung bräuchte je Punkt einen zweiten Signalpuffer und ein zweites
Pfadpaar.

**Der Hall rechnet mit festen 343 m/s,** nicht mit c(T) aus `Medium.h`. Seine
Laufzeiten beschreiben eine gedachte Raumform und keinen gemessenen Weg; sie mit
dem Wetterregler wandern zu lassen würde jedes Mal die Puffer neu bemessen, ohne
dass man es hört.

**Ein Abgriffpunkt hört nur die erste Ordnung** - Direktweg, Boden und die
beiden Wände. Auf dem Rückweg werfen ihn die Wände zurück, der Boden nicht.

**Die vier Bauarten sind Hallbauarten, kein Raummodell.** Ein FDN-Netz ist ein
Rückkopplungsnetz und keine Geometrie. Am nächsten an einer echten Fläche liegt
"Draussen", und auch dort ist die Verkopplung eine Matrix, weil sie die Energie
erhalten muss - nicht, weil Bergflanken das tun.

**Das Wandern der Hall-Leitungen** hat kein Vorbild. Es steht da, damit die
Resonanzen nicht stehen bleiben.

### 4.5 Bewegung und Quelle

**Der Tempo-Deckel** (`globalMaxSpeed`) begrenzt jede Bewegungsquelle, auch die
Automation. Eine Quelle, die schneller soll, wird langsamer gemacht.

**Der Wackler** ist ein Bewegungsgenerator, keine Aerodynamik. Dass er im
Überschall Doppelknälle erzeugt, ist eine Folge davon und der Grund für die
Knall-Sperre.

**Fahrtwind und Rauschband** sind gefiltertes Rauschen an einem Regler, kein
Strömungsmodell. Der Tempoanteil wächst mit dem Quadrat der Geschwindigkeit,
weil das ungefähr hinkommt.

**Gas aus der Beschleunigung** koppelt die Drehzahl an die Längsbeschleunigung
der Quelle. Kein realer Antrieb funktioniert so herum - es ist die Umkehrung,
mit der sich eine Fahrt bedienen lässt, ohne zwei Regler gleichzeitig zu
bewegen.

**Klone** bekommen alle volle Löserphysik, teilen sich aber ein einziges
Quellsignal. Zwanzig Maschinen, die exakt dasselbe spielen, gibt es nicht.

---

Zwei Punkte, die beim Umsetzen leicht falsch laufen und deshalb im Plan ausdrücklich begründet stehen: der Amplituden-Fokussierungsfaktor `1/|1 − M_r|` entsteht beim fraktionalen Auslesen einer Delay-Line nicht von allein und muss als expliziter Gain gesetzt werden (Abschnitt 2.7), und der Löser muss Brent statt Newton benutzen, weil `F'(t_e) = −c·(1 − M_r)` an der Mach-Front gegen null geht (Abschnitt 2.10).
