#pragma once

#include <atomic>
#include <cstdint>

#include <cstdint>
#include <vector>

// Mono-Ringpuffer für das abgestrahlte Quellsignal (Plan 3.3). Mono, weil
// die Quelle ein Punkt ist - Stereo entsteht ausschließlich durch die zwei
// Ohren in PropagationPath/DopplerEngine.
//
// std::vector<float> statt juce::AudioBuffer, damit die Klasse JUCE-frei
// und einzeln offline testbar bleibt, im selben Sinn wie es der Plan für
// RetardedTimeSolver ausdrücklich verlangt (Plan 3.4).
//
// Physisch immer auf die volle Kapazität angelegt, fortlaufende
// int64-Schreibposition wie im granular-Vorbild
// (granular/Source/PluginProcessor.cpp) - Modulo passiert erst beim
// Zugriff, nie beim Fortschreiben von writePos.
class SourceSignalBuffer
{
public:
    void prepare (double sampleRate, double maxSeconds);

    // Schreibt numSamples ab der aktuellen Schreibposition und schiebt sie
    // weiter.
    void write (const float* mono, int numSamples);

    // Lagrange-Interpolation 4. Ordnung um absoluteSampleIndex (siehe
    // Interpolation.h). 0.0f außerhalb des gültigen (geschriebenen)
    // Bereichs - kein Wrap-Around auf alte/zukünftige Daten.
    float readAt (double absoluteSampleIndex) const;

    // Loescht das gespeicherte Signal, behaelt aber die Schreibposition: die
    // Zeitbasis der Physik haengt daran (siehe writePos unten). Danach liefert
    // readAt() fuer die ganze Vorgeschichte Stille, waehrend neu geschriebene
    // Samples normal weiterlaufen.
    void clearHistory();

    std::int64_t writePosition() const { return writePos; }

    // Rechnet eine Zeit in Sekunden (dieselbe Zeitbasis wie im Rest der
    // Physik) in eine fraktionale Sample-Position für readAt() um.
    double timeToIndex (double t) const { return t * currentSampleRate; }

private:
    int ringSlot (std::int64_t idx) const;

    std::vector<float> ring;
    int    capacity = 0;
    double currentSampleRate = 48000.0;

public:
    // Messung, nicht Betrieb: wie oft readAt() ausserhalb des gefuellten
    // Bereichs gefragt wurde und dort eine harte Null geliefert hat - getrennt
    // nach altem und neuem Rand. Ein Hoerweg, dessen Leseposition dagegen
    // laeuft, verliert sein Signal schlagartig, waehrend seine Huellkurve
    // noch steht. mutable, weil readAt() const ist.
    mutable std::atomic<std::uint64_t> missesOld { 0 };
    mutable std::atomic<std::uint64_t> missesNew { 0 };

    // Fortlaufend, nie gewrappt - Anzahl insgesamt geschriebener Samples.
    // Gültiger Lesebereich ist [writePos - capacity, writePos - 1].
    std::int64_t writePos = 0;
};
