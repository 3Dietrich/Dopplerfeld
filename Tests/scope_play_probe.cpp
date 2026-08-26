// Messprogramm zum Play-Knopf am Scope (@dpa 20260826: "play an schalten: es
// spielt die Scopeansicht von vorn bis hinten und geht dann auf null. wenn
// play an bleibt, kann man mit der maus im Scope clicken und dadurch an
// bestimmten stellen starten (wieder bis hinten)").
//
// Gibt einen bekannten Ausschnitt in die Wiedergabe und misst am Ausgang des
// Processors, was daraus wird: wann er zu hoeren ist, wann er aufhoert, was
// ein zweiter Start mittendrin macht und was beim Ausschalten passiert.
// Gemessen wird hinter der kompletten Ausgangsstufe, also an dem, was der
// Host bekaeme.
//
// Eigenes Target, nicht Teil von ctest:
//   cmake --build build --target scope_play_probe && build/scope_play_probe

#include "Params.h"
#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int    blockSize  = 512;

int failures = 0;

void check (const char* label, double got, double want, double tolerance)
{
    const bool ok = std::abs (got - want) <= tolerance;

    if (! ok)
        ++failures;

    std::printf ("  %-52s %9.5f  (erwartet %7.5f +-%.3f)  %s\n",
                 label, got, want, tolerance, ok ? "ok" : "FEHLER");
}

// Spielt n Bloecke und liefert den groessten Betrag, der dabei am Ausgang
// stand.
double runBlocks (DopplerfeldProcessor& proc, int blocks)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer         midi;

    double peak = 0.0;

    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            peak = std::max (peak, (double) buffer.getMagnitude (ch, 0, blockSize));
    }

    return peak;
}
}

int main()
{
    DopplerfeldProcessor proc;
    proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
    proc.prepareToPlay (sampleRate, blockSize);

    // Der Ausschnitt, den das Scope "sieht": 0,1 s Sinus, links doppelt so
    // laut wie rechts - daran laesst sich auch die Kanalzuordnung ablesen.
    constexpr int   windowSamples = 4800;
    constexpr float leftAmp       = 0.50f;
    constexpr float rightAmp      = 0.25f;

    std::vector<float> left ((size_t) windowSamples), right ((size_t) windowSamples);

    for (int i = 0; i < windowSamples; ++i)
    {
        const double phase = juce::MathConstants<double>::twoPi * 500.0 * (double) i / sampleRate;
        left [(size_t) i] = leftAmp  * (float) std::sin (phase);
        right[(size_t) i] = rightAmp * (float) std::sin (phase);
    }

    // Bezugspegel: mit den Voreinstellungen laeuft der Motor, am Ausgang
    // steht also etwas. Genau das macht "geht dann auf null" ueberhaupt
    // messbar - die Stille zwischen zwei Abspielvorgaengen ist dann eine
    // Aussage und nicht der Zustand, der ohnehin herrschte.
    const double idlePeak = runBlocks (proc, 20);

    std::printf ("Ausgang ohne Play (Bezug):\n");
    check ("Dopplersignal hoerbar", idlePeak > 0.05 ? 1.0 : 0.0, 1.0, 0.0);
    std::printf ("  %-52s %9.5f\n", "Spitzenwert", idlePeak);

    // 1) Play einschalten = der sichtbare Ausschnitt laeuft einmal durch.
    std::printf ("\nPlay an, ganzer Ausschnitt (0,1 s):\n");

    proc.setScopePlaybackModeEnabled (true);
    proc.requestScopePlayback (left.data(), right.data(), windowSamples);

    // 4800 Samples = 9,375 Bloecke; 10 Bloecke decken sie ab.
    check ("Spitzenwert waehrend der Wiedergabe", runBlocks (proc, 10), (double) leftAmp, 0.02);

    // 2) Danach Stille, obwohl der Toggle an bleibt ("geht dann auf null").
    check ("Spitzenwert danach (Toggle bleibt an)", runBlocks (proc, 20), 0.0, 1.0e-4);

    // 3) Klick im Scope: ab der halben Strecke bis hinten - dieselbe Anfrage,
    // nur mit verschobenem Zeiger und kuerzerer Laenge, genau das reicht der
    // Editor beim Klick ein.
    std::printf ("\nKlick auf die Mitte, Rest bis hinten:\n");

    constexpr int clickOffset = windowSamples / 2;

    proc.requestScopePlayback (left.data() + clickOffset, right.data() + clickOffset,
                               windowSamples - clickOffset);

    check ("Spitzenwert waehrend der Wiedergabe", runBlocks (proc, 6), (double) leftAmp, 0.02);
    check ("Spitzenwert danach", runBlocks (proc, 20), 0.0, 1.0e-4);

    // 4) Ausschalten: der Ausgang gehoert wieder dem Dopplersignal. Ohne
    // Quelle ist das die Stille von oben - gemessen wird hier, dass die
    // Umschaltung selbst nichts stehen laesst.
    std::printf ("\nPlay aus:\n");

    proc.setScopePlaybackModeEnabled (false);

    // Verglichen wird die Groessenordnung, nicht der Wert: das Dopplersignal
    // ist ein laufender Motor, sein Spitzenwert ist von Block zu Block ein
    // anderer.
    const double afterOffPeak = runBlocks (proc, 20);

    check ("Ausgang wieder da (Anteil des Bezugspegels)",
           afterOffPeak / juce::jmax (1.0e-9, idlePeak), 1.0, 0.35);

    // 5) Mitten hinein noch einmal starten: der laufende Vorgang wird
    // ausgeblendet und der neue uebernimmt, ohne dass der Ausgang dabei
    // ueber den Ausschnitt hinausschlaegt (kein harter Quellwechsel).
    std::printf ("\nZweiter Start mitten in die laufende Wiedergabe:\n");

    proc.setScopePlaybackModeEnabled (true);
    proc.requestScopePlayback (left.data(), right.data(), windowSamples);
    runBlocks (proc, 3);

    proc.requestScopePlayback (left.data(), right.data(), windowSamples);

    check ("Spitzenwert beim Umschalten", runBlocks (proc, 12), (double) leftAmp, 0.02);
    check ("Spitzenwert danach", runBlocks (proc, 20), 0.0, 1.0e-4);

    std::printf ("\n%s\n", failures == 0 ? "alles ok" : "FEHLER, siehe oben");
    return failures == 0 ? 0 : 1;
}
