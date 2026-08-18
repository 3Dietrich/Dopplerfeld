#pragma once

#include "OnePoleSmoother.h"

#include <cstdint>

// Langsame, additive Mikrobewegung der Quelle M ("echter Chorus" bei
// Stillstand, @dpa 20260818): drei unabhängige Sinusoszillatoren (x/y/z),
// deren Momentanfrequenz über einen OnePoleSmoother langsam zwischen
// zufällig gewürfelten Zielfrequenzen driftet, statt zu springen.
//
// Der Trick gegen Klicks: Position = amount * sin(phase), phase' =
// 2π·freq(t). Weil freq(t) durch den One-Pole immer C0-stetig ist, ist auch
// d(position)/dt = amount·cos(phase)·2π·freq(t) stetig - ein Knick beim
// Umwürfeln der Zielfrequenz zeigt sich erst in der Beschleunigung, nicht in
// dem für den Doppler maßgeblichen v. Gefiltertes Rauschen bräuchte
// denselben Umweg über einen eigenen Glätter noch einmal extra; hier ist die
// Stetigkeit im Rezept eingebaut. OnePoleSmoother (Vorbild/Baustein aus
// diesem Ordner) wird dafür zweckentfremdet: er glättet keine Position,
// sondern das Frequenztripel wie einen Vec3.
//
// Wird additiv VOR den Bewegungsglättern (SmootherSet in PluginProcessor)
// auf das rohe Positionsziel aufgeschlagen - dadurch profitiert auch die
// sichtbare Quellenposition (FieldComponent::drawSource(), das runde
// "SendSignalIcon") automatisch vom Wackeln, ohne dass Snapshot/UI extra
// angefasst werden müssten.
//
// Reines C++ wie der Rest von Source/Motion, kein JUCE. Statt juce::Random
// (das wäre die einzige JUCE-Abhängigkeit im ganzen Ordner) ein eigener
// kleiner deterministischer Generator - reicht für "irgendeine plausible
// Streuung" und bleibt offline testbar.
class PositionJitter
{
public:
    void prepare (double tickRateHz);
    void reset();

    // Auslenkung der Wackelbewegung in Metern, 0 = aus (Default).
    void setAmount (double metres);

    // "Hektik": wie schnell/chaotisch sich die Bewegung ändert, in Hz.
    void setRate (double hektikHz);

    // Additiver Versatz für diesen Tick, in Metern. Immer aktiv, kein
    // Ein/Aus nach Bewegungszustand (@dpa 20260818: additiv immer, geht bei
    // Bewegung im normalen Doppler unter, dominiert im Stillstand von
    // selbst) - bei amount = 0 exakt {0,0,0}.
    Vec3 tick (double dt);

private:
    Vec3 pickFreqTargetHz();
    static float nextRandom01 (std::uint32_t& state);

    double amount = 0.0;
    double rateHz = 0.2;

    // Zweckentfremdet: glättet das Frequenztripel (fx,fy,fz), nicht eine
    // Position - siehe Klassenkommentar.
    OnePoleSmoother freqSmoother { 1.0 };
    double          retargetTimer = 0.0;

    double phase[3] { 0.0, 0.0, 0.0 };

    // Fester Startwert statt Uhrzeit-Aussaat (wie EngineGenerator::prepare) -
    // derselbe Regelweg muss zweimal dasselbe ergeben.
    std::uint32_t rngState = 0x5eed4a11u;
};
