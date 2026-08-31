// Messprogramm zur Knall-Ansicht des Scopes (@dpa 20260830: "Wenn ich einen
// Play Loop ablaufen lasse und dann die Knalle anschauen will - dafuer die
// 'Knall' Ansicht. Das muss sich zuverlaessig updaten, incl. zoomen").
//
// Fuettert die ScopeComponent so, wie es der Editor-Timer tut - alle 33 ms ein
// Rohfenster aus einem Ringpuffer - und zaehlt, wie viele der eingespeisten
// Knalle wirklich im Bild landen. Einmal je Zoomstufe, denn genau dort war die
// Frage.
//
//   cmake --build build --target scope_boom_probe && build/scope_boom_probe

#include "UI/ScopeComponent.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;

// Eine Sekunde Signal mit Knallen im festen Abstand, sonst leise.
std::vector<float> makeSignal (double seconds, double boomEverySeconds, double quietLevel)
{
    const int total = (int) (seconds * sampleRate);

    std::vector<float> x ((size_t) total, 0.0f);

    // Grundrauschen: das Bild ist im Betrieb nie ganz still, und der langsame
    // Huellkurvenfolger braucht etwas, wogegen er den Anstieg misst.
    unsigned int seed = 12345u;

    for (int n = 0; n < total; ++n)
    {
        seed = seed * 1664525u + 1013904223u;

        x[(size_t) n] = (float) quietLevel * (2.0f * ((float) (seed >> 8) / 16777216.0f) - 1.0f);
    }

    // Die Knalle selbst: eine kurze N-Welle, wie sie das Plugin erzeugt.
    const int period = (int) (boomEverySeconds * sampleRate);
    const int length = (int) (0.0004 * sampleRate);

    for (int start = period; start + length < total; start += period)
        for (int n = 0; n < length; ++n)
            x[(size_t) (start + n)] = (n < length / 2) ? 0.8f : -0.8f;

    return x;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf ("scope_boom_probe - findet die Knall-Ansicht ihre Knalle?\n\n");

    constexpr double boomEvery   = 0.5;
    constexpr double quietLevel  = 0.02;

    std::printf ("Knalle im Abstand von %.0f ms, Grundrauschen %.3f\n\n",
                 boomEvery * 1000.0, quietLevel);

    std::printf ("%12s %10s %8s %8s\n", "Zoom", "Fenster", "gezeigt", "von");

    bool failed = false;

    for (int displaySamples : { 128, 512, 2048, 8192, 32768, 131072 })
    {
        // Das Signal muss deutlich laenger sein als das Bild, sonst fehlt den
        // spaeten Knallen schlicht der Nachlauf.
        const double seconds = juce::jmax (6.0, displaySamples / sampleRate * 8.0);

        const auto signal   = makeSignal (seconds, boomEvery, quietLevel);
        const int  expected = (int) (seconds / boomEvery) - 1;

        ScopeComponent scope;

        scope.setSize (700, 200);
        scope.setSampleRateHint (sampleRate);
        scope.setMaxDisplaySampleCount (1 << 20);
        scope.setDisplaySeconds (displaySamples / sampleRate, sampleRate);
        scope.setEventTriggerEnabled (true);
        scope.setHoldSeconds (0.0);          // ohne Haltezeit: jeder Knall darf ins Bild

        const int captureLen = scope.captureWindowSampleCount();

        // Der Editor liefert alle 33 ms ein Fenster, das am aktuellen
        // Schreibstand endet.
        const int step = (int) (0.033 * sampleRate);

        int shown = 0;

        std::vector<float> window ((size_t) captureLen, 0.0f);

        for (int end = captureLen; end < (int) signal.size(); end += step)
        {
            for (int n = 0; n < captureLen; ++n)
                window[(size_t) n] = signal[(size_t) (end - captureLen + n)];

            const int before = scope.triggerCountForTest();

            scope.feed (window.data(), window.data(), (std::uint32_t) end);

            if (scope.triggerCountForTest() > before)
                ++shown;
        }

            // Zwei Fehlerbilder, beide schon dagewesen: gar nichts mehr zeigen
        // (bei grosser Zeitbasis) und auf Rauschen zappeln (bei kleiner).
        // Deshalb eine Unter- UND eine Obergrenze.
        const int floorShown = juce::jmax (5, expected / 4);
        const int ceilShown  = expected * 2;

        const bool ok = shown >= floorShown && shown <= ceilShown;

        if (! ok)
            failed = true;

        std::printf ("%9d sp %10d %8d %8d   %s\n",
                     displaySamples, captureLen, shown, expected,
                     ok ? "ok" : (shown < floorShown ? "FEHLGESCHLAGEN: zu selten"
                                                     : "FEHLGESCHLAGEN: zappelt"));
    }

    std::printf (failed ? "\nFEHLGESCHLAGEN\n" : "\nOK\n");

    return failed ? 1 : 0;
}
