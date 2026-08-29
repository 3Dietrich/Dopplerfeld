// Messprogramm zur "Fahne" (@dpa 20260829: "prueft mal bitte ob das nicht doch
// eine falsche Berechnung ist..?").
//
// Faehrt ein Preset genau so, wie es der Zustandsstreifen laedt, und schreibt
// waehrenddessen Block fuer Block mit, WAS gerade zu hoeren ist: Ausgangspegel,
// Entfernung, Mach, und je Hoerweg die Zweige mit ihrer Verzoegerung und ihrem
// M_r. Damit laesst sich die Frage beantworten, ob das laute Plateau nach dem
// Knall ein zusaetzlicher Zweig ist - und ob dessen Zahlenwerte zur Geometrie
// passen oder nicht.
//
// Eigenes Target, nicht Teil von ctest:
//   cmake --build build --target trail_probe && build/trail_probe [Preset] [Sekunden]

#include "Params.h"
#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int    blockSize  = 512;

juce::File presetFile (const juce::String& name)
{
    return juce::File (DOPPLERFELD_SOURCE_DIR).getChildFile ("presets").getChildFile (name);
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const double seconds = argc > 1 ? std::atof (argv[1]) : 12.0;
    const double mach    = argc > 2 ? std::atof (argv[2]) : 1.31;
    const double side    = argc > 3 ? std::atof (argv[3]) : 300.0;

    DopplerfeldProcessor proc;

    proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
    proc.prepareToPlay (sampleRate, blockSize);

    auto set = [&proc] (const char* id, float value)
    {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    // Gerader Vorbeiflug, alles andere abgeschaltet: nur der Direktschall,
    // damit die Huellkurve wirklich die Summe der Hoerwege ist und nicht
    // zusaetzlich Boden, Waende oder Hall.
    const double c = 343.0;

    set (Params::fieldMetres,     6000.0f);
    set (Params::flyKind,         1.0f);            // waagerecht querend
    set (Params::flySpeed,        (float) (mach * c));
    set (Params::flyDistance,     (float) side);
    set (Params::flyApproach,     3000.0f);
    set (Params::groundReflectionOn, 0.0f);
    set (Params::reverbBypass,    1.0f);
    set (Params::nWaveOn,         0.0f);            // ohne Knall: nur der Traegerton
    set (Params::extraPathGainDb, 0.0f);            // Fahne voll auf
    set (Params::distanceCurve,   0.0f);            // 1/R, der physikalische Fall
    set (Params::airAbsorbAmount, 0.0f);
    set (Params::shockDuckAmount, 0.0f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);
    }

    proc.triggerFlyBy();

    const int blocks = (int) (seconds * sampleRate / blockSize);

    std::printf ("Vorbeiflug Mach %.2f, Abstand %.0f m, Fahne 0 dB, 1/R, ohne N-Welle\n\n", mach, side);
    std::printf ("%6s %8s %8s %7s %6s %5s %10s %10s %8s %8s %8s  %s\n",
                 "t(s)", "rms(dB)", "peak(dB)", "R(m)", "Mach", "Zwg",
                 "sx", "sy", "sz", "lx", "ly", "Zweige: dTau(ms)/M_r");

    FieldSnapshot snap;

    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);

        const double t = (double) b * blockSize / sampleRate;

        double sum = 0.0, peak = 0.0;

        for (int i = 0; i < blockSize; ++i)
        {
            const double v = 0.5 * ((double) buffer.getSample (0, i) + (double) buffer.getSample (1, i));
            sum += v * v;
            peak = std::max (peak, std::abs (v));
        }

        const double rms = std::sqrt (sum / blockSize);

        proc.fillFieldSnapshot (snap);

        int  branches = 0;
        char detail[512];
        int  used = 0;

        for (int p = 0; p < snap.pathCount && used < 400; ++p)
        {
            const auto& info = snap.paths[(size_t) p];

            if (info.surface != 0 || info.ear != 0)
                continue;

            branches += info.activeBranches;

            used += std::snprintf (detail + used, sizeof detail - (size_t) used,
                                   "%.1f/%.3f ", info.delaySeconds * 1000.0, info.machRadial);
        }

        detail[used] = 0;

        const double R = (snap.sourcePos - snap.listener.head).length();

        std::printf ("%6.3f %8.1f %8.1f %7.0f %6.2f %5d %10.2f %10.2f %8.2f %8.2f %8.2f  %s\n",
                     t, 20.0 * std::log10 (std::max (1.0e-9, rms)),
                     20.0 * std::log10 (std::max (1.0e-9, peak)), R,
                     snap.sourceSpeed / std::max (1.0, snap.speedOfSound),
                     branches,
                     snap.sourcePos.x, snap.sourcePos.y, snap.sourcePos.z,
                     snap.listener.head.x, snap.listener.head.y,
                     detail);
    }

    return 0;
}
