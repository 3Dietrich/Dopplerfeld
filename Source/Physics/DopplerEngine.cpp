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

void DopplerEngine::prepare (double sampleRate, int maxBlockSize, double maxFieldMetres)
{
    sr       = sampleRate;
    maxBlock = std::max (1, maxBlockSize);

    // Längste mögliche Laufzeit: die Felddiagonale ist 1,152·n (Plan 2.1),
    // geteilt durch die langsamste je auftretende Schallgeschwindigkeit
    // (331,3 m/s bei 0 °C), plus Reserve. Bei n = 10000 sind das rund 42 s,
    // was der 40-s-Dimensionierung aus Plan 2.12 entspricht.
    const double diagonal = 1.152 * std::max (1.0, maxFieldMetres);
    maxHistorySeconds = std::max (0.5, diagonal / 331.3 * 1.2 + 0.1);

    signal.prepare (sr, maxHistorySeconds);
    trajectory.prepare (trajectoryRateHz, maxHistorySeconds);

    silence.assign ((size_t) maxBlock, 0.0f);

    // Phase 1: genau zwei Pfade, linkes und rechtes Ohr, beide mit
    // Identity-Transform. Phase 2 hängt hier weitere Einträge an.
    paths.resize (2);
    pathEar = { 0, 1 };

    for (auto& p : paths)
    {
        p.prepare (sr, maxBlock);
        p.setTransform (PathTransform{});
        p.setTrajectoryGridSeconds (1.0 / trajectoryRateHz);
    }

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

    // Vorgeschichte konstant an der aktuellen Zielposition (Plan 2.6): damit
    // existiert von der ersten Probe an mindestens eine Wurzel.
    trajectory.reset (sourceTarget, 0.0);
    prevTarget = sourceTarget;

    prevListener = listener;

    for (auto& p : paths)
        p.reset();
}

void DopplerEngine::jumpSourceTo (Vec3 posMetres)
{
    sourceTarget = posMetres;
    prevTarget   = posMetres;

    // Auf dem letzten bereits geschriebenen Rasterpunkt aufsetzen, nicht auf
    // der (rasterfremden) Blockzeit - sonst läge der nächste push() vor
    // newestTime() und die Zeitachse der Trajektorie wäre nicht mehr monoton.
    trajectory.jumpTo (posMetres, (double) (nextTrajIndex - 1) / trajectoryRateHz);

    // Die Zweigidentitäten und Filterzustände hängen an der alten Geometrie
    // und dürfen nicht nachgeführt werden (siehe RetardedTimeSolver.h).
    for (auto& p : paths)
        p.reset();
}

void DopplerEngine::setBoomLimitDb (double dB)
{
    for (auto& p : paths)
        p.setBoomLimitDb (dB);
}

void DopplerEngine::setAirAbsorptionAmount (double amount01)
{
    for (auto& p : paths)
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

    while ((double) nextTrajIndex * grid <= until)
    {
        const double t = (double) nextTrajIndex * grid;

        // TODO H13: hier gehört der MotionSmoother aus Plan 3.8 hin. In H5
        // wird die Zielposition ungeglättet über den Block linear
        // durchgezogen - der Verlauf ist damit stetig und liefert eine
        // brauchbare Geschwindigkeit, aber er ist keine Glättung.
        const double u   = (t - blockStart) / span;
        const Vec3   pos = prevTarget + (sourceTarget - prevTarget) * u;

        trajectory.push (pos, t);
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

    stereoOut.clear();

    const double blockStart = (double) sampleClock / sr;
    const double blockEnd   = (double) (sampleClock + numSamples) / sr;

    // 1) Quellsignal in den geteilten Ringpuffer. Muss vor den Pfaden
    //    passieren, weil die auf dem vollständigen Datenbestand laufen
    //    (Plan 3.6).
    const float* mono = sourceMono;

    if (mono == nullptr)
    {
        if ((int) silence.size() < numSamples)
            silence.assign ((size_t) numSamples, 0.0f);

        mono = silence.data();
    }

    signal.write (mono, numSamples);

    // 2) Bewegungspfad nachziehen.
    pushTrajectory (blockStart, blockEnd);

    // 3) Ohrgeometrie. Position gilt zu blockStart, die Geschwindigkeit deckt
    //    den Block ab - PropagationPath extrapoliert damit die Ohrposition an
    //    jedem Solver-Punkt (Plan 3.5).
    const double dt = (double) numSamples / sr;

    const Vec3 headVel = (listener.head - prevListener.head) * (1.0 / dt);

    // ω zeigt in z-Richtung. yaw wächst von +y nach +x, also mathematisch
    // negativ - deshalb das Minuszeichen (Herleitung in Listener.h).
    const double yawRate = -wrapAngle (listener.yaw - prevListener.yaw) / dt;

    for (size_t i = 0; i < paths.size(); ++i)
    {
        const bool rightEar = (pathEar[i] != 0);

        const Vec3 pos = earPosition (prevListener, rightEar);
        const Vec3 vel = earVelocity (prevListener, headVel, yawRate, rightEar);

        const int ch = std::min (pathEar[i], numCh - 1);

        paths[i].process (trajectory, signal, medium,
                          pos, vel, blockStart,
                          stereoOut.getWritePointer (ch), numSamples);
    }

    prevListener = listener;
    sampleClock += numSamples;
}
