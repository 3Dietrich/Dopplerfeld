#include "PropagationPath.h"
#include "Interpolation.h"

#include <algorithm>
#include <cmath>

void PropagationPath::prepare (double sampleRate, int maxBlockSize)
{
    sr              = sampleRate;
    maxBlockSamples = maxBlockSize;

    reset();
}

void PropagationPath::reset()
{
    solver.reset();

    for (auto& b : branches)
        b = Branch{};

    lastSolveTime = 0.0;
    seeded        = false;

    // Ohne das traegt der erste Block nach einem Reset die Differenz zum
    // Versatz von vorher als Geschwindigkeit ein - ein Wusch aus dem Nichts.
    hasPrevTransformOffset = false;
    prevTransformOffset    = Vec3{};

    dispBranches.store (0);
    dispDelay.store (0.0);
    dispMach.store (0.0);

    deathCount.store (0);
    deathLoudCount.store (0);
    deathEnvSum.store (0.0);
    deathEnvMax.store (0.0);
    evictionCount.store (0);
    causticCount.store (0);
    deathTauSum.store (0.0);
    deathTauMax.store (0.0);
    abruptCount.store (0);
    handoverCount.store (0);

    nWavePairBirthCount.store (0);
    nWaveRisingCount.store (0);
    nWaveFallingCount.store (0);
}

void PropagationPath::setBoomLimitDb (double dB)
{
    boomDb = dB;
    eps    = std::pow (10.0, -dB / 20.0);
}

void PropagationPath::setDistanceCurve (double curve)
{
    const double c = std::min (1.0, std::max (-1.0, curve));

    // Beide Hälften einzeln aufgezogen: die Reglermitte soll genau den
    // Exponenten 1 ergeben, und die Enden liegen nicht symmetrisch um 1
    // (0,3 nach unten, 2,5 nach oben).
    const double span = c >= 0.0 ? (distanceExponentSteep - 1.0)
                                 : (1.0 - distanceExponentFlat);

    distanceExponent = 1.0 + c * span;

    // Kein Gleichheitsvergleich auf 0 - das Projekt baut mit -Wfloat-equal.
    // "Betrag nicht größer als null" deckt +0 und -0 ab und fängt zusätzlich
    // NaN auf: dann wird das reine 1/R gerechnet statt eines NaN-Exponenten.
    plainInverseR = ! (std::abs (c) > 0.0);
}

void PropagationPath::setAirAbsorptionAmount (double amount01)
{
    airAmount = std::min (1.0, std::max (0.0, amount01));
}

void PropagationPath::setAirAbsorption (double fc0Hz, double refMetres, double exponent)
{
    airFc0      = std::max (20.0, fc0Hz);
    airRefM     = std::max (1.0e-3, refMetres);
    airExponent = exponent;
}

void PropagationPath::setReflectionDamping (double amount01, double fcHz)
{
    reflectAmount = std::min (1.0, std::max (0.0, amount01));

    // Der Regler verschiebt die GRENZFREQUENZ, statt einen Filter mit fester
    // Frequenz ein- und auszublenden (@dpa 20260819: "Wände und Boden - Dumps:
    // bis zu 100Hz, wirklich richtig dumpf"). Ganz links ist die Reflexion
    // praktisch offen, ganz rechts steht sie bei fcHz - und das darf jetzt bis
    // in den Bereich gehen, in dem nur noch ein Wummern uebrig bleibt.
    //
    // Logarithmisch, weil Tonhoehe so gehoert wird: die Mitte des Reglerwegs
    // liegt geometrisch zwischen offen und dumpf, nicht arithmetisch.
    constexpr double openFcHz = 20000.0;

    const double closedFc = std::max (20.0, fcHz);

    reflectFcHz = openFcHz * std::pow (closedFc / openFcHz, reflectAmount);
}

void PropagationPath::setSolverStride (int normalStride, int supersonicStrideIn)
{
    baseStride       = std::max (1, normalStride);
    supersonicStride = std::max (1, supersonicStrideIn);
}

void PropagationPath::setTrajectoryGridSeconds (double seconds)
{
    trajGridSeconds = std::max (1.0e-6, seconds);
}

void PropagationPath::setNWave (bool shouldBeEnabled, double sizeMetres, double gainLinear)
{
    nWaveOn    = shouldBeEnabled;
    nWaveSizeM = std::max (0.01, sizeMetres);

    // Nach oben bewusst ohne Deckel: ein Knall darf uebersteuern, dafuer gibt
    // es den sichtbaren Limiter.
    nWaveGain  = std::max (0.0, gainLinear);
}

double PropagationPath::nWaveAt (const Branch& b)
{
    // Die klassische N-Welle: senkrechter Sprung auf +A, linearer Abfall durch
    // null, senkrechter Rücksprung von -A auf 0. Zwei Stoßfronten, dazwischen
    // eine Gerade - das ist die Form, die eine Überschallquelle wirklich
    // abstrahlt, und nicht dasselbe wie ein abklingender Impuls.
    //
    // Die Fronten bekommen eine endliche Anstiegszeit. Das ist keine
    // Bequemlichkeit gegen Aliasing, sondern selbst Physik: eine Stoßfront
    // verbreitert sich auf ihrem Weg durch die Luft, und genau deshalb klingt
    // ein Knall aus der Nähe wie ein Peitschenschlag und aus der Ferne wie ein
    // dumpfes Grollen. nRise wächst deswegen mit der Entfernung (siehe
    // Auslösestelle).
    if (b.nPhase < 0.0 || b.nPhase > b.nDuration)
        return 0.0;

    const double t    = b.nPhase;
    const double T    = b.nDuration;
    const double rise = std::min (b.nRise, 0.4 * T);

    // Linearer Rumpf von +1 nach -1 über die volle Dauer.
    double shape = 1.0 - 2.0 * t / T;

    // Vordere Front: aus der Ruhe auf +1 hochziehen.
    if (t < rise)
        shape *= t / rise;

    // Hintere Front: von -1 zurück auf Ruhe.
    if (t > T - rise)
        shape *= (T - t) / rise;

    return b.nAmp * shape;
}

void PropagationPath::triggerNWave (Branch& b, double c, double listenerTimeNow)
{
    // Pulsdauer aus der Ausdehnung des Körpers: die Zeit, die der Schall
    // braucht, um ihn der Länge nach zu durchlaufen, mal zwei (Bug- und
    // Heckstoß liegen nicht am selben Punkt). Damit heißt größer wirklich
    // länger und tiefer, kleiner kürzer und knackiger - ohne dass hinter dem
    // Regler eine Formel steckt, die niemand nachvollziehen kann.
    b.nDuration = 2.0 * nWaveSizeM / std::max (1.0, c);

    // Verbreiterung der Stoßfront mit der Entfernung, siehe nWaveAt(). Zwei
    // Mikrosekunden je Meter sind eine Modellkonstante: in 100 m ergibt das
    // 0,2 ms (Peitschenknall), in 3 km 6 ms (dumpfes Grollen).
    // Der erste Term ist der Anteil, der NICHT von der Entfernung kommt: die
    // Front ist auch am Ursprung nicht unendlich steil. Er faellt bewusst klein
    // aus, denn er entscheidet ueber den Charakter - bei 5 % der Pulsdauer sind
    // es auf 87 ms Dauer schon 4,4 ms Anstieg, und was so weich einsetzt, klingt
    // nach Wusch statt nach Schlag. Mit 2 % bleiben 1,7 ms, die Front kommt als
    // Kante. Die Verbreiterung mit der Entfernung (zweiter Term) bleibt
    // unangetastet, sie ist die eigentliche Physik dahinter: in 100 m ein
    // Peitschenknall, in 3 km ein dumpfes Grollen.
    b.nRise = 0.02 * b.nDuration + 2.0e-6 * b.R;

    // Eigenes Abstandsgesetz statt des regularisierten Fokussierungsfaktors:
    // die Druckwelle ist eine separate Schicht und soll nicht an demselben
    // eps hängen, das "Boom Limit" für die Amplitudenformel deckelt.
    //
    // Und ausdrücklich NICHT 1/R (@dpa 20260819: "das passt nicht zu den
    // Duesenjaegern, die irgendwo im Himmel sind, kilometer entfernt, und man
    // hoert ploetzlich einen lauten Knall").
    //
    // Eine N-Welle ist keine gewöhnliche Kugelwelle. Sie ist nichtlinear: die
    // Druckspitze läuft schneller als der Fuss, die Welle zieht sich auf
    // ihrem Weg selbst in die Länge und wird dabei flacher statt einfach
    // leiser. Der Überdruck fällt deshalb nur mit rund R^(-3/4). Über 15 km
    // macht das gegenüber 1/R etwa den Faktor 10 aus, also 20 dB - genau der
    // Unterschied zwischen "in 12 km Höhe unhörbar" und "man hört plötzlich
    // einen lauten Knall".
    //
    // Bezugsentfernung, damit nWaveLevel weiter dasselbe bedeutet wie bisher:
    // bei nWaveRefMetres ist das Ergebnis bitgleich zum alten 1/R, näher dran
    // etwas leiser, weiter weg deutlich lauter. 20 m ist bewusst nah gewählt -
    // dort war die Schicht eingehört, und dieser Stand soll erhalten bleiben.
    //
    // Größenkopplung (@dpa: "die N-Welle ist das Druckabbild des Körpers ...
    // Größerer Körper = lauterer Knall waere der passende Ausbau"): dasselbe
    // 3/4-Potenzgesetz wie beim Abstand, denn beide Zahlen kommen physikalisch
    // aus derselben klassischen Sonic-Boom-Asymptotik (Whitham-Theorie): der
    // Spitzenüberdruck einer N-Welle waechst mit L^(3/4) der Körperlänge L,
    // genau wie er mit R^(-3/4) der Entfernung faellt. Bei nWaveSizeRefMetres
    // (15 m, derselbe Wert wie der Default/Skew-Mittelpunkt des "N-Wave
    // Size"-Reglers) ist der Faktor exakt 1 - der bisher eingehörte Klang bei
    // mittlerer Reglerstellung bleibt also unveraendert, kleinere Koerper
    // werden leiser, groessere lauter. Kein Deckel noetig: das Potenzgesetz
    // ist ueber den ganzen Reglerbereich (0,5 bis 200 m) von selbst monoton
    // und endlich.
    b.nAmp = (nWaveLevel / nWaveRefMetres)
           * std::pow (nWaveRefMetres / std::max (b.R, minRadius), nWaveDistanceExponent)
           * std::pow (nWaveSizeM / nWaveSizeRefMetres, nWaveSizeExponent)
           * nWaveGain;

    b.nPhase = 0.0;

    // Fenster fuer die Absenkung des uebrigen Schalls: solange die Stossfront
    // ueber diesen Weg laeuft, kommt nichts anderes durch (siehe
    // setShockDuck). Es wird nur verlaengert, nie verkuerzt - eine zweite,
    // kuerzere Front darf ein noch laufendes Fenster nicht abschneiden.
    shockEndTime = std::max (shockEndTime, listenerTimeNow + b.nDuration);
}

double PropagationPath::shockDuckAt (double listenerTime) const
{
    if (shockDuckAmount <= 0.0)
        return 1.0;

    const double since = listenerTime - shockEndTime;

    // Waehrend des Pulses voll abgesenkt, danach kommt der Ton mit der
    // eingestellten Zeitkonstante zurueck.
    const double g = since <= 0.0 ? 1.0
                                  : std::exp (-since / std::max (1.0e-4, shockDuckRelease));

    return 1.0 - shockDuckAmount * g;
}

void PropagationPath::setReverseGain (double gainLinear)
{
    reverseGain = std::max (0.0, gainLinear);
}

void PropagationPath::setShadowTailSeconds (double seconds)
{
    // Nie kuerzer als die Anti-Klick-Rampe - darunter waere der Ausklang
    // selbst wieder ein Knacks.
    shadowTailSeconds = std::max (rampSeconds, seconds);
}

void PropagationPath::setShockDuck (double amount01, double releaseSeconds)
{
    shockDuckAmount  = std::min (1.0, std::max (0.0, amount01));
    shockDuckRelease = std::max (1.0e-4, releaseSeconds);
}

void PropagationPath::setDiscoveryIntervalSeconds (double seconds)
{
    discoverySeconds = std::max (0.0, seconds);
}

void PropagationPath::setBranchRampSeconds (double seconds)
{
    rampSeconds = std::min (2.0e-3, std::max (0.5e-3, seconds));
}

double PropagationPath::nearFieldGain (double, double) const
{
    // Phase 2 (Plan 2.7 / Abschnitt 7): der 1/R²-Nahfeldterm ist bewusst nicht
    // implementiert. Der Aufruf existiert nur, damit später kein Aufrufer
    // geändert werden muss.
    return 0.0;
}

double PropagationPath::lowpassCoeff (double R) const
{
    // fc(R) = clamp(fc0 * (R_ref/R)^k, 200, 18000) - Plan 2.9. Näherung, keine
    // Normrechnung; ISO 9613-1 passt später in dieselbe Schnittstelle.
    const double fc = std::min (18000.0,
                                std::max (200.0,
                                          airFc0 * std::pow (airRefM / std::max (R, 1.0e-6),
                                                             airExponent)));

    // One-Pole: y += a*(x - y), a = 1 - exp(-2π·fc/fs). Gleichstromverstärkung
    // 1, damit die Dämpfung nur die Höhen kostet und nicht den Pegel.
    const double a = 1.0 - std::exp (-2.0 * 3.14159265358979323846 * fc / sr);

    // airAmount blendet den Koeffizienten gegen 1 (= Bypass). Ohne das wäre
    // "Luftdämpfung aus" immer noch der 18-kHz-Tiefpass.
    return 1.0 - airAmount * (1.0 - std::min (1.0, std::max (0.0, a)));
}

int PropagationPath::findSlot (int id) const
{
    for (int i = 0; i < maxBranchSlots; ++i)
        if (branches[i].used && branches[i].id == id)
            return i;

    return -1;
}

int PropagationPath::freeSlot()
{
    for (int i = 0; i < maxBranchSlots; ++i)
        if (! branches[i].used)
            return i;

    // Kein Steckplatz frei: den leisesten AUSKLINGENDEN Zweig hergeben.
    //
    // Ohne das könnte der längere Ausklang (siehe maxDeathTailSeconds) einen
    // neu ankommenden Zweig verdrängen - der Kegel käme an, fände keinen Platz
    // und bliebe stumm. Das wäre genau der Aussetzer, den der Ausklang
    // beseitigen soll, nur an anderer Stelle. Lebende Zweige bleiben
    // unantastbar; unter den sterbenden geht der leiseste, weil sein Verlust am
    // wenigsten zu hören ist.
    int    victim = -1;
    double lowest = 0.0;

    for (int i = 0; i < maxBranchSlots; ++i)
    {
        const Branch& b = branches[i];

        if (b.wasAlive)
            continue;

        if (victim < 0 || b.env < lowest)
        {
            victim = i;
            lowest = b.env;
        }
    }

    if (victim >= 0)
    {
        evictionCount.store (evictionCount.load() + 1);

        // Eine Verdraengung ist selbst ein Abbruch: der Zweig wird sofort auf
        // null gesetzt, nicht ausgeblendet. Sie muss deshalb nach derselben
        // Regel mitzaehlen wie ein natuerlich zu schnell verschwundener Zweig,
        // sonst verschwaende die Messung genau die Faelle, die sie finden soll.
        const Branch& v = branches[victim];

        if (v.deathEnvValue >= 0.5 && (double) v.deathSampleCount < abruptSeconds * sr)
            abruptCount.store (abruptCount.load() + 1);
    }

    return victim;
}

double PropagationPath::panoramaGain (Vec3 incoming) const
{
    // Seitlichkeit: -1 ganz links, 0 genau voraus (oder dahinter), +1 ganz
    // rechts. Nach vorn und nach hinten ist das Ergebnis dasselbe - genau wie
    // bei einem gewoehnlichen Panorama-Regler, der auch nur eine Achse kennt.
    // Die Unterscheidung vorn/hinten macht weiterhin die Ohrgeometrie ueber die
    // Laufzeit; dieses Panning legt sich nur darueber.
    //
    // Gerechnet wird in der WAAGERECHTEN: die Einfallsrichtung wird erst auf die
    // Grundflaeche projiziert und dann normiert. Sonst zieht die Hoehe das
    // Panning zusammen, und zwar um so mehr, je kleiner das Feld ist - Hoehen
    // stehen in Metern und wachsen nicht mit der Feldgroesse mit. Eine Quelle
    // seitlich vom Hoerer landete dann bei 400 m Feld sauber rechts, bei 10 m
    // Feld fast in der Mitte (gemessen 78 dB gegen 18 dB Seitenunterschied),
    // obwohl sie in beiden Faellen genau rechts steht.
    const Vec3   flatIn    = { incoming.x, incoming.y, 0.0 };
    const Vec3   flatRight = { panRight.x, panRight.y, 0.0 };
    const double lenIn     = flatIn.length();
    const double lenRight  = flatRight.length();

    if (lenIn < 1.0e-9 || lenRight < 1.0e-9)
        return 1.0;   // genau von oben oder von unten: keine Seite

    const double side = std::clamp (flatIn.dot (flatRight) / (lenIn * lenRight), -1.0, 1.0);

    // Gleichbleibende Leistung: L = cos(x), R = sin(x) mit x von 0 (ganz links)
    // bis pi/2 (ganz rechts). Durch cos(pi/4) geteilt, damit eine Quelle genau
    // voraus bei jedem Anteil gleich laut bleibt und der Regler die Lautstaerke
    // nicht mitzieht.
    constexpr double pi     = 3.14159265358979323846;
    constexpr double sqrtTwo = 1.41421356237309504880;

    const double x    = (side + 1.0) * 0.25 * pi;
    const double full = sqrtTwo * (panRightEar ? std::sin (x) : std::cos (x));

    return 1.0 + panAmount * (full - 1.0);
}

void PropagationPath::setPanning (double amount, Vec3 right, bool rightEar)
{
    panAmount   = std::clamp (amount, 0.0, 1.0);
    panRightEar = rightEar;

    // Gerechnet wird im Koordinatensystem dieses Pfades, in dem der Empfaenger
    // gespiegelt ist (siehe PathTransform). Die Seitlichkeit ist ein
    // Skalarprodukt, und die Abbildung ist eine Isometrie - deshalb genuegt es,
    // die Rechts-Achse des Kopfes einmal mitzuspiegeln, statt jede einzelne
    // Einfallsrichtung zurueckzurechnen. Ohne das kaeme die Reflexion an einer
    // seitlichen Wand von der falschen Seite.
    //
    // Muss deshalb NACH setTransform() aufgerufen werden.
    panRight = applyPathTransformVelocity (transform, right);
}

void PropagationPath::evaluateRoot (const SourceTrajectory& traj,
                                    const Root& root,
                                    Vec3   recvPos,
                                    Vec3   recvVel,
                                    double c,
                                    Target& out) const
{
    // R nach unten klemmen, damit ein Ohr direkt auf der Quelle nicht in eine
    // Division durch Null läuft (Plan 2.7, R_min).
    const double R  = std::max (root.R, minRadius);
    const double mr = root.machRadial;

    // Regularisierte Divergenz: denom = sqrt((1 - M_r)² + eps²) statt |1 - M_r|
    // (Plan 2.7). Kantenlos, überall differenzierbar, entspricht einer Quelle
    // mit endlicher Ausdehnung statt eines mathematischen Punkts.
    const double dm    = 1.0 - mr;
    const double denom = std::sqrt (dm * dm + eps * eps);

    // A = A_geo * A_focus. A_focus = 1/|1 - M_r| entsteht beim fraktionalen
    // Auslesen der Delay-Line NICHT von allein und muss hier explizit gesetzt
    // werden (Plan 2.7) - das ist die Stelle, die erfahrungsgemäß fehlt.
    //
    // A_geo = 1/R^k mit einstellbarem k (setDistanceCurve). Bei k = 1 - dem
    // Default und dem physikalisch richtigen Kugelwellen-Fall - wird R direkt
    // benutzt statt std::pow, damit dieser Fall bitgleich bleibt.
    const double rGeo = plainInverseR ? R : std::pow (R, distanceExponent);

    double amp = 1.0 / (rGeo * denom);

    if (nearFieldOn)
        amp += nearFieldGain (R, dominantFreqHz);

    // dτ/dt_h. Für einen ruhenden Empfänger ist das -M_r/(1 - M_r) (Plan 2.11).
    // Der bewegte Empfänger ist der allgemeine Fall derselben Rechnung:
    //   F = c·(t_h - t_e) - |L(t_h) - M(t_e)| = 0
    //   c·(1 - t_e') = û·(v_L - v_M·t_e')  =>  t_e' = (1 - M_L)/(1 - M_r)
    //   τ' = 1 - t_e' = (M_L - M_r)/(1 - M_r)
    // mit M_L = û·v_L/c. Mit v_L = 0 fällt das exakt auf die Planformel
    // zurück. Ohne diesen Term stimmte die Verzögerungssteigung nicht, sobald
    // der Kopf bewegt oder gedreht wird - und genau das soll hörbar sein.
    double machListener = 0.0;
    double panGain      = 1.0;

    if (recvVel.lengthSquared() > 0.0 || panAmount > 0.0)
    {
        Vec3 sourcePos;

        if (traj.samplePositionAt (root.t_e, sourcePos))
        {
            // Zeigt vom Ort der Aussendung zum Ohr, ist also die Richtung, in
            // die der Schall unterwegs war.
            const Vec3 uHat = (recvPos - sourcePos).normalised();

            if (recvVel.lengthSquared() > 0.0)
                machListener = uHat.dot (recvVel) / c;

            if (panAmount > 0.0)
                panGain = panoramaGain (-uHat);
        }
    }

    // Denselben regularisierten Nenner mit Vorzeichen benutzen: an der
    // Mach-Front bleibt die Steigung damit endlich (|dτ/dt_h| <= ~1/eps),
    // ohne dass ein zweiter, anders gesetzter Grenzwert entsteht.
    const double signedDenom = (dm < 0.0) ? -denom : denom;

    out.present = true;
    out.tau     = 0.0;   // vom Aufrufer gesetzt, er kennt t_h
    out.dTau    = (machListener - mr) / signedDenom;
    out.amp     = amp * panGain;
    out.R       = R;
    out.mach    = mr;
    out.lpCoeff = lowpassCoeff (R);
}

void PropagationPath::seedAt (const SourceTrajectory& traj,
                              const MediumState&      medium,
                              Vec3   recvPos,
                              Vec3   recvVel,
                              double t_h)
{
    // Nach reset(), einem Positionssprung oder einer Lücke in der Zeitachse
    // gibt es keinen linken Stützpunkt für die Hermite-Strecke. Statt über die
    // Lücke zu interpolieren wird hier ein Solver-Punkt bei t_h gesetzt; alle
    // dabei gefundenen Zweige starten mit Envelope 0 und rampen sauber ein.
    for (auto& b : branches)
        b = Branch{};

    solver.reset();

    const double c = medium.speedOfSound();

    Root roots[maxBranchSlots];
    const int n = solver.solve (traj, medium, recvPos, t_h, roots, maxBranchSlots);

    for (int i = 0; i < n && i < maxBranchSlots; ++i)
    {
        Target tg;
        evaluateRoot (traj, roots[i], recvPos, recvVel, c, tg);

        Branch& b = branches[i];
        b.used    = true;
        b.id      = roots[i].id;
        b.tau     = t_h - roots[i].t_e;
        b.dTau    = tg.dTau;
        b.amp     = tg.amp;
        b.R       = tg.R;
        b.mach    = tg.mach;
        b.lpCoeff = tg.lpCoeff;
        b.lpZ     = 0.0;
        b.env     = 0.0;
    }

    lastSolveTime     = t_h;
    lastDiscoveryTime = t_h;
    seeded            = true;
}

void PropagationPath::process (const SourceTrajectory&   traj,
                               const SourceSignalBuffer& sig,
                               const MediumState&        medium,
                               Vec3   receiverPos,
                               Vec3   receiverVel,
                               double blockStartTime,
                               float* out,
                               int    numSamples)
{
    if (out == nullptr || numSamples <= 0 || sr <= 0.0)
        return;

    // Pflichtstelle für die Spiegelquellen aus Phase 2 (Plan 3.4): der
    // Empfänger wird zuerst in das Koordinatensystem dieses Pfades gespiegelt.
    // Für den Direktschall ist das die Identität, also ein No-op.
    // Der Versatz dieses Pfades wandert ueber den Block, statt an seinem Anfang
    // zu springen: begonnen wird beim Versatz des vorigen Blocks, die Differenz
    // geht als zusaetzliche Empfaengergeschwindigkeit ein. Weiter unten wird der
    // Empfaenger ohnehin linear mit recvVel extrapoliert (siehe recvAtEnd), die
    // Bewegung ist damit stueckweise linear statt treppenfoermig.
    //
    // Das ist nicht nur eine Glaettung: erst dadurch bekommt die Wackelbewegung
    // eines Klons ueberhaupt einen Doppler-Anteil. Ein reiner Positionssprung
    // ohne Geschwindigkeit ist genau das, was als Bitcrusher zu hoeren war.
    const Vec3 offsetNow  = transform.offset;
    const Vec3 offsetPrev = hasPrevTransformOffset ? prevTransformOffset : offsetNow;
    const Vec3 offsetStep = offsetNow - offsetPrev;

    prevTransformOffset    = offsetNow;
    hasPrevTransformOffset = true;

    const double blockSeconds = (double) numSamples / sr;

    const Vec3 recvPos0 = applyPathTransform (transform, receiverPos) - offsetStep;
    const Vec3 recvVel  = applyPathTransformVelocity (transform, receiverVel)
                        + offsetStep * (1.0 / std::max (1.0e-9, blockSeconds));

    const double c = medium.speedOfSound();

    solver.setMinScanStep (trajGridSeconds);

    // Überschall JETZT (nicht irgendwann in den letzten bis zu 40s) ->
    // feineres Solver-Raster (Plan 2.11), weil M_r dort innerhalb weniger
    // Samples umschlägt. Stride ist eine reine Zeitraster-Entscheidung - ob
    // JETZT fein aufgelöst werden muss, nicht ob es das je einmal musste. Ein
    // Überschall-Moment vor 30s braucht keine 8-fach so oft aufgerufene
    // Löserkette mehr, dessen M_r ändert sich nicht mehr schnell. Die
    // korrektheitskritische Wurzelsuche im Löser hängt daran nicht - dort geht
    // es nur darum, wie oft er gerufen wird, nicht was er dabei findet.
    // (Ursache des von @dpa beobachteten "Aussetzer nach schnellem Bewegen":
    // stride blieb bis zu 40s lang auf 8 hängen.)
    const double recentMaxSpeed = traj.maxSpeedSince (blockStartTime - 2.0);
    const int    stride         = (recentMaxSpeed > c) ? supersonicStride : baseStride;

    // Nicht mehr versucht: den Stride an der Kaustik feiner machen, damit die
    // Wurzel je Schritt weniger weit wandert. Gemessen loest das die Zweige
    // tatsaechlich haeufiger sauber auf (3-Wurzel-Zustand 15 % -> 45 % der
    // Aufrufe), aendert an den Kollisionen aber nichts (291 -> 299) und
    // verschlechtert die harten Abbrueche deutlich (41 % -> 75 %) bei 1,6-facher
    // Loeserlast. Die Ursache liegt also nicht in der Schrittweite.

    // Zusammenhängende Zeitachse? Ein halbes Sample Toleranz reicht, weil der
    // Aufrufer die Blockzeit aus einem ganzzahligen Sample-Zähler bildet.
    if (! seeded || std::abs (blockStartTime - lastSolveTime) > 0.5 / sr)
        seedAt (traj, medium, recvPos0, recvVel, blockStartTime);

    const double envInc = 1.0 / std::max (1.0, rampSeconds * sr);
    const double gain   = (double) transform.gain;

    // Reflexionsdämpfung: fester One-Pole, einmal je Block gerechnet statt je
    // Solver-Punkt - anders als die Luftdämpfung hängt er nicht von R ab.
    // Derselbe Bypass-Trick wie dort (Koeffizient gegen 1 blenden), damit
    // "aus" wirklich aus ist und nicht der Eckfrequenz-Fall.
    // Ein reiner Tiefpass auf der Grenzfrequenz, die der Regler vorgibt (siehe
    // setReflectionDamping) - nicht ein Filter, der anteilig beigemischt wird.
    // Beigemischt bliebe auch bei vollem Regler ein ungefilterter Anteil
    // stehen, und genau der verhindert, dass es wirklich dumpf wird.
    const bool   useReflectLp = reflectAmount > 0.0;
    const double reflectCoeff = std::min (1.0, std::max (0.0,
                                    1.0 - std::exp (-2.0 * 3.14159265358979323846 * reflectFcHz / sr)));

    for (int n0 = 0; n0 < numSamples; n0 += stride)
    {
        const int    len    = std::min (stride, numSamples - n0);
        const double tStart = lastSolveTime;
        const double tEnd   = blockStartTime + (double) (n0 + len) / sr;
        const double h      = tEnd - tStart;

        if (h <= 0.0)
            break;

        // Ohrposition am Solver-Punkt: linear aus Position und Geschwindigkeit
        // extrapoliert. Damit wird die Ohrgeometrie pro Solver-Punkt und nicht
        // pro Block ausgewertet (Plan 3.5), ohne dass der Pfad den Hörer kennt.
        const Vec3 recvAtEnd = recvPos0 + recvVel * (tEnd - blockStartTime);

        // Neue Zweige entstehen nur bei einer Kegelankunft, nachzuführen sind
        // die vorhandenen dagegen an jedem Solver-Punkt. Deshalb wird die
        // teure Suche zeitlich gedeckelt und nicht an den Stride gehängt: bei
        // Stride 64 (Unterschall) liegt ohnehin jeder Solver-Punkt weiter
        // auseinander als das Intervall, dort ändert sich also nichts.
        const bool discover = (tEnd - lastDiscoveryTime) >= discoverySeconds;

        if (discover)
            lastDiscoveryTime = tEnd;

        Root roots[maxBranchSlots];
        const int nRoots = solver.solve (traj, medium, recvAtEnd, tEnd,
                                         roots, maxBranchSlots, discover);

        Target targets[maxBranchSlots];

        // Sammelt die Steckplätze frisch geborener Zweige aus DIESEM
        // Solver-Segment - Grundlage der Paar-Geburt-Erkennung der N-Welle
        // weiter unten (siehe dortige Begründung). Ein Kegel bringt genau ein
        // Paar, der Puffer ist trotzdem so groß wie maxBranchSlots, damit ein
        // unerwarteter Überlauf nie schreibt statt nur die Erkennung verfehlt.
        int newBornSlots[maxBranchSlots];
        int newBornCount = 0;

        for (int i = 0; i < nRoots; ++i)
        {
            Target tg;
            evaluateRoot (traj, roots[i], recvAtEnd, recvVel, c, tg);
            tg.tau = tEnd - roots[i].t_e;

            int slot = findSlot (roots[i].id);

            if (slot < 0)
            {
                // Neuer Zweig (Plan 2.10 Schritt 5). Filter und Envelope fangen
                // bei Null an, der Zustand eines FREMDEN Zweigs wird nicht
                // geerbt (Plan 2.9).
                //
                //
                // GEPRUEFT UND VERWORFEN: den Zustand eines gerade gestorbenen
                // Zweigs mit praktisch derselben Verzoegerung uebernehmen, statt
                // bei null anzufangen. Die Idee war, den Nummernwechsel an der
                // Mach-Front unhoerbar zu machen, ohne die Identitaet im Loeser
                // reparieren zu muessen.
                //
                // Sie greift nicht: bei 291 Kollisionen kam es zu 9 Uebergaben,
                // egal ob gegen die eingefrorene Verzoegerung des Toten, gegen
                // die fortgeschriebene, oder mit einer aus dTau skalierten
                // Reichweite verglichen wurde. Der neu gemeldete Zweig liegt
                // also NICHT bei derselben Verzoegerung wie der gestorbene - er
                // ist keine Umbenennung desselben Hoerwegs, sondern wirklich
                // eine andere Ankunft.
                //
                // Das ist der bisher wichtigste Hinweis: die Paare sterben nicht
                // und tauchen unter neuer Nummer wieder auf, sondern es
                // entstehen und vergehen tatsaechlich staendig neue. Der Kegel
                // ueberstreicht das Ohr also immer wieder, statt einmal.
                slot = freeSlot();

                if (slot < 0)
                    continue;   // mehr als K Zweige: der Löser hat bereits gezählt

                Branch& nb = branches[slot];
                nb         = Branch{};
                nb.used    = true;

                nb.id      = roots[i].id;
                nb.dTau    = tg.dTau;
                nb.amp     = tg.amp;
                nb.R       = tg.R;
                nb.mach    = tg.mach;
                nb.lpCoeff = tg.lpCoeff;

                // Linker Stützpunkt rückwärts aus der bekannten Steigung
                // extrapoliert, damit die Hermite-Strecke im Segment schon
                // stimmt und nicht erst ab dem nächsten Solver-Punkt.
                nb.tau = tg.tau - tg.dTau * h;

                if (newBornCount < maxBranchSlots)
                    newBornSlots[newBornCount++] = slot;
            }

            targets[slot] = tg;
        }

        // --- N-Wellen-Schicht: Auslösung an der Geburt eines Zweigpaares ---
        //
        // Diagnose (@dpa + gemeinsame Analyse): auf einer sauberen
        // Überschallgeraden ist es vor der Kegelankunft still, und mit ihr
        // entstehen zwei Zweige GLEICHZEITIG aus dem Nichts (Wurzelzahl 1 ->
        // 3, siehe rootCountFlips im Löser). Keiner der beiden hat einen
        // Vorwert, dessen M_r durch 1 wandern könnte - der andere Auslöser
        // weiter unten (M_r-Durchgang eines BESTEHENDEN Zweigs) sieht diesen
        // Fall deshalb nie. Der Knall IST aber genau diese Geburt, nicht der
        // spätere Durchgang eines schon laufenden Zweigs.
        //
        // Erkannt wird sie NICHT an jeder neuen Zweig-Identität - davon gibt
        // es laut load_check auch im Unterschall welche (newIdCount), das
        // wäre also ein Fehlalarm bei jedem gewöhnlichen Zweigwechsel.
        // Kriterium ist stattdessen: in diesem Solver-Segment sind GENAU ZWEI
        // neue Zweige entstanden, und beide liegen nahe an M_r = 1. Das ist
        // kein willkürlicher Zusatztest, sondern dieselbe Bedingung, unter der
        // ein Wurzelpaar mathematisch entsteht bzw. wieder vergeht: die Falte
        // liegt genau dort, wo F'(t_e) = -c*(1-M_r) durch null geht, also bei
        // M_r = 1 - derselben Stelle, an der weiter oben auch der
        // Kaustik-Ausklang beim Tod eines Zweigs greift (causticWidths*eps).
        if (nWaveOn && newBornCount == 2)
        {
            Branch& a = branches[newBornSlots[0]];
            Branch& b = branches[newBornSlots[1]];

            const double distA = std::abs (1.0 - a.mach);
            const double distB = std::abs (1.0 - b.mach);

            // Notwendige Bedingung: es muss wirklich Ueberschall sein.
            //
            // Die Naehe zu M_r = 1 allein reicht nicht, denn ihr Mass haengt an
            // eps und damit am Regler "Boom Limit" - der steuert aber nur, wie
            // stark die Amplitudenformel an der Front geglaettet wird, und hat
            // mit der Frage, OB ein Kegel eintrifft, nichts zu tun. Bei
            // Boom Limit 20,8 dB ist causticWidths * eps schon 0,365 breit, es
            // genuegte also ein M_r von 0,64.
            //
            // Ein Wurzelpaar entsteht an der Falte bei M_r = 1, der eine der
            // beiden Zweige liegt danach darueber. Ist keiner von beiden
            // schneller als der Schall, war es keine Kegelankunft, sondern ein
            // Paar aus anderem Grund - und dann gibt es nichts zu knallen.
            const bool trulySupersonic = (a.mach >= 1.0 || b.mach >= 1.0);

            if (trulySupersonic
                && distA < causticWidths * eps && distB < causticWidths * eps)
            {
                // Nur EIN Zweig trägt den Puls, sonst würde dieselbe
                // Kegelankunft doppelt ausgelöst - zwei neue Zweige, aber ein
                // einziges physikalisches Ereignis (der Knall trifft das Ohr
                // einmal, nicht zweimal). Welcher der beiden trägt ist
                // beliebig: an der Falte liegen R und tau für beide praktisch
                // gleich.
                nWavePairBirthCount.store (nWavePairBirthCount.load() + 1);
                triggerNWave (a, c, tStart);
            }
        }

        for (int s = 0; s < maxBranchSlots; ++s)
        {
            Branch& b = branches[s];

            if (! b.used)
                continue;

            const Target& tg    = targets[s];
            const bool    alive = tg.present;

            // Wie schnell dieser Hörweg gerade durch die Kaustik läuft. Aus
            // zwei aufeinanderfolgenden Solver-Punkten; beim allerersten Punkt
            // eines Zweigs gibt es noch keinen Vorwert (machSeen), dort bleibt
            // die Rate stehen statt aus einem Sprung gebildet zu werden.
            if (alive && b.machSeen && h > 0.0)
                b.machRate = std::abs (tg.mach - b.prevMach) / h;

            // Todesmessung (siehe branchDeaths() im Header): genau die Flanke
            // "wurde gemeldet -> wird nicht mehr gemeldet". b.env trägt hier
            // noch den Wert vom Ende des vorigen Segments, also den Pegel, mit
            // dem der Zweig in den Ausklang geht.
            if (b.wasAlive && ! alive)
            {
                deathCount.store (deathCount.load() + 1);
                deathEnvSum.store (deathEnvSum.load() + b.env);

                if (b.env > deathEnvMax.load())
                    deathEnvMax.store (b.env);

                if (b.env >= 0.5)
                    deathLoudCount.store (deathLoudCount.load() + 1);

                // Der SCHATTENAUSLAEUFER gilt nur für den Tod an der Kaustik:
                // dort folgt die Ausklangdauer aus der Physik, nämlich daraus,
                // wie schnell M_r durch die Front läuft.
                //
                // "An der Kaustik" heißt: M_r liegt innerhalb der Breite, auf
                // die eps die Divergenz ohnehin glättet. Ausserhalb davon ist
                // der Fokussierungsfaktor unauffällig, dort gibt es auch nichts
                // abzuschneiden.
                //
                // Ein Zweig kann aber auch aus ganz anderen Gründen
                // verschwinden - die Nachführung verliert die Wurzel, ein
                // Vollscan kommt zu spät, ein Sprung in der Geometrie. Das sind
                // Ereignisse des Lösers, keine akustischen: der Schall ist
                // weiter da, wir haben ihn nur verloren. Stirbt so ein Zweig
                // laut, bekommt er deshalb einen kurzen eigenen Ausklang statt
                // der Millisekunden-Rampe (siehe lostBranchTailSeconds im
                // Header) - andernfalls schneidet ein Löserfehler hörbar echtes
                // Signal weg.
                const double distanceToCone = std::abs (1.0 - b.mach);
                const bool   diedAtCaustic  = (distanceToCone < causticWidths * eps);

                if (diedAtCaustic)
                    // Untergrenze ist der eingestellte Schattenausklang (siehe
                    // setShadowTailSeconds), nicht mehr die blosse
                    // Anti-Klick-Rampe: die rechnerische Dauer faellt bei
                    // schnellen Vorbeifluegen immer auf diese Untergrenze, und
                    // dann entscheidet allein sie, ob der Zweig ausklingt oder
                    // abreisst. Der Deckel nach oben bleibt.
                    b.deathTau = std::min (std::max (maxDeathTailSeconds, shadowTailSeconds),
                                           std::max (shadowTailSeconds,
                                                     eps / std::max (b.machRate, 1.0e-9)));
                else if (b.env >= lostBranchMinEnv)
                    b.deathTau = lostBranchTailSeconds;
                else
                    b.deathTau = 0.0;

                b.deathEnvValue    = b.env;
                b.deathTauValue    = b.tau;
                b.deathSampleCount = 0;

                // Nur echte Kaustik-Tode zaehlen hier mit: sonst zeigte die
                // Statistik "davon an der Kaustik" jeden verlorenen Zweig an
                // und waere als Diagnose wertlos.
                if (diedAtCaustic && b.deathTau > 0.0)
                {
                    causticCount.store (causticCount.load() + 1);
                    deathTauSum.store (deathTauSum.load() + b.deathTau);

                    if (b.deathTau > deathTauMax.load())
                        deathTauMax.store (b.deathTau);
                }
            }

            b.wasAlive = alive;

            // Ein Faktor je Sample für den Kaustik-Ausklang. Exponentiell statt
            // linear, weil ein Schattenausläufer so aussieht und weil eine
            // lineare Rampe an ihrem Ende wieder eine Ecke hätte.
            const double deathDecay = (! alive && b.deathTau > 0.0)
                                     ? std::exp (-1.0 / std::max (1.0, b.deathTau * sr))
                                     : 0.0;

            // Verschwundener Zweig: mit der zuletzt bekannten Steigung
            // weiterlaufen lassen, während der Envelope auf 0 fährt. Ein
            // harter Abbruch wäre ein Sprung und damit Energie oberhalb
            // Nyquist (Plan 3.7).
            const double tau1   = alive ? tg.tau     : b.tau + b.dTau * h;
            const double dTau1  = alive ? tg.dTau    : b.dTau;
            const double amp1   = alive ? tg.amp     : b.amp;
            const double coeff  = alive ? tg.lpCoeff : b.lpCoeff;

            // Hermite-Tangenten müssen auf den Parameter u in [0,1] skaliert
            // sein, deshalb dτ/dt_h mal Segmentlänge (siehe Interpolation.h).
            const double m0 = b.dTau * h;
            const double m1 = dTau1 * h;

            const double tau0 = b.tau;
            const double amp0 = b.amp;

            // --- N-Wellen-Schicht: zweiter Auslöser (bestehender Zweig) ---
            //
            // Der PRIMÄRE Auslöser ist die Paar-Geburt weiter oben (Kegel-
            // ankunft auf einer sauberen Überschallgeraden). Hier zusätzlich:
            // wenn der M_r eines bereits bestehenden, laufenden Zweigs die 1
            // durchquert - der Fall, wenn z.B. innerhalb einer Bewegung durch
            // Mach 1 beschleunigt wird, ohne dass dabei ein neues Zweigpaar
            // entsteht. Ein frisch geborener Zweig hat keinen Vorwert
            // (b.machSeen ist dann noch false, siehe Branch{}) und feuert hier
            // deshalb nie - der ist schon oben über die Paar-Geburt bedient,
            // sonst gäbe es an derselben Kegelankunft einen doppelten Puls.
            const double machNow = alive ? tg.mach : b.mach;

            if (nWaveOn && b.machSeen && ((b.prevMach < 1.0) != (machNow < 1.0)))
            {
                // Richtung der Durchquerung getrennt zählen: aufsteigend ist
                // die Kegelankunft an einem laufenden Zweig, absteigend der
                // Ruecklauf des zeitverkehrten Zweigs durch 1.
                if (machNow >= 1.0)
                    nWaveRisingCount.store (nWaveRisingCount.load() + 1);
                else
                    nWaveFallingCount.store (nWaveFallingCount.load() + 1);

                triggerNWave (b, c, tStart);
            }

            b.prevMach = machNow;
            b.machSeen = true;

            // Absenkung durch eine laufende Stossfront: zweimal je Segment
            // ausgewertet und dazwischen linear geblendet, statt pro Sample ein
            // exp() zu rechnen. Segmente sind kurz (Solver-Stride), der
            // Unterschied zum echten Exponentialverlauf liegt weit unter der
            // Hoerschwelle - der Rechenaufwand pro Sample und Zweig nicht.
            const double duck0 = shockDuckAt (tStart);
            const double duck1 = shockDuckAt (tStart + (double) len / sr);

            for (int i = 0; i < len; ++i)
            {
                const double u  = (double) i / (double) len;
                const double tH = tStart + (double) i / sr;

                // Kubisch nach Hermite (Plan 2.11). Linear wäre stückweise
                // konstanter Doppler und damit an jedem Solver-Punkt ein
                // Tonhöhensprung mit Stride-Wiederholrate.
                const double tau = hermite (tau0, m0, tau1, m1, u);

                // Leseposition darf rückwärts wandern - bei M_r > 1 wird der
                // Zweig zeitverkehrt gehört (Plan 2.8). Kein Sondercode, aber
                // auch keine Annahme über Monotonie.
                const double x = (double) sig.readAt (sig.timeToIndex (tH - tau));

                const double amp = amp0 + (amp1 - amp0) * u;

                b.lpZ += coeff * (x * amp - b.lpZ);

                // Ein einzelner nicht-endlicher Wert (Rand-/Sonderfall
                // irgendwo in der Kette) würde diesen One-Pole sonst für
                // immer vergiften - "+= coeff*(... - NaN)" bleibt NaN, egal
                // was als Nächstes hereinkommt. Die Ausgangsstufe filtert nur
                // noch das einzelne Sample, nicht diesen Zustand - ohne diese
                // Selbstheilung bliebe der Zweig bis zum Neuladen des Plugins
                // stumm (genau das beobachtete Verhalten: Ton weg, Neustart
                // hilft).
                if (! std::isfinite (b.lpZ))
                    b.lpZ = 0.0;

                // Zweite, streckenunabhängige Dämpfungsstufe für gespiegelte
                // Pfade. Beim Direktschall ist reflectAmount 0 und die Stufe
                // wird komplett übersprungen - sie kostet dort nur den (immer
                // gleich ausgehenden, also gut vorhersagbaren) Sprung.
                double y = b.lpZ;

                if (useReflectLp)
                {
                    b.refZ += reflectCoeff * (y - b.refZ);

                    // Dieselbe Selbstheilung wie bei lpZ: ein einzelner
                    // nicht-endlicher Wert würde den Zustand sonst dauerhaft
                    // vergiften und der Zweig bliebe für immer stumm.
                    if (! std::isfinite (b.refZ))
                        b.refZ = 0.0;

                    y = b.refZ;
                }

                // Envelope nach dem Filter, damit ein leiser Zweig freigegeben
                // werden kann, ohne dass ein abgeschnittener Filterschwanz
                // knackt.
                //
                // Einsatz und Ausklang sind bewusst NICHT symmetrisch: der
                // Einsatz bleibt die lineare Anti-Klick-Rampe aus Plan 3.7 (eine
                // Kegelankunft ist eine echte Stoßfront und darf steil sein),
                // der Ausklang folgt der Kaustik, aus der der Zweig
                // verschwindet - siehe maxDeathTailSeconds im Header.
                if (alive)
                    b.env = std::min (1.0, b.env + envInc);
                else
                {
                    ++b.deathSampleCount;

                    if (b.deathTau > 0.0)
                        b.env *= deathDecay;
                    else
                        b.env = std::max (0.0, b.env - envInc);
                }

                // Die N-Welle kommt ADDITIV oben drauf und läuft bewusst NICHT
                // durch die Filterkette der Amplitudenformel: sie ist eine
                // eigene Schicht, ihre Entfernungsabhängigkeit steckt in
                // nAmp und ihre Höhen in der Breite der Stoßfront.
                //
                // Sie läuft AUCH NICHT durch den Anti-Klick-Envelope. Eine
                // einmal ausgelöste Stoßfront ist unterwegs - ob der Löser
                // danach noch eine Wurzel für diesen Hörweg findet, ändert an
                // ihr nichts. Sie an env zu hängen hiess, dass ein sterbender
                // Zweig seinen eigenen Knall mitten im Puls abschneidet. Ihre
                // Hüllkurve hat sie in nRise/nDuration selbst (siehe
                // nWaveAt), sie braucht keine zweite.
                // Zeitverkehrt gehoerter Zweig (siehe setReverseGain): die
                // Leseposition wandert mit (1 - dTau), ueber dTau = 1 also
                // rueckwaerts. Geblendet statt geschaltet, sonst waere der
                // Uebergang ein Pegelsprung mitten im Signal.
                double reverseFactor = 1.0;

                if (reverseGain != 1.0)
                {
                    const double dTauNow = b.dTau + (dTau1 - b.dTau) * u;
                    const double blend   = std::min (1.0, std::max (0.0,
                                              (dTauNow - 1.0) / reverseBlendWidth));

                    reverseFactor = 1.0 + (reverseGain - 1.0) * blend;
                }

                // Die Absenkung trifft NUR den Zweiginhalt, nicht die N-Welle
                // darunter: gedaempft werden soll der Motor waehrend der
                // Stossfront, nicht der Knall selbst.
                double outSample = y * b.env * reverseFactor
                                 * (duck0 + (duck1 - duck0) * u);

                if (b.nPhase >= 0.0)
                {
                    outSample += nWaveAt (b);

                    b.nPhase += 1.0 / sr;

                    if (b.nPhase > b.nDuration)
                        b.nPhase = -1.0;
                }

                out[n0 + i] += (float) (outSample * gain);
            }

            b.tau     = tau1;
            b.dTau    = dTau1;
            b.amp     = amp1;
            b.lpCoeff = coeff;

            if (alive)
            {
                b.R    = tg.R;
                b.mach = tg.mach;
            }
            else if (b.env < envFloor)
            {
                // Ausgelaufen, Slot frei für den nächsten Zweig. Ein
                // exponentieller Ausklang erreicht die Null nie exakt, deshalb
                // eine Schwelle statt eines Vergleichs mit 0 (siehe envFloor).
                //
                // Hier steht fest, wie lange der Ausklang wirklich gedauert
                // hat. War der Zweig beim Tod laut und ist trotzdem in wenigen
                // Millisekunden verschwunden, ist genau das der Abbruch.
                if (b.deathEnvValue >= 0.5
                    && (double) b.deathSampleCount < abruptSeconds * sr)
                    abruptCount.store (abruptCount.load() + 1);

                b.env  = 0.0;
                b.used = false;
            }
        }

        lastSolveTime = tEnd;
    }

    // Anzeigewerte: der lauteste Zweig ist der, den man hört (Plan 3.12).
    int    active   = 0;
    double bestAmp  = -1.0;
    double bestTau  = 0.0;
    double bestMach = 0.0;

    for (const auto& b : branches)
    {
        if (! b.used)
            continue;

        ++active;

        if (b.amp > bestAmp)
        {
            bestAmp  = b.amp;
            bestTau  = b.tau;
            bestMach = b.mach;
        }
    }

    dispBranches.store (active);
    dispDelay.store (bestTau);
    dispMach.store (bestMach);
}
