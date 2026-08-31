// Messprogramm zu springenden Tonhoehen (@dpa 20260830: "die Pitches
// springen.. das ist ein starker Rueckschritt").
//
// Laedt ein Preset, rendert es und schreibt den Mitschnitt als WAV, damit sich
// der Tonhoehenverlauf ausserhalb messen laesst. Einzelne Verursacher lassen
// sich abschalten, um sie zu trennen:
//
//   accel    Gas aus der Beschleunigung (throttleFromAccel = 0)
//   jitter   Wackler der Quelle (srcJitterOn = 0)
//   rjitter  Wackler der Drehzahl (jitterAmount = 0)
//   verb     Nachhall (reverbBypass)
//
// Zusaetzlich schaltet "smooth" den Wackler DURCH die Bewegungsglaettung
// (srcJitterSmooth), statt etwas abzuschalten.
//
//   cmake --build build --target pitch_probe && build/pitch_probe [Preset] [s] [aus,aus,...]

#include "Params.h"
#include "PluginProcessor.h"

#include <cstdio>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int    blockSize  = 512;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String name = argc > 1 ? juce::String::fromUTF8 (argv[1]) : juce::String ("BugPitchBigbug");
    const double seconds    = argc > 2 ? std::atof (argv[2]) : 6.0;
    const juce::String off  = argc > 3 ? juce::String (argv[3]) : juce::String();

    const juce::File f = juce::File (DOPPLERFELD_SOURCE_DIR).getChildFile ("presets").getChildFile (name);

    juce::MemoryBlock block;

    if (! f.existsAsFile() || ! f.loadFileAsData (block))
    {
        std::printf ("FEHLT: %s\n", f.getFullPathName().toRawUTF8());
        return 1;
    }

    DopplerfeldProcessor proc;

    proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
    proc.prepareToPlay (sampleRate, blockSize);
    proc.setStateInformation (block.getData(), (int) block.getSize());

    auto set = [&proc] (const juce::String& id, float value)
    {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    if (off.contains ("accel"))   set (Params::throttleFromAccel, 0.0f);
    if (off.contains ("jitter"))  set (Params::srcJitterOn, 0.0f);
    if (off.contains ("rjitter")) set (Params::jitterAmount, 0.0f);
    if (off.contains ("verb"))    set (Params::reverbBypass, 1.0f);
    if (off.contains ("smooth"))  set (Params::srcJitterSmooth, 1.0f);

    auto show = [&proc] (const juce::String& id)
    {
        if (const auto* v = proc.apvts.getRawParameterValue (id))
            std::printf ("  %-20s %.3f\n", id.toRawUTF8(), (double) v->load());
    };

    std::printf ("Preset %s, %.1f s, abgeschaltet: %s\n",
                 name.toRawUTF8(), seconds, off.isEmpty() ? "nichts" : off.toRawUTF8());
    show (Params::rpm);
    show (Params::throttleFromAccel);
    show (Params::throttleTau);
    show (Params::srcJitterOn);
    show (Params::srcJitterAmount);
    show (Params::srcJitterSpeed);
    show (Params::smootherTau);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    const int blocks = (int) (seconds * sampleRate / blockSize);
    juce::AudioBuffer<float> recorded (1, blocks * blockSize);
    recorded.clear();

    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);
        recorded.copyFrom (0, b * blockSize, buffer, 0, 0, blockSize);
    }

    const auto out = juce::File (DOPPLERFELD_SOURCE_DIR).getChildFile ("build")
                         .getChildFile ("pitch_probe" + (off.isEmpty() ? juce::String() : "_ohne_" + off) + ".wav");
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
            std::printf ("Mitschnitt %s\n", out.getFullPathName().toRawUTF8());

    // Woran die Rechenzeit haengt - fuer die Frage, was eine Verteilung auf
    // mehrere Kerne ueberhaupt brechen koennte.
    std::printf ("Last: gesamt %.1f %% des Echtzeit-Budgets, davon Quelle %.1f %%, Physik %.1f %%\n",
                 (double) proc.cpuLoadPercent(),
                 (double) proc.cpuLoadSourcePercent(),
                 (double) proc.cpuLoadPhysicsPercent());
        }
    }

    return 0;
}
