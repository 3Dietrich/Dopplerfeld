#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

// Ringpuffer fuer den Oszilloskop-Scope: der Audiothread schreibt Sample fuer
// Sample (applyOutputStage, NACH Gain+Limiter - dieselbe Stelle wie das
// Levelmeter), der Message-Thread liest im Timer ein Fenster der letzten
// Samples heraus. Anders als FieldSnapshot (Plan 3.12, echte Doppelpufferung
// mit atomarem Objekttausch) reicht hier ein einfacher SPSC-Ring mit
// atomarem Schreibindex: der Leser kopiert sein Fenster heraus und nimmt in
// Kauf, dass der Schreiber waehrenddessen weiterlaeuft - sichtbar hoechstens
// als ein einzelnes verrissenes Sample ganz am Rand des Fensters, genau wie
// bei einem echten Oszilloskop mit laufendem Signal.
//
// Groesse haengt von der Samplerate ab (@dpa-Feedback: Zoom bis ~3s Zeit-
// basis), deshalb keine feste Array-Groesse mehr - prepare() allokiert
// einmalig aus prepareToPlay() (das darf das, Message-Thread), push()/
// readLatest() allokieren nie.
class ScopeRingBuffer
{
public:
    // Reserviert Platz fuer mindestens maxSeconds Audio bei sampleRate,
    // aufgerundet auf die naechste Zweierpotenz (schnelles Maskieren statt
    // Modulo). Nur aus prepareToPlay() (Message-Thread) - danach schreibt/
    // liest nur noch mit fester Groesse.
    void prepare (double sampleRate, double maxSeconds)
    {
        const std::uint32_t needed = (std::uint32_t) std::ceil (std::max (1.0, sampleRate * maxSeconds));

        std::uint32_t cap = 1;
        while (cap < needed)
            cap <<= 1;

        left.assign (cap, 0.0f);
        right.assign (cap, 0.0f);
        mask = cap - 1;
        writePos.store (0, std::memory_order_relaxed);
    }

    int capacity() const { return (int) left.size(); }

    void push (float l, float r)
    {
        if (left.empty())   // prepare() noch nicht gelaufen - sollte laut JUCE-Vertrag nicht vorkommen
            return;

        const std::uint32_t i = writePos.load (std::memory_order_relaxed);
        left[i & mask]  = l;
        right[i & mask] = r;
        writePos.store (i + 1, std::memory_order_release);
    }

    // Kopiert die letzten `count` Samples (chronologisch, aeltestes zuerst)
    // nach destL/destR. count muss <= capacity() sein - der Aufrufer haelt
    // sich an das per prepare() zugesagte Maximum.
    void readLatest (float* destL, float* destR, int count) const
    {
        if (left.empty() || count <= 0)
            return;

        const std::uint32_t end   = writePos.load (std::memory_order_acquire);
        const std::uint32_t start = end - (std::uint32_t) count;

        for (int n = 0; n < count; ++n)
        {
            const std::uint32_t idx = (start + (std::uint32_t) n) & mask;
            destL[n] = left[idx];
            destR[n] = right[idx];
        }
    }

private:
    std::vector<float> left, right;
    std::uint32_t mask = 0;
    std::atomic<std::uint32_t> writePos { 0 };
};
