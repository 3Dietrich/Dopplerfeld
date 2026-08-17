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

void SourceTrajectory::pushDistanceSample (double t, double distance)
{
    while (! distDeque.empty() && distDeque.back().second <= distance)
        distDeque.pop_back();

    distDeque.push_back ({ t, distance });
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
    distDeque.clear();
    writeIndex = 0;

    const double posNorm = pos.length();

    for (int i = 0; i < capacity; ++i)
    {
        TrajectorySample s;
        s.t = newestSampleTime - (double) (capacity - 1 - i) * gridDt;
        s.p = pos;
        s.v = {};
        s.speed = 0.0f;

        ring[(size_t) ringSlot (writeIndex)] = s;
        pushSpeedSample (s.t, 0.0);
        pushDistanceSample (s.t, posNorm);
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

void SourceTrajectory::fillLinear (Vec3 pos, Vec3 vel, double time, double spanSeconds)
{
    speedDeque.clear();
    distDeque.clear();
    writeIndex = 0;

    const double speed = vel.length();
    const double span  = std::max (0.0, spanSeconds);

    for (int i = 0; i < capacity; ++i)
    {
        const double t     = time - (double) (capacity - 1 - i) * gridDt;
        const double along = std::max (t - time, -span);
        const bool   moving = (t - time) >= -span;

        TrajectorySample s;

        s.t     = t;
        s.p     = pos + vel * along;
        s.v     = moving ? vel : Vec3{};
        s.speed = moving ? (float) speed : 0.0f;

        ring[(size_t) ringSlot (writeIndex)] = s;
        pushSpeedSample (s.t, (double) s.speed);
        pushDistanceSample (s.t, s.p.length());
        ++writeIndex;
    }
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
    pushDistanceSample (s.t, pos.length());
    ++writeIndex;

    // Einträge verwerfen, die durch das Weiterrücken des Fensters gerade
    // aus der Historie fallen (amortisiert O(1): jeder Eintrag wird genau
    // einmal eingefügt und höchstens einmal entfernt).
    const double oldest = ring[(size_t) ringSlot (writeIndex - capacity)].t;

    while (! speedDeque.empty() && speedDeque.front().first < oldest)
        speedDeque.pop_front();

    while (! distDeque.empty() && distDeque.front().first < oldest)
        distDeque.pop_front();
}

const TrajectorySample& SourceTrajectory::sampleAtClampedIndex (std::int64_t idx) const
{
    const std::int64_t lower = writeIndex - capacity;
    const std::int64_t upper = writeIndex - 1;
    idx = std::min (std::max (idx, lower), upper);
    return ring[(size_t) ringSlot (idx)];
}

std::int64_t SourceTrajectory::indexBefore (double t) const
{
    const std::int64_t lower = writeIndex - capacity;
    const std::int64_t upper = writeIndex - 1;

    // Das Raster ist gleichförmig: fillConstant() legt es so an, und push()
    // bekommt seine Zeiten aus einem ganzzahligen Rasterzähler (siehe
    // DopplerEngine::pushTrajectory). Der Index lässt sich deshalb direkt
    // ausrechnen, statt ihn über den ganzen Puffer zu suchen - bei 42 s
    // Historie waren das rund 16 Binärsuchschritte, jeder mit einer
    // Ganzzahldivision im Ringzugriff und einem Sprung an eine andere Stelle
    // des Megabyte-Puffers. Das ist die häufigste Einzeloperation des Lösers.
    const double tLower = ring[(size_t) ringSlot (lower)].t;

    std::int64_t k = lower + (std::int64_t) std::floor ((t - tLower) / gridDt);
    k = std::min (std::max (k, lower), upper);

    // Korrektur für Rundung am Rasterpunkt - und Sicherheitsnetz, falls doch
    // einmal ungleichmäßig geschrieben wurde. Zwei Schritte reichen für den
    // gleichförmigen Fall; wer mehr braucht, bekommt die Binärsuche.
    int guard = 0;

    while (k > lower && ring[(size_t) ringSlot (k)].t > t && guard < 2)
    {
        --k;
        ++guard;
    }

    while (k < upper && ring[(size_t) ringSlot (k + 1)].t <= t && guard < 2)
    {
        ++k;
        ++guard;
    }

    if (guard >= 2)
    {
        std::int64_t lo = lower, hi = upper;

        while (lo < hi)
        {
            const std::int64_t mid = lo + (hi - lo + 1) / 2;

            if (ring[(size_t) ringSlot (mid)].t <= t)
                lo = mid;
            else
                hi = mid - 1;
        }

        k = lo;
    }

    return k;
}

bool SourceTrajectory::sampleAt (double t, Vec3& outPos, Vec3& outVel) const
{
    if (writeIndex < capacity)
        return false; // noch nicht per reset()/jumpTo() initialisiert

    if (t < oldestTime() || t > newestTime())
        return false;

    const std::int64_t upper = writeIndex - 1;
    const std::int64_t k     = indexBefore (t);

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

bool SourceTrajectory::samplePositionAt (double t, Vec3& outPos) const
{
    if (writeIndex < capacity)
        return false;

    if (t < oldestTime() || t > newestTime())
        return false;

    const std::int64_t upper = writeIndex - 1;
    const std::int64_t k     = indexBefore (t);

    if (k >= upper)
    {
        outPos = ring[(size_t) ringSlot (upper)].p;
        return true;
    }

    const TrajectorySample& s0 = sampleAtClampedIndex (k - 1);
    const TrajectorySample& s1 = sampleAtClampedIndex (k);
    const TrajectorySample& s2 = sampleAtClampedIndex (k + 1);
    const TrajectorySample& s3 = sampleAtClampedIndex (k + 2);

    const double span = s2.t - s1.t;
    const double frac = (span > 1.0e-12) ? (t - s1.t) / span : 0.0;

    outPos = catmullRom (s0.p, s1.p, s2.p, s3.p, frac);
    return true;
}

double SourceTrajectory::maxSince (const std::deque<std::pair<double, double>>& d, double t0)
{
    // Die Deque enthält genau die Einträge, die von keinem späteren Eintrag
    // dominiert werden; ihre Werte fallen deshalb von vorn nach hinten streng.
    // Der Größte im Fenster [t0, jetzt] ist damit der erste Eintrag mit
    // Zeit >= t0 - binär gesucht statt vorne weggeworfen, damit die Deque für
    // den nächsten Frager mit anderem (älterem) t0 vollständig bleibt.
    //
    // Dass der Eintrag stellvertretend für alle weggeworfenen steht, ist die
    // Invariante des Verfahrens: ein Eintrag fliegt nur raus, wenn ein SPÄTERER
    // mindestens so groß ist - der liegt dann erst recht im Fenster.
    const auto it = std::lower_bound (d.begin(), d.end(), t0,
                                      [] (const std::pair<double, double>& e, double t)
                                      { return e.first < t; });

    return it == d.end() ? 0.0 : it->second;
}

double SourceTrajectory::maxSpeedSince (double t0) const
{
    return maxSince (speedDeque, t0);
}

double SourceTrajectory::maxDistanceSince (double t0) const
{
    return maxSince (distDeque, t0);
}

double SourceTrajectory::oldestTime() const
{
    return ring[(size_t) ringSlot (writeIndex - capacity)].t;
}

double SourceTrajectory::newestTime() const
{
    return ring[(size_t) ringSlot (writeIndex - 1)].t;
}
