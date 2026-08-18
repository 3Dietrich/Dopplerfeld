#pragma once

#include <array>
#include <atomic>
#include <cstdint>

// Ringpuffer fuer den Oszilloskop-Scope: der Audiothread schreibt Sample fuer
// Sample (applyOutputStage, NACH Gain+Limiter - dieselbe Stelle wie das
// Levelmeter), der Message-Thread liest im Timer ein Fenster der letzten
// Samples heraus. Anders als FieldSnapshot (Plan 3.12, echte Doppelpufferung
// mit atomarem Objekttausch) reicht hier ein einfacher SPSC-Ring mit
// atomarem Schreibindex: der Leser kopiert sein Fenster heraus und nimmt in
// Kauf, dass der Schreiber waehrenddessen weiterlaeuft - sichtbar hoechstens
// als ein einzelnes verrissenes Sample ganz am Rand des Fensters, genau wie
// bei einem echten Oszilloskop mit laufendem Signal. Feste Groesse, keine
// Allokation im Audiothread.
class ScopeRingBuffer
{
public:
    // Zweierpotenz fuer schnelles Maskieren statt Modulo. Muss groesser sein
    // als das groesste angeforderte Lesefenster (siehe ScopeComponent::
    // captureWindowSamples) plus Sicherheitsabstand.
    static constexpr int capacity = 1 << 14;   // 16384

    void push (float l, float r)
    {
        const std::uint32_t i = writePos.load (std::memory_order_relaxed);
        left[i & mask]  = l;
        right[i & mask] = r;
        writePos.store (i + 1, std::memory_order_release);
    }

    // Kopiert die letzten `count` Samples (chronologisch, aeltestes zuerst)
    // nach destL/destR. count muss <= capacity sein - der Aufrufer haelt
    // sich an captureWindowSamples, das liegt weit darunter.
    void readLatest (float* destL, float* destR, int count) const
    {
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
    static constexpr std::uint32_t mask = (std::uint32_t) capacity - 1;

    std::array<float, capacity> left {};
    std::array<float, capacity> right {};
    std::atomic<std::uint32_t> writePos { 0 };
};
