// Messprogramm zur Live-Bewegung (@dpa 20260830: "Live bewegen ist ganz schoen
// kantig (Mausglatt an/aus egal). recorded ist es dann glatt, aber Live hoert
// es sich sprunghaft an").
//
// Zieht die Quelle wie unter der Maus - neue Position nur im Bildtakt, wie sie
// aus der Oberflaeche kommt - und schreibt den Mitschnitt als WAV. Aus dem
// Tonhoehenverlauf laesst sich danach ablesen, wie kantig die Bewegung im
// Doppler ankommt.
//
//   cmake --build build --target live_probe && build/live_probe [s] [Raster ms] [aus]
//
// "aus" nimmt dieselben Namen wie pitch_probe: accel schaltet das Gas aus der
// Beschleunigung ab, verb den Nachhall.

#include "Params.h"
#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int    blockSize  = 512;
constexpr double fieldMetres = 1000.0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const double seconds   = argc > 1 ? std::atof (argv[1]) : 4.0;
    const double rasterMs  = argc > 2 ? std::atof (argv[2]) : 1000.0 / 60.0;
    const juce::String off = argc > 3 ? juce::String (argv[3]) : juce::String();
    const double throttleTau = argc > 4 ? std::atof (argv[4]) : 0.3;

    DopplerfeldProcessor proc;
    proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
    proc.prepareToPlay (sampleRate, blockSize);

    auto set = [&proc] (const juce::String& id, float value)
    {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    set (Params::fieldMetres, (float) fieldMetres);
    set (Params::srcJitterOn, 0.0f);
    set (Params::rpm, 3000.0f);
    set (Params::engineKind, 0.0f);
    set (Params::throttleFromAccel, off.contains ("accel") ? 0.0f : 100.0f);
    set (Params::throttleTau, (float) throttleTau);

    if (off.contains ("verb"))
        set (Params::reverbBypass, 1.0f);

    // Der Motor wird auf EINEN Sinus reduziert: dann bildet die Frequenz des
    // Mitschnitts die Drehzahl (mal Doppler) exakt ab, und die Messung
    // draussen braucht keine Annahmen ueber Teiltoene, Rauschband oder Wind.
    set (Params::harmSine[0], 1.0f);
    set (Params::harmLevel1, 0.0f);
    set (Params::harmRatio1, 1.0f);

    set (Params::harmLevel2, -120.0f);
    set (Params::harmLevel3, -120.0f);
    set (Params::harmLevel4, -120.0f);

    set (Params::noiseGainLo, -120.0f);
    set (Params::noiseGainHi, -120.0f);
    set (Params::windLevelDb,   -36.0f);
    set (Params::noiseSpeedAmount, 0.0f);

    set (Params::srcX, 0.15f);
    set (Params::srcY, 0.5f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer         midi;

    // Einschwingen lassen, damit der Mitschnitt nicht mit dem Aufbau beginnt.
    for (int i = 0; i < 60; ++i)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);
    }

    const double blockSeconds  = (double) blockSize / sampleRate;
    const int    blocks        = (int) (seconds / blockSeconds);
    const double dragSpeed     = 40.0;   // m/s, ein zuegiger Zug quer durchs Feld

    juce::AudioBuffer<float> recorded (1, blocks * blockSize);
    recorded.clear();

    double elapsed  = 0.0;
    double lastSent = -1.0;
    double normX    = 0.15;

    for (int b = 0; b < blocks; ++b)
    {
        // Die Oberflaeche meldet nur im Bildtakt eine neue Stelle; dazwischen
        // steht der Parameter still. Genau dieses Raster steckt in der
        // Live-Bewegung.
        if (lastSent < 0.0 || (elapsed - lastSent) * 1000.0 >= rasterMs)
        {
            normX = juce::jlimit (0.0, 1.0, 0.15 + dragSpeed * elapsed / fieldMetres);
            set (Params::srcX, (float) normX);
            lastSent = elapsed;
        }

        buffer.clear();
        proc.processBlock (buffer, midi);
        recorded.copyFrom (0, b * blockSize, buffer, 0, 0, blockSize);

        elapsed += blockSeconds;
    }

    const auto out = juce::File (DOPPLERFELD_SOURCE_DIR).getChildFile ("build")
                         .getChildFile ("live_probe_tau" + juce::String (throttleTau, 3)
                                        + (off.isEmpty() ? juce::String() : "_ohne_" + off) + ".wav");
    out.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream (out.createOutputStream());

    if (stream != nullptr)
    {
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.release(), sampleRate, 1, 32, {}, 0));

        if (writer != nullptr)
        {
            writer->writeFromAudioSampleBuffer (recorded, 0, recorded.getNumSamples());
            writer.reset();
        }
    }

    std::printf ("Zug %.0f m/s, Raster %.1f ms, %.1f s, Gas-Traegheit %.3f s, abgeschaltet: %s\n",
                 dragSpeed, rasterMs, seconds, throttleTau, off.isEmpty() ? "nichts" : off.toRawUTF8());
    std::printf ("Mitschnitt %s\n", out.getFullPathName().toRawUTF8());

    return 0;
}
