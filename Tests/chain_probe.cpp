// Messprogramm zur Hallkette (@dpa 20260830: "geht es, dass man einen Hall
// (z.B. 3: Draussen) in den folgenden (z.B. 4: Diffusor) direkt schalten
// kann ... so koennte man komplexe Hallversionen kreieren").
//
// Stellt den Direktschall stumm, sodass am Ausgang NUR der Hall der
// Abgriffpunkte steht, und misst dessen Pegel - einmal mit zwei Punkten
// nebeneinander, einmal mit demselben Paar als Kette. Der scharfe Nachweis
// steht darunter: wird der zweite Punkt stumm gestellt, muss als Kette fast
// nichts mehr herauskommen (alles laeuft durch ihn), nebeneinander dagegen
// bleibt der erste unveraendert hoerbar.
//
//   cmake --build build --target chain_probe && build/chain_probe

#include "Params.h"
#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int    blockSize  = 512;

struct Result
{
    double rms = 0.0;   // Pegel des Hallanteils am Ausgang
};

Result run (bool chained, bool muteSecond = false)
{
    DopplerfeldProcessor proc;
    proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
    proc.prepareToPlay (sampleRate, blockSize);

    auto set = [&proc] (const juce::String& id, float value)
    {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    using namespace Params::TapPart;

    set (Params::srcJitterOn, 0.0f);
    set (Params::rpm, 2000.0f);

    // Direktschall aus: was dann noch am Ausgang steht, ist ausschliesslich
    // der Hall der Abgriffpunkte.
    set (Params::directGain, -60.0f);
    set (Params::reverbBypass, 0.0f);

    // Zwei Punkte, verschiedene Bauarten: Draussen zuerst, dann der Diffusor -
    // dasselbe Paar, das @dpa als Beispiel genannt hat.
    for (int t = 2; t <= 3; ++t)
    {
        set (Params::tapId (t, on), 1.0f);
        set (Params::tapId (t, gain), -6.0f);
        set (Params::tapId (t, room), 40.0f);
        set (Params::tapId (t, decay), 2.5f);
    }

    set (Params::tapId (2, type), 3.0f);   // Draussen
    set (Params::tapId (3, type), 0.0f);   // Diffusor

    // Kette: Punkt 3 (Index 2) geht in Punkt 4 (Index 3) - der naechste, also
    // Auswahl 1.
    set (Params::tapId (2, chain), chained ? 1.0f : 0.0f);

    // Schaerfster Nachweis: den ZWEITEN stumm stellen. Als Kette laeuft alles
    // durch ihn, es darf also fast nichts mehr herauskommen; nebeneinander
    // bleibt der erste unveraendert hoerbar.
    if (muteSecond)
        set (Params::tapId (3, gain), -60.0f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    const double blockSeconds = (double) blockSize / sampleRate;
    const int    settleBlocks = (int) (2.0 / blockSeconds);
    const int    measureBlocks = (int) (2.0 / blockSeconds);

    for (int b = 0; b < settleBlocks; ++b)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);
    }

    double sum = 0.0;
    long long count = 0;

    for (int b = 0; b < measureBlocks; ++b)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);

        for (int i = 0; i < blockSize; ++i)
        {
            const double l = buffer.getSample (0, i);
            const double r = buffer.getSample (1, i);
            sum += 0.5 * (l * l + r * r);
            count += 1;
        }
    }

    Result result;
    result.rms = count > 0 ? std::sqrt (sum / (double) count) : 0.0;

    return result;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const auto apart   = run (false);
    const auto chained = run (true);
    const auto apartMuted   = run (false, true);
    const auto chainedMuted = run (true,  true);

    auto db = [] (double a, double b) { return 20.0 * std::log10 (std::max (1.0e-12, a)
                                                                / std::max (1.0e-12, b)); };

    std::printf ("Nur der Hall der Punkte 3 und 4 (Direktschall stumm):\n");
    std::printf ("  nebeneinander            RMS %.6f\n", apart.rms);
    std::printf ("  als Kette                RMS %.6f  (%+.1f dB)\n", chained.rms,
                 db (chained.rms, apart.rms));
    std::printf ("  zweiter stumm, nebeneinander RMS %.6f  (%+.1f dB)\n", apartMuted.rms,
                 db (apartMuted.rms, apart.rms));
    std::printf ("  zweiter stumm, als Kette     RMS %.6f  (%+.1f dB)  <- muss fast still sein\n",
                 chainedMuted.rms, db (chainedMuted.rms, apart.rms));

    return 0;
}
