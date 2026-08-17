#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>
#include <vector>

// Der billige Gegenpart zu einem Klon-Pfad: eine Handvoll leicht verzögerter,
// in der Verzögerung langsam wandernder Kopien des fertigen Stereosignals.
//
// Das ist ausdrücklich KEINE Physik, und deshalb steht die Klasse auch nicht in
// Source/Physics/. Ein echter Klon ist ein eigener Ausbreitungsweg mit eigenem
// Löser, eigener Laufzeit und eigenem Doppler - dafür kostet er auch so viel
// wie jeder andere Pfad. Hier wird stattdessen nur nachgemacht, was man an
// einem Schwarm zuerst hört: dass die Einzelquellen minimal versetzt eintreffen
// und ihre Tonhöhen leicht gegeneinander wandern. Das kostet eine Verzögerung
// und eine Interpolation je Kopie und Sample, keinen einzigen Löseraufruf.
//
// Die wandernde Verzögerung erzeugt die Tonhöhenabweichung von selbst: eine
// Leseposition, die sich mit dv/dt durch den Puffer schiebt, verstimmt um genau
// diesen Faktor. Ein getrennter Tonhöhen-Parameter wäre deshalb doppelt gemoppelt.
class CloneSpray
{
public:
    // Mehr Kopien als das gibt es nicht - der Puffer für die Verzögerungen wird
    // danach bemessen und in prepare() einmal angelegt.
    static constexpr int maxClones = 20;

    void prepare (double sampleRate, int numChannels);
    void reset();

    // Wie viele Kopien mitlaufen. 0 heißt: process() ist ein No-op und kostet
    // nichts.
    void setCount (int count);
    int  getCount() const { return count; }

    // Streuung der Verzögerungen in Millisekunden (der "Abstand" der Klone) und
    // Stärke des Wanderns, ebenfalls in Millisekunden.
    void setSpreadMs (double milliseconds);
    void setJitterMs (double milliseconds);

    // Pegel je Kopie, relativ zum Original.
    void setLevel (double level01);

    // Addiert die Kopien auf den Puffer. Das Original bleibt unverändert
    // stehen - der Schwarm kommt dazu, er ersetzt nichts.
    void process (juce::AudioBuffer<float>& buffer);

private:
    struct Clone
    {
        double baseDelaySamples = 0.0;

        // Langsame, unabhängige Wanderung. Zwei Sinus mit inkommensurablen
        // Raten statt eines einzigen: eine reine Sinuswanderung wäre als
        // regelmäßiges Leiern hörbar, zwei überlagerte klingen unregelmäßig,
        // ohne dass ein Zufallsgenerator im Audiothread laufen muss.
        double phaseA = 0.0, phaseB = 0.0;
        double rateA  = 0.0, rateB  = 0.0;
    };

    double readInterpolated (int channel, double delaySamples) const;

    std::vector<float> ring;      // verschachtelt: [sample * channels + ch]
    int    channels = 0;
    int    capacity = 0;
    int    writePos = 0;

    double sr = 48000.0;

    Clone  clones[maxClones];
    int    count = 0;

    double spreadMs = 12.0;
    double jitterMs = 1.5;
    double level    = 0.5;
};
