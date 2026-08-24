#pragma once

#include "OnePoleSmoother.h"

#include <cstdint>

// Langsame, additive Mikrobewegung der Quelle M ("echter Chorus" bei
// Stillstand, @dpa 20260818): drei unabhängige Sinusoszillatoren (x/y/z),
// deren Momentanfrequenz über einen OnePoleSmoother langsam zwischen
// zufällig gewürfelten Zielfrequenzen driftet, statt zu springen.
//
// Bewusst KEINE Kreisbahn und keine bevorzugte Ebene (@dpa 20260824: "nicht
// mehr nur auf der XY Ebene und nicht nur im Kreis (wir haben Hubschrauber ja
// extra). Bitte den Jitter wieder gleichmäßig auf x, y und z."). Die
// Rotoren-Betriebsart, die es hier von 20260821 bis 20260824 gab, ist damit
// entfallen - eine umlaufende Bewegung gehört zum Motor (EngineGenerator,
// Betriebsart Hubschrauber), nicht zur Mikrobewegung der Position. Alle drei
// Achsen bekommen denselben Ausschlag und je eine eigene, unabhängig
// gewürfelte Frequenz.
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

    // Eigener Startwert des Zufallsgenerators. Damit wackeln mehrere Jitter
    // nebeneinander wirklich unabhaengig, statt dieselbe Folge zu durchlaufen -
    // ohne das haetten alle Klone denselben Wackler, nur zeitversetzt um nichts.
    void setSeed (std::uint32_t newSeed)
    {
        rngState = newSeed | 1u;   // 0 waere ein Fixpunkt des xorshift
        reset();
    }
    void reset();

    // Auslenkung der Wackelbewegung in Metern, 0 = aus (Default).
    void setAmount (double metres);

    // "Hektik": wie schnell/chaotisch sich die Bewegung ändert, in Hz.
    void setRate (double hektikHz);

    // Obergrenze für die Bahngeschwindigkeit des Wacklers, in m/s. 0 oder
    // negativ heißt "keine Grenze".
    //
    // Gebremst wird ueber die Frequenz, nicht ueber den Ausschlag: die
    // Bewegung wird langsamer und behaelt ihre Groesse, statt an einer Kante
    // abgeschnitten zu werden (@dpa 20260820: "vielleicht kannst Du die
    // Jitterbewegung auch zum max Speed (rund) limitieren? So kann man grosse
    // Gebiete in unterschiedlichen Geschwindigkeiten"). Ein Ausschlag von
    // 1000 m bei 20 Hz waeren sonst 125000 m/s, also Mach 365 - der Loeser
    // haette dort nichts mehr zu suchen.
    void setMaxSpeed (double metresPerSecond);

    // Additiver Versatz für diesen Tick, in Metern. Immer aktiv, kein
    // Ein/Aus nach Bewegungszustand (@dpa 20260818: additiv immer, geht bei
    // Bewegung im normalen Doppler unter, dominiert im Stillstand von
    // selbst) - bei amount = 0 exakt {0,0,0}.
    Vec3 tick (double dt);

private:
    Vec3 pickFreqTargetHz();
    static float nextRandom01 (std::uint32_t& state);

    // Ausschlag und Hektik werden ANGEFAHREN, nicht gesetzt (@dpa 20260824:
    // "Jitter ist noch immer sehr laut beim Verstellen"). Ein Reglerruck von
    // 0 auf 200 m ist sonst ein Positionssprung um bis zu 200 m innerhalb
    // eines Ticks - fuer den Loeser formal Ueberschall, also genau die
    // Kegelankunft samt N-Welle, die man beim Verstellen hoert.
    //
    // Angefahren wird ueber einen kurzen Ein-Pol (amountGlideSeconds), dessen
    // Schrittweite zusaetzlich unter dem EIGENEN Tempo-Deckel des Wacklers
    // liegt. Kein verstecktes Limit: das Ziel wird vollstaendig erreicht, es
    // dauert nur einen Wimpernschlag laenger, und schneller als "Jit Max"
    // erlaubt bewegt sich der Wackler ohnehin nirgends.
    double amount       = 0.0;
    double amountTarget = 0.0;
    double rateHz       = 0.2;
    double rateTarget   = 0.2;
    double maxSpeed     = 0.0;   // 0 = keine Grenze

    static constexpr double amountGlideSeconds = 0.02;

    // Zweckentfremdet: glättet das Frequenztripel (fx,fy,fz), nicht eine
    // Position - siehe Klassenkommentar.
    OnePoleSmoother freqSmoother { 1.0 };
    double          retargetTimer = 0.0;

    double phase[3] { 0.0, 0.0, 0.0 };

    // Fester Startwert statt Uhrzeit-Aussaat (wie EngineGenerator::prepare) -
    // derselbe Regelweg muss zweimal dasselbe ergeben.
    std::uint32_t rngState = 0x5eed4a11u;
};
