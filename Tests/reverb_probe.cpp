// Messwerkzeug fuer die Hallbauarten: was kostet welcher Typ, und wie klingt
// er. Kein Test - die Rechenzeiten haengen an der Maschine.
//
// Ohne Argumente rechnet es die Impulsantwort jeder Bauart und schreibt sie als
// WAV. Mit --in nimmt es stattdessen eine Aufnahme, zum Beispiel einen mit
// Dopplerfeld aufgezeichneten Vorbeiflug, und schickt sie durch jeden Typ. Das
// ist der schnelle Weg zum Hoerdurchgang: kein Plugin bauen, kein Host
// starten.
//
//   reverb_probe [--in datei.wav] [--out ordner] [--size m] [--decay s]
//                [--damp 0..1] [--early x] [--seconds t] [--sr hz]
//
// Gemessen wird die VOLLE Kette eines Abgriffpunkts (TapBus), also samt
// fruehen Reflexionen - nicht nur das Hallnetz. Nur so entspricht das, was man
// hier hoert, dem, was im Plugin herauskommt.
//
// Ausgegeben wird je Bauart eine WAV-Datei und eine Zeile mit gemessener
// Abklingzeit und Rechenzeit als Anteil an der Echtzeit.

#include "Reverb/AllpassDiffuser.h"
#include "Reverb/FdnReverb.h"
#include "Reverb/SchroederReverb.h"
#include "Reverb/TapBus.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{

// -------------------------------------------------------------------- WAV
//
// Nur so viel WAV, wie das Werkzeug braucht: lesen kann es PCM 16/24/32 und
// Float 32, geschrieben wird immer Float 32. JUCE waere hier zur Hand, wuerde
// den Probe aber an die Plugin-Bibliothek binden - und damit an einen
// vollstaendigen Bau, den man sich gerade sparen will.

struct WavData
{
    std::vector<float> mono;
    double             sampleRate = 48000.0;
};

uint32_t readU32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
uint16_t readU16 (const uint8_t* p) { return (uint16_t) ((uint32_t) p[0] | ((uint32_t) p[1] << 8)); }

bool readWav (const std::string& path, WavData& out)
{
    std::FILE* f = std::fopen (path.c_str(), "rb");

    if (f == nullptr)
        return false;

    std::vector<uint8_t> raw;
    uint8_t              chunk[65536];
    size_t               got = 0;

    while ((got = std::fread (chunk, 1, sizeof chunk, f)) > 0)
        raw.insert (raw.end(), chunk, chunk + got);

    std::fclose (f);

    if (raw.size() < 44 || std::memcmp (raw.data(), "RIFF", 4) != 0 || std::memcmp (raw.data() + 8, "WAVE", 4) != 0)
        return false;

    uint16_t channels = 0, bits = 0, format = 0;
    size_t   pos = 12;

    while (pos + 8 <= raw.size())
    {
        const char*    id   = (const char*) raw.data() + pos;
        const uint32_t size = readU32 (raw.data() + pos + 4);
        const size_t   body = pos + 8;

        if (std::memcmp (id, "fmt ", 4) == 0 && body + 16 <= raw.size())
        {
            format         = readU16 (raw.data() + body);
            channels       = readU16 (raw.data() + body + 2);
            out.sampleRate = (double) readU32 (raw.data() + body + 4);
            bits           = readU16 (raw.data() + body + 14);
        }
        else if (std::memcmp (id, "data", 4) == 0 && channels > 0)
        {
            const size_t end   = std::min (raw.size(), body + size);
            const int    bytes = bits / 8;

            if (bytes <= 0)
                return false;

            for (size_t i = body; i + (size_t) (bytes * channels) <= end; i += (size_t) (bytes * channels))
            {
                // Kanaele werden gemittelt: ein Abgriffpunkt ist mono, und
                // eine Stereoaufnahme davor waere hier ohnehin nur Material.
                double sum = 0.0;

                for (int c = 0; c < channels; ++c)
                {
                    const uint8_t* s = raw.data() + i + (size_t) (c * bytes);

                    if (format == 3 && bits == 32)
                    {
                        float v;
                        std::memcpy (&v, s, 4);
                        sum += (double) v;
                    }
                    else if (bits == 16)
                    {
                        sum += (double) (int16_t) readU16 (s) / 32768.0;
                    }
                    else if (bits == 24)
                    {
                        int32_t v = ((int32_t) s[0] << 8) | ((int32_t) s[1] << 16) | ((int32_t) s[2] << 24);
                        sum += (double) (v >> 8) / 8388608.0;
                    }
                    else if (bits == 32)
                    {
                        sum += (double) (int32_t) readU32 (s) / 2147483648.0;
                    }
                }

                out.mono.push_back ((float) (sum / (double) channels));
            }

            return ! out.mono.empty();
        }

        pos = body + size + (size & 1);
    }

    return false;
}

void writeU32 (std::FILE* f, uint32_t v) { uint8_t b[4] { (uint8_t) v, (uint8_t) (v >> 8), (uint8_t) (v >> 16), (uint8_t) (v >> 24) }; std::fwrite (b, 1, 4, f); }
void writeU16 (std::FILE* f, uint16_t v) { uint8_t b[2] { (uint8_t) v, (uint8_t) (v >> 8) }; std::fwrite (b, 1, 2, f); }

bool writeWavStereo (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, double sampleRate)
{
    std::FILE* f = std::fopen (path.c_str(), "wb");

    if (f == nullptr)
        return false;

    const uint32_t frames    = (uint32_t) std::min (l.size(), r.size());
    const uint32_t dataBytes = frames * 2u * 4u;

    std::fwrite ("RIFF", 1, 4, f);
    writeU32 (f, 36 + dataBytes);
    std::fwrite ("WAVEfmt ", 1, 8, f);
    writeU32 (f, 16);
    writeU16 (f, 3);                                   // Float
    writeU16 (f, 2);
    writeU32 (f, (uint32_t) sampleRate);
    writeU32 (f, (uint32_t) sampleRate * 2u * 4u);
    writeU16 (f, 8);
    writeU16 (f, 32);
    std::fwrite ("data", 1, 4, f);
    writeU32 (f, dataBytes);

    for (uint32_t i = 0; i < frames; ++i)
    {
        std::fwrite (&l[i], 4, 1, f);
        std::fwrite (&r[i], 4, 1, f);
    }

    std::fclose (f);
    return true;
}

// ---------------------------------------------------------------- Messung

double measureRt60 (const std::vector<float>& x, double sr)
{
    std::vector<double> edc (x.size(), 0.0);
    double              sum = 0.0;

    for (size_t i = x.size(); i-- > 0;)
    {
        sum   += (double) x[i] * (double) x[i];
        edc[i] = sum;
    }

    if (edc.empty() || edc[0] <= 0.0)
        return 0.0;

    auto timeAt = [&] (double db) -> double
    {
        const double target = edc[0] * std::pow (10.0, db / 10.0);

        for (size_t i = 0; i < edc.size(); ++i)
            if (edc[i] <= target)
                return (double) i / sr;

        return -1.0;
    };

    const double t5  = timeAt (-5.0);
    const double t35 = timeAt (-35.0);

    return (t5 < 0.0 || t35 < 0.0) ? -1.0 : (t35 - t5) * 2.0;
}

std::string argValue (int argc, char** argv, const char* key, const std::string& fallback)
{
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp (argv[i], key) == 0)
            return argv[i + 1];

    return fallback;
}

} // namespace

int main (int argc, char** argv)
{
    const std::string inPath  = argValue (argc, argv, "--in",  "");
    const std::string outDir  = argValue (argc, argv, "--out", ".");
    const double      size    = std::stod (argValue (argc, argv, "--size",    "30"));
    const double      decay   = std::stod (argValue (argc, argv, "--decay",   "2.0"));
    const double      damp    = std::stod (argValue (argc, argv, "--damp",    "0.35"));
    const double      early   = std::stod (argValue (argc, argv, "--early",   "1.0"));
    const double      seconds = std::stod (argValue (argc, argv, "--seconds", "0"));

    double sampleRate = std::stod (argValue (argc, argv, "--sr", "48000"));

    std::vector<float> input;

    if (! inPath.empty())
    {
        WavData wav;

        if (! readWav (inPath, wav))
        {
            std::printf ("konnte %s nicht lesen\n", inPath.c_str());
            return 1;
        }

        input      = wav.mono;
        sampleRate = wav.sampleRate;

        std::printf ("Quelle: %s, %.1f s bei %.0f Hz\n",
                     inPath.c_str(), (double) input.size() / sampleRate, sampleRate);
    }
    else
    {
        // Impulsantwort. Die Laenge richtet sich nach der Abklingzeit, damit
        // der Schwanz vollstaendig drin ist und die Messung nicht abschneidet.
        const double len = seconds > 0.0 ? seconds : decay * 2.5 + 0.5;

        input.assign ((size_t) (len * sampleRate), 0.0f);
        input[0] = 1.0f;

        std::printf ("Quelle: Impuls, %.1f s bei %.0f Hz\n", len, sampleRate);
    }

    // Ein Nachlauf, damit der Schwanz nicht am Materialende abgeschnitten wird.
    const size_t tail = (size_t) (decay * 1.5 * sampleRate);
    input.resize (input.size() + tail, 0.0f);

    std::printf ("Raum %.1f m, Abklingzeit %.2f s, Daempfung %.2f, fruehe Echos %.2f\n\n",
                 size, decay, damp, early);
    std::printf ("  %-12s %10s %11s %10s %11s %11s\n",
                 "Bauart", "RT60", "Rechenzeit", "Anteil", "Energie", "Spitze");
    std::printf ("  %-12s %10s %11s %10s %11s %11s\n",
                 "------", "----", "----------", "------", "-------", "------");

    constexpr int block = 256;

    struct Entry { const char* name; TapBus::Type type; };

    const Entry entries[] {
        { "Diffusor",  TapBus::Type::diffuser },
        { "Schroeder", TapBus::Type::schroeder },
        { "FDN",       TapBus::Type::fdn }
    };

    for (const auto& e : entries)
    {
        TapBus bus;

        bus.prepare (sampleRate, block);
        bus.setType (e.type);
        bus.setRoomSize (size);
        bus.setDecaySeconds (decay);
        bus.setDamping (damp);
        bus.setEarlyAmount (early);
        bus.setGain (1.0);
        bus.setWidth (1.0);

        // Kein Vorlauf im Messbetrieb: er verschoebe nur alles um eine feste
        // Zeit und macht die Messung schwerer lesbar. Im Plugin kommt er aus
        // der Entfernung des Punktes.
        bus.setPredelayMetres (0.0);
        bus.reset();

        std::vector<float> l (input.size(), 0.0f);
        std::vector<float> r (input.size(), 0.0f);

        const auto t0 = std::chrono::steady_clock::now();

        for (size_t n = 0; n < input.size(); n += block)
        {
            const int count = (int) std::min ((size_t) block, input.size() - n);
            bus.processAdd (input.data() + n, l.data() + n, r.data() + n, count);
        }

        const auto   t1       = std::chrono::steady_clock::now();
        const double cpuSec   = std::chrono::duration<double> (t1 - t0).count();
        const double audioSec = (double) input.size() / sampleRate;
        const double rt       = measureRt60 (l, sampleRate);

        // Energie und Spitze gegen den Eingang: das ist die Zahl, an der man
        // sieht, ob eine Bauart Kraft verliert oder nur anders verteilt.
        double inEnergy = 0.0, outEnergy = 0.0, outPeak = 0.0;

        for (size_t i = 0; i < input.size(); ++i)
        {
            inEnergy  += (double) input[i] * (double) input[i];
            outEnergy += (double) l[i] * (double) l[i] + (double) r[i] * (double) r[i];
            outPeak    = std::max (outPeak, (double) std::fabs (l[i]));
        }

        // Der Ausgang ist zweikanalig, der Eingang nicht - halbieren, sonst
        // stuende jede Bauart grundlos 3 dB im Plus.
        outEnergy *= 0.5;

        auto dB = [] (double x) { return x > 1.0e-12 ? 10.0 * std::log10 (x) : -120.0; };

        std::printf ("  %-12s %8.2f s %8.1f ms %8.2f %% %8.1f dB %8.1f dB\n",
                     e.name, rt, cpuSec * 1000.0, cpuSec / audioSec * 100.0,
                     dB (outEnergy) - dB (inEnergy),
                     20.0 * std::log10 (std::max (1.0e-12, outPeak)));

        const std::string path = outDir + "/reverb_" + e.name + ".wav";

        if (! writeWavStereo (path, l, r, sampleRate))
            std::printf ("     (konnte %s nicht schreiben)\n", path.c_str());
    }

    std::printf ("\nWAVs liegen in %s\n", outDir.c_str());
    return 0;
}
