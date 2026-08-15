#include "SourceTrajectory.h"
#include "Interpolation.h"
#include <algorithm>
#include <cassert>
#include <cmath>

void SourceTrajectory::prepare (double trajRateHz, double maxSeconds)
{
    assert (trajRateHz > 0.0 && maxSeconds > 0.0);

    gridDt = 1.0 / trajRateHz;

    // +4 Rand für den Catmull-Rom-Stencil (zwei Stützstellen vor und nach
    // dem Zielintervall werden gebraucht), damit sampleAt() nahe am Rand
    // nicht extra behandelt werden muss - dort wird stattdessen geklemmt.
    capacity = std::max (8, (int) std::ceil (maxSeconds / gridDt) + 4);

    ring.assign ((size_t) capacity, TrajectorySample{});
    writeIndex = 0;
    speedDeque.clear();
}

int SourceTrajectory::ringSlot (std::int64_t idx) const
{
    const std::int64_t m = idx % capacity;
    return (int) (m < 0 ? m + capacity : m);
}

void SourceTrajectory::pushSpeedSample (double t, double speed)
{
    // Sliding-window-maximum: von hinten alle Einträge verwerfen, die
    // nicht größer als der neue Wert sind - die können ohnehin nie mehr
    // das Maximum stellen, solange der neue Wert im Fenster bleibt.
    while (! speedDeque.empty() && speedDeque.back().second <= speed)
        speedDeque.pop_back();

    speedDeque.push_back ({ t, speed });
}

void SourceTrajectory::fillConstant (Vec3 pos, double newestSampleTime)
{
    // Physisch immer die volle Kapazität beschreiben (analog zum
    // Ringpuffer-Muster aus granular/PluginProcessor.cpp: "Ringpuffer ist
    // physisch immer auf die maximale Länge angelegt"). reset() und
    // jumpTo() legen die komplette Vorgeschichte auf denselben Punkt mit
    // Geschwindigkeit 0, endend exakt auf dem Zeitraster bei
    // newestSampleTime (Plan 2.6).
    speedDeque.clear();
    writeIndex = 0;

    for (int i = 0; i < capacity; ++i)
    {
        TrajectorySample s;
        s.t = newestSampleTime - (double) (capacity - 1 - i) * gridDt;
        s.p = pos;
        s.v = {};
        s.speed = 0.0f;

        ring[(size_t) ringSlot (writeIndex)] = s;
        pushSpeedSample (s.t, 0.0);
        ++writeIndex;
    }
}

void SourceTrajectory::reset (Vec3 initialPos, double startTime)
{
    fillConstant (initialPos, startTime);
}

void SourceTrajectory::jumpTo (Vec3 pos, double time)
{
    fillConstant (pos, time);
}

void SourceTrajectory::push (Vec3 pos, double time)
{
    assert (writeIndex > 0); // reset()/jumpTo() muss vor dem ersten push() laufen

    TrajectorySample s;
    s.t = time;
    s.p = pos;

    const TrajectorySample& prev = ring[(size_t) ringSlot (writeIndex - 1)];
    const double dt = time - prev.t;

    // v wird aus der Differenz zum letzten Punkt berechnet (Plan 3.2). Bei
    // dt <= 0 (doppelter oder rückwärtiger Zeitstempel) die letzte
    // Geschwindigkeit beibehalten statt durch ~0 zu teilen.
    s.v = (dt > 1.0e-9) ? (pos - prev.p) * (1.0 / dt) : prev.v;
    s.speed = (float) s.v.length();

    ring[(size_t) ringSlot (writeIndex)] = s;
    pushSpeedSample (s.t, (double) s.speed);
    ++writeIndex;

    // Einträge verwerfen, die durch das Weiterrücken des Fensters gerade
    // aus der Historie fallen (amortisiert O(1): jeder Eintrag wird genau
    // einmal eingefügt und höchstens einmal entfernt).
    const double oldest = ring[(size_t) ringSlot (writeIndex - capacity)].t;
    while (! speedDeque.empty() && speedDeque.front().first < oldest)
        speedDeque.pop_front();
}

const TrajectorySample& SourceTrajectory::sampleAtClampedIndex (std::int64_t idx) const
{
    const std::int64_t lower = writeIndex - capacity;
    const std::int64_t upper = writeIndex - 1;
    idx = std::min (std::max (idx, lower), upper);
    return ring[(size_t) ringSlot (idx)];
}

bool SourceTrajectory::sampleAt (double t, Vec3& outPos, Vec3& outVel) const
{
    if (writeIndex < capacity)
        return false; // noch nicht per reset()/jumpTo() initialisiert

    if (t < oldestTime() || t > newestTime())
        return false;

    const std::int64_t lower = writeIndex - capacity;
    const std::int64_t upper = writeIndex - 1;

    // Binärsuche über den fortlaufenden Index: die Zeit wächst mit dem
    // Index monoton, solange push() nie mit fallender Zeit aufgerufen wird.
    std::int64_t lo = lower, hi = upper;
    while (lo < hi)
    {
        const std::int64_t mid = lo + (hi - lo + 1) / 2;
        if (ring[(size_t) ringSlot (mid)].t <= t)
            lo = mid;
        else
            hi = mid - 1;
    }

    const std::int64_t k = lo; // größter Index mit t(k) <= t

    if (k >= upper)
    {
        const auto& last = ring[(size_t) ringSlot (upper)];
        outPos = last.p;
        outVel = last.v;
        return true;
    }

    const TrajectorySample& s0 = sampleAtClampedIndex (k - 1);
    const TrajectorySample& s1 = sampleAtClampedIndex (k);
    const TrajectorySample& s2 = sampleAtClampedIndex (k + 1);
    const TrajectorySample& s3 = sampleAtClampedIndex (k + 2);

    const double span = s2.t - s1.t;
    const double frac = (span > 1.0e-12) ? (t - s1.t) / span : 0.0;

    outPos = catmullRom (s0.p, s1.p, s2.p, s3.p, frac);
    outVel = catmullRom (s0.v, s1.v, s2.v, s3.v, frac);
    return true;
}

double SourceTrajectory::maxSpeedInWindow (double t0, double /*t1*/) const
{
    // Verankerung ans laufende Fenster wie im Löser (Plan 2.10):
    // maxSpeedInWindow(t_h - T_max, t_h) - t1 ist in der Praxis immer die
    // aktuelle Schreibzeit, deshalb genügt es, den Vorderrand bis t0
    // nachzuziehen (amortisiert O(1): jeder Eintrag verlässt die Deque
    // höchstens einmal). Mit einem t1 aus der Vergangenheit könnte das
    // Ergebnis theoretisch auch Werte nach t1 mit einschließen.
    while (! speedDeque.empty() && speedDeque.front().first < t0)
        speedDeque.pop_front();

    return speedDeque.empty() ? 0.0 : speedDeque.front().second;
}

double SourceTrajectory::oldestTime() const
{
    return ring[(size_t) ringSlot (writeIndex - capacity)].t;
}

double SourceTrajectory::newestTime() const
{
    return ring[(size_t) ringSlot (writeIndex - 1)].t;
}
