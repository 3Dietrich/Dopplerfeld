#include "DopplerEngine.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double twoPi = 6.283185307179586476925;

// Winkeldifferenz auf (-π, π] bringen, damit ein Sprung über den Nulldurchgang
// von yaw nicht als rasend schnelle Drehung in die Ohrgeschwindigkeit geht.
double wrapAngle (double a)
{
    a = std::fmod (a + 3.14159265358979323846, twoPi);

    if (a < 0.0)
        a += twoPi;

    return a - 3.14159265358979323846;
}
}

//======================================================================
// PathSet - ein kompletter Geometriesatz

void DopplerEngine::PathSet::prepare (double sampleRate, int maxBlockSize, int numPathsIn,
                                      double trajRateHz, double maxSeconds)
{
    sr = sampleRate;

    trajectory.prepare (trajRateHz, maxSeconds);

    paths.resize ((size_t) std::max (0, numPathsIn));

    for (auto& p : paths)
    {
        p.prepare (sr, maxBlockSize);
        p.setTransform (PathTransform{});
        p.setTrajectoryGridSeconds (1.0 / trajRateHz);
    }
}

void DopplerEngine::PathSet::reset (Vec3 pos, double time, const ListenerState& l)
{
    // Vorgeschichte konstant an pos (Plan 2.6): damit existiert von der ersten
    // Probe an mindestens eine Wurzel, egal ob der Satz gerade frisch
    // angefangen hat oder aus einem Sprung kommt.
    trajectory.reset (pos, time);
    lastPos = pos;

    listener     = l;
    prevListener = l;

    for (auto& p : paths)
        p.reset();
}

void DopplerEngine::PathSet::push (Vec3 pos, double t)
{
    trajectory.push (pos, t);
    lastPos = pos;
}

void DopplerEngine::PathSet::renderInto (juce::AudioBuffer<float>& dest, int numSamples)
{
    // Der Renderer-Vertrag verlangt Überschreiben, die Pfade addieren aber -
    // also hier einmal räumen, danach summieren sich mehrere Zweige und
    // mehrere Pfade wie gehabt auf.
    for (int ch = 0; ch < dest.getNumChannels(); ++ch)
        dest.clear (ch, 0, numSamples);

    if (signal == nullptr || medium == nullptr || pathEar == nullptr || sr <= 0.0)
        return;

    const int numCh = dest.getNumChannels();
    const double dt = (double) numSamples / sr;

    // Position gilt zu blockStartTime, die Geschwindigkeit deckt den Block ab -
    // PropagationPath extrapoliert damit die Ohrposition an jedem Solver-Punkt
    // (Plan 3.5). Bei eingefrorenem Satz sind listener und prevListener gleich,
    // die Ohrgeschwindigkeit ist dann exakt 0.
    const Vec3 headVel = (listener.head - prevListener.head) * (1.0 / dt);

    // ω zeigt in z-Richtung. yaw wächst von +y nach +x, also mathematisch
    // negativ - deshalb das Minuszeichen (Herleitung in Listener.h).
    const double yawRate = -wrapAngle (listener.yaw - prevListener.yaw) / dt;

    for (size_t i = 0; i < paths.size() && i < pathEar->size(); ++i)
    {
        const bool rightEar = ((*pathEar)[i] != 0);

        const Vec3 pos = earPosition (prevListener, rightEar);
        const Vec3 vel = earVelocity (prevListener, headVel, yawRate, rightEar);

        const int ch = std::min ((*pathEar)[i], numCh - 1);

        paths[i].process (trajectory, *signal, *medium,
                          pos, vel, blockStartTime,
                          dest.getWritePointer (ch), numSamples);
    }
}

//======================================================================
// DopplerEngine

void DopplerEngine::prepare (double sampleRate, int maxBlockSize, double maxFieldMetres)
{
    sr       = sampleRate;
    maxBlock = std::max (1, maxBlockSize);

    // Längste mögliche Laufzeit: die Felddiagonale ist 1,152·n (Plan 2.1),
    // geteilt durch die langsamste je auftretende Schallgeschwindigkeit
    // (331,3 m/s bei 0 °C), plus Reserve. Bei n = 10000 sind das rund 42 s,
    // was der 40-s-Dimensionierung aus Plan 2.12 entspricht.
    //
    // Bemessen wird nach maxFieldMetres, nicht nach fieldMetres: die Puffer
    // stehen damit unabhängig vom aktuellen Feldmaßstab - erst das macht den
    // Feldgrößen-Crossfade weiter unten allokationsfrei möglich.
    const double diagonal = 1.152 * std::max (1.0, maxFieldMetres);
    maxHistorySeconds = std::max (0.5, diagonal / 331.3 * 1.2 + 0.1);

    signal.prepare (sr, maxHistorySeconds);

    silence.assign ((size_t) maxBlock, 0.0f);

    // Phase 1: genau zwei Pfade, linkes und rechtes Ohr, beide mit
    // Identity-Transform. Phase 2 hängt hier weitere Einträge an.
    pathEar = { 0, 1 };

    geometry.prepare (sr, maxBlock, 2);

    geometry.active().prepare  (sr, maxBlock, (int) pathEar.size(), trajectoryRateHz, maxHistorySeconds);
    geometry.pending().prepare (sr, maxBlock, (int) pathEar.size(), trajectoryRateHz, maxHistorySeconds);

    setBoomLimitDb (boomLimitDb);
    setAirAbsorptionAmount (airAbsorbAmount);

    reset();
}

void DopplerEngine::reset()
{
    sampleClock   = 0;
    nextTrajIndex = 1;   // t = 0 belegt bereits der Reset-Stützpunkt

    // prepare() auf unveränderter Größe füllt den vorhandenen Speicher nur neu
    // und allokiert deshalb nicht - ein eigenes clear() hat SourceSignalBuffer
    // nicht, und der Puffer muss beim Zurücksetzen wirklich leer sein.
    if (sr > 0.0)
        signal.prepare (sr, maxHistorySeconds);

    geometry.reset();
    geometry.active().reset  (sourceTarget, 0.0, listener);
    geometry.pending().reset (sourceTarget, 0.0, listener);

    prevTarget       = sourceTarget;
    queuedJumpPos    = sourceTarget;
    fieldChangeArmed = false;
}

int DopplerEngine::fadeSamplesFor (FadeReason reason, double positionDeltaMetres) const
{
    FadeContext ctx;
    ctx.reason              = reason;
    ctx.sampleRate          = sr > 0.0 ? sr : 48000.0;
    ctx.positionDeltaMetres = positionDeltaMetres;
    ctx.manualSeconds       = manualFadeSeconds;
    ctx.useManual           = useManualFade;

    return computeFadeSamples (ctx);
}

void DopplerEngine::setManualFade (bool shouldUseManual, double seconds)
{
    useManualFade     = shouldUseManual;
    manualFadeSeconds = seconds;
}

void DopplerEngine::configurePendingSet (Vec3 newPos)
{
    auto& s = geometry.pending();

    // Auf dem letzten bereits geschriebenen Rasterpunkt aufsetzen, nicht auf
    // der (rasterfremden) Blockzeit - sonst läge der nächste push() vor
    // newestTime() und die Zeitachse der Trajektorie wäre nicht mehr monoton.
    const double t = (double) (nextTrajIndex - 1) / trajectoryRateHz;

    // Komplette Vorgeschichte konstant an der neuen Stelle: der neue Satz
    // klingt sofort, statt erst nach der Laufzeit einzusetzen (Plan 2.6/3.2).
    s.trajectory.jumpTo (newPos, t);
    s.lastPos = newPos;

    // Die neue Ohrgeometrie gehört zum neuen Satz; der alte behält seine und
    // friert sie ein (siehe pushTrajectory/process).
    s.listener     = listener;
    s.prevListener = listener;

    // Zweigidentitäten und Filterzustände hängen an der alten Geometrie und
    // dürfen nicht nachgeführt werden (siehe RetardedTimeSolver.h).
    for (auto& p : s.paths)
    {
        p.reset();
        p.setBoomLimitDb (boomLimitDb);
        p.setAirAbsorptionAmount (airAbsorbAmount);
    }
}

void DopplerEngine::startGeometrySwitch (Vec3 newPos, int fadeSamples)
{
    sourceTarget = newPos;
    prevTarget   = newPos;

    if (geometry.isFading() || geometry.queuedSwitchDue())
    {
        // Läuft schon ein Fade: nur anmelden. Der pending()-Satz ist gerade
        // hörbar und darf nicht umkonfiguriert werden - deshalb wartet der
        // neue Zielzustand hier, bis der laufende Fade durch ist (Plan 3.7,
        // Warteschlange der Länge eins, der letzte gewinnt).
        queuedJumpPos = newPos;
        geometry.beginSwitch (fadeSamples);
        return;
    }

    configurePendingSet (newPos);
    geometry.beginSwitch (fadeSamples);
}

void DopplerEngine::jumpSourceTo (Vec3 posMetres)
{
    // Sprungweite ab der zuletzt tatsächlich geschriebenen Position des
    // jüngsten Satzes, nicht ab sourceTarget: bei laufender Bewegung ist das
    // Ziel dem Klang voraus, und gefadet wird über das, was man hört.
    const PathSet& newest = geometry.isFading() ? geometry.pending() : geometry.active();
    const double   delta  = (posMetres - newest.lastPos).length();

    startGeometrySwitch (posMetres, fadeSamplesFor (FadeReason::SourcePosition, delta));
}

void DopplerEngine::setFieldMetres (double metres)
{
    // Schwelle statt Gleichheitsvergleich: dieselbe Reglerstellung darf keinen
    // Fade auslösen, und ein float-Parameter trifft seinen alten Wert beim
    // Hin- und Herrechnen nicht bitgenau.
    if (std::abs (metres - fieldMetres) < 1.0e-9)
        return;

    fieldMetres = metres;

    // Vor prepare() gibt es nichts zu überblenden (und keine Sample-Rate für
    // die Fadedauer) - dann ist das nur ein Wertespeicher.
    if (sr > 0.0)
        fieldChangeArmed = true;
}

void DopplerEngine::applyArmedFieldChange()
{
    if (! fieldChangeArmed)
        return;

    fieldChangeArmed = false;

    // Bewusste Abweichung vom Wortlaut aus Plan 3.7 ("DualPathCrossfader
    // <DopplerEngine> über die komplette Engine"):
    //
    // Positionen werden normiert gespeichert und erst beim Lesen in Meter
    // umgerechnet (Plan 2.1). Eine Feldgrößenänderung ist deshalb - INNERHALB
    // der Engine - exakt und ausschließlich ein Sprung der Geometrie in
    // Metern: Quellposition und Hörerposition wechseln unstetig, sonst
    // nichts. Der Signalpuffer, das Medium und die Puffergrößen (nach
    // maxFieldMetres bemessen, siehe prepare) bleiben unberührt.
    //
    // Genau diesen Sprung blendet der PathSet-Doppelpfad bereits vollständig
    // ab: eigene Trajektorie, eigener Löser mit eigenen Zweigidentitäten,
    // eigene Luftdämpfungs- und Envelope-Zustände, eigene Ohrgeometrie. Eine
    // zweite komplette DopplerEngine würde zusätzlich den Signalpuffer
    // verdoppeln (bei n_max = 10000 zweistellige MB) und einen zweiten
    // Schreiber auf ein Signal setzen, das identisch ist - ohne dass ein
    // einziges Sample anders klänge. Der Aufrufer muss dafür lediglich
    // Feldgröße UND die daraus folgenden Positionen im selben Block setzen,
    // was er ohnehin tut.
    //
    // Der Fade startet erst hier, am Blockanfang, und nicht schon in
    // setFieldMetres(): so ist die Reihenfolge der Setter egal - die neuen
    // Positionen stehen dann in jedem Fall schon.
    startGeometrySwitch (sourceTarget, fadeSamplesFor (FadeReason::FieldSize, 0.0));
}

void DopplerEngine::setBoomLimitDb (double dB)
{
    boomLimitDb = dB;

    for (auto* s : { &geometry.active(), &geometry.pending() })
        for (auto& p : s->paths)
            p.setBoomLimitDb (dB);
}

void DopplerEngine::setAirAbsorptionAmount (double amount01)
{
    airAbsorbAmount = amount01;

    for (auto* s : { &geometry.active(), &geometry.pending() })
        for (auto& p : s->paths)
            p.setAirAbsorptionAmount (amount01);
}

void DopplerEngine::pushTrajectory (double blockStart, double blockEnd)
{
    const double grid = 1.0 / trajectoryRateHz;
    const double span = blockEnd - blockStart;

    if (span <= 0.0)
        return;

    // Ein Rasterpunkt über das Blockende hinaus, damit newestTime() den ganzen
    // Block abdeckt. Sonst friert der Löser die Quelle im letzten Bruchteil
    // einer Millisekunde ein (Randbedingung aus Plan 2.6) und M_r bekäme dort
    // eine falsche Null.
    const double until = blockEnd + grid;

    const bool fading = geometry.isFading();

    // Solange ein Wechsel angemeldet ist, gehört die aktuelle Zielposition
    // schon dem NÄCHSTEN Satz. Der gerade einblendende darf nicht dorthin
    // wandern, sonst zieht er den Sprung als rasende Bewegung nach - man
    // hörte ihn dann doch, nur als Doppler-Schleifer statt als Klick.
    const bool follow = ! geometry.hasQueuedSwitch();

    PathSet& newest    = fading ? geometry.pending() : geometry.active();
    PathSet& fadingOut = geometry.active();

    while ((double) nextTrajIndex * grid <= until)
    {
        const double t = (double) nextTrajIndex * grid;

        // TODO H13: hier gehört der MotionSmoother aus Plan 3.8 hin. Bis
        // dahin wird die Zielposition ungeglättet über den Block linear
        // durchgezogen - der Verlauf ist damit stetig und liefert eine
        // brauchbare Geschwindigkeit, aber er ist keine Glättung.
        const double u   = (t - blockStart) / span;
        const Vec3   pos = follow ? prevTarget + (sourceTarget - prevTarget) * u
                                  : newest.lastPos;

        newest.push (pos, t);

        // Der ausblendende Satz hält seine Position. Er repräsentiert den
        // Zustand VOR dem Sprung; würde er mitlaufen, wäre er kein
        // Vergleichsklang mehr, sondern eine zweite Bewegung.
        if (fading)
            fadingOut.push (fadingOut.lastPos, t);

        ++nextTrajIndex;
    }

    prevTarget = sourceTarget;
}

void DopplerEngine::process (juce::AudioBuffer<float>& stereoOut,
                             const float*              sourceMono,
                             const MediumState&        medium)
{
    const int numSamples = stereoOut.getNumSamples();
    const int numCh      = stereoOut.getNumChannels();

    if (numSamples <= 0 || numCh <= 0 || sr <= 0.0)
        return;

    // 0) Angemeldeter Geometriewechsel zuerst: der laufende Fade ist durch,
    //    jetzt darf pending() neu konfiguriert werden.
    if (geometry.queuedSwitchDue())
    {
        configurePendingSet (queuedJumpPos);
        geometry.startQueuedSwitch();
    }

    applyArmedFieldChange();

    const double blockStart = (double) sampleClock / sr;
    const double blockEnd   = (double) (sampleClock + numSamples) / sr;

    // 1) Quellsignal in den geteilten Ringpuffer. Muss vor den Pfaden
    //    passieren, weil die auf dem vollständigen Datenbestand laufen
    //    (Plan 3.6). Genau ein Schreiber, auch während eines Fades - beide
    //    Geometriesätze lesen dasselbe Signal.
    const float* mono = sourceMono;

    if (mono == nullptr)
    {
        if ((int) silence.size() < numSamples)
            silence.assign ((size_t) numSamples, 0.0f);

        mono = silence.data();
    }

    signal.write (mono, numSamples);

    // 2) Bewegungspfad nachziehen, beide Sätze auf demselben Zeitraster.
    pushTrajectory (blockStart, blockEnd);

    // 3) Blockkontext setzen. Der jüngste Satz übernimmt die aktuelle
    //    Ohrgeometrie, der ausblendende friert seine ein (prevListener ==
    //    listener, also Ohrgeschwindigkeit 0).
    const bool fading = geometry.isFading();
    const bool follow = ! geometry.hasQueuedSwitch();

    auto setContext = [&] (PathSet& s, bool takesNewListener)
    {
        s.prevListener = s.listener;

        if (takesNewListener)
            s.listener = listener;

        s.signal         = &signal;
        s.medium         = &medium;
        s.pathEar        = &pathEar;
        s.blockStartTime = blockStart;
        s.sr             = sr;
    };

    setContext (fading ? geometry.pending() : geometry.active(), follow);

    if (fading)
        setContext (geometry.active(), false);

    // 4) Rendern. Ohne laufenden Fade rendert nur der aktive Satz, direkt in
    //    stereoOut - der zweite kostet dann keine CPU.
    geometry.process (stereoOut);

    // Größere Blöcke als in prepare() angekündigt kann der Doppelpfad nicht
    // bedienen (die Zwischenpuffer stehen fest); der Rest muss still sein
    // statt alten Hostinhalt zu zeigen.
    if (numSamples > maxBlock)
        for (int ch = 0; ch < numCh; ++ch)
            stereoOut.clear (ch, maxBlock, numSamples - maxBlock);

    sampleClock += numSamples;
}
