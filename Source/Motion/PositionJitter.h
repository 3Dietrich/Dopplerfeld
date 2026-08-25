#pragma once

#include "OnePoleSmoother.h"

#include <cstdint>

// Langsame, additive Mikrobewegung der Quelle M ("echter Chorus" bei
// Stillstand, @dpa 20260818), gebaut wie der Flug einer Fliege: ein zufaellig
// gewuerfelter Punkt im Wackelbereich wird geradlinig angeflogen, und sobald
// er erreicht ist, knickt die Bewegung zum naechsten ab.
//
// Bewusst KEINE Kreisbahn und keine bevorzugte Ebene (@dpa 20260824: "nicht
// mehr nur auf der XY Ebene und nicht nur im Kreis (wir haben Hubschrauber ja
// extra). Bitte den Jitter wieder gleichmaessig auf x, y und z." und
// @dpa 20260825: "wie die Fliegen: jeder einzeln ueber den Jitter bereich").
// Eine umlaufende Bewegung gehoert zum Motor (EngineGenerator, Betriebsart
// Hubschrauber), nicht zur Mikrobewegung der Position. Die Zielpunkte liegen
// gleichverteilt auf der Kugel um den Ankerpunkt, die Hoehe bekommt ihren
// Anteil ueber setZFactor().
//
// Warum Zielpunkte und nicht drei Sinus je Achse: drei Sinus mit festem
// Achsenverhaeltnis ergeben eine geschlossene Lissajous-Figur. Laesst man sie
// mit konstanter Bahngeschwindigkeit durchlaufen - und genau das verlangt ein
// Tempo-Regler in m/s - ist das eine Kreisbahn, sichtbar als Karussell um den
// Ankerpunkt. Der Karussell-Eindruck steckt in der Bahnform, nicht in ihrer
// Geschwindigkeit.
//
// Der Trick gegen Klicks: die Richtung wird nicht umgeschaltet, sondern ueber
// einen kurzen Ein-Pol auf die neue Zielrichtung gezogen. Ein harter
// Richtungswechsel waere ein Sprung in der Geschwindigkeit - fuer den Doppler
// dieselbe Kante wie ein Positionssprung. Ueber ein paar Millisekunden gezogen
// bleibt der Knick sichtbar und ist trotzdem stetig. OnePoleSmoother
// (Baustein aus diesem Ordner) wird dafuer zweckentfremdet: er glaettet keine
// Position, sondern die Flugrichtung.
//
// Ausschlag und Tempo bleiben dabei genau das, was auf den Reglern steht: der
// Schritt ist Richtung mal Tempo mal dt (also exakt die eingestellte
// Bahngeschwindigkeit), und die Zielpunkte liegen im Ausschlag (die Klemmung
// in tick() ist nur das Netz darunter).
//
// Wird additiv VOR den Bewegungsglaettern (SmootherSet in PluginProcessor)
// auf das rohe Positionsziel aufgeschlagen - dadurch profitiert auch die
// sichtbare Quellenposition (FieldComponent::drawSource(), das runde
// "SendSignalIcon") automatisch vom Wackeln, ohne dass Snapshot/UI extra
// angefasst werden muessten.
//
// Reines C++ wie der Rest von Source/Motion, kein JUCE. Statt juce::Random
// (das waere die einzige JUCE-Abhaengigkeit im ganzen Ordner) ein eigener
// kleiner deterministischer Generator - reicht fuer "irgendeine plausible
// Streuung" und bleibt offline testbar.
//
// Bedient wird das seit @dpa 20260825 ueber ZWEI Groessen, die sich nicht
// gegenseitig aufheben: Ausschlag in Metern (wie weit) und Bahngeschwindigkeit
// in m/s (wie schnell). Die Frequenz ist kein Regler mehr, sie ergibt sich -
// siehe setSpeed().
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

    // Bahngeschwindigkeit der Wackelbewegung in Metern je Sekunde, 0 = die
    // Bewegung steht.
    //
    // Das ist seit @dpa 20260825 der zweite und letzte Regler der Bewegung,
    // und er ersetzt sowohl die alte "Hektik" (eine Frequenz) als auch den
    // Tempo-Deckel "Jit Max". Der Grund ist Bedienbarkeit, nicht Physik:
    // Ausschlag, Hektik und Deckel hingen multiplikativ zusammen
    // (v_peak = A * 2pi * f * 2*sqrt(3)), wer eines drehte, verschob die
    // Wirkung der beiden anderen. @dpa: "ist das mit der Hektik zu
    // kompliziert das 'passende Fenster' zu finden".
    //
    // Jetzt bedeutet jeder Regler genau eine Sache: der Ausschlag sagt, WIE
    // WEIT sich die Quelle bewegt, dieser hier, WIE SCHNELL. Die Frequenz
    // ergibt sich aus beiden (f = v / (2pi*A)) und ist kein Bedienwert mehr.
    // Ein Deckel eruebrigt sich damit von selbst: eine Geschwindigkeit, die
    // man in m/s einstellt, kann nicht versehentlich Mach 365 werden.
    void setSpeed (double metresPerSecond);

    // Anteil der Höhe am Wackeln (@dpa 20260824: "bitte doch einen Regler für
    // z (0-100% of jitter, 0: z=Source Z)"). 1 = z wackelt genauso weit wie x
    // und y, 0 = gar nicht, die Quelle bleibt auf ihrer eingestellten Höhe.
    //
    // Ein Anteil und keine eigene Auslenkung: so bleibt "Jitter" der eine
    // Regler für die Größe der Bewegung, und dieser hier sagt nur, ob sie
    // flach in der Ebene liegt oder den Raum füllt.
    void setZFactor (double factor01);

    // Additiver Versatz für diesen Tick, in Metern. Immer aktiv, kein
    // Ein/Aus nach Bewegungszustand (@dpa 20260818: additiv immer, geht bei
    // Bewegung im normalen Doppler unter, dominiert im Stillstand von
    // selbst) - bei amount = 0 exakt {0,0,0}.
    Vec3 tick (double dt);

private:
    // Der naechste anzufliegende Punkt im Wackelbereich, relativ zum
    // Ankerpunkt. Siehe tick().
    Vec3 pickWaypoint();
    static float nextRandom01 (std::uint32_t& state);

    // Ausschlag, Tempo und Hoehenanteil werden ANGEFAHREN, nicht gesetzt
    // (@dpa 20260824: "Jitter ist noch immer sehr laut beim Verstellen"). Ein
    // Reglerruck von 0 auf 200 m ist sonst ein Positionssprung um bis zu
    // 200 m innerhalb eines Ticks - fuer den Loeser formal Ueberschall, also
    // genau die Kegelankunft samt N-Welle, die man beim Verstellen hoert.
    //
    // Angefahren wird ueber einen kurzen Ein-Pol (amountGlideSeconds), dessen
    // Schrittweite beim Ausschlag zusaetzlich unter dem eingestellten TEMPO
    // liegt - eine Aenderung des Ausschlags ist eine echte Strecke, und der
    // Wackler legt Strecken nun einmal mit seiner Bahngeschwindigkeit
    // zurueck. Kein verstecktes Limit: das Ziel wird vollstaendig erreicht,
    // es dauert nur laenger, je gemaechlicher der Wackler unterwegs ist.
    // Steht das Tempo auf null, steht auch das Anfahren - dann bewegt sich
    // der Wackler eben gar nicht, und das ist genau, was der Regler sagt.
    double amount       = 0.0;
    double amountTarget = 0.0;
    double zFactor      = 1.0;
    double zTarget      = 1.0;
    double speed        = 0.0;
    double speedTarget  = 0.0;

    static constexpr double amountGlideSeconds = 0.02;

    // Tickrate, aus prepare(). Sie ist die Darstellbarkeitsgrenze der
    // Frequenz: schneller als zwei Ticks je Schwingung laesst sich eine
    // Sinusbewegung auf diesem Raster nicht mehr abbilden, sie faltete als
    // Alias zurueck. Das ist kein Bedienlimit, sondern dieselbe Grenze, die
    // ein Sample-Raster jeder Wellenform setzt.
    double tickRate = 1000.0;

    // Zweckentfremdet: glaettet die Flugrichtung, nicht eine Position - siehe
    // Klassenkommentar.
    OnePoleSmoother headingSmoother { 1.0 };

    // Aktueller Versatz zum Ankerpunkt und der Punkt, der gerade angeflogen
    // wird - beide in Metern, beide relativ zum Anker.
    Vec3 offset;
    Vec3 waypoint;

    // Fester Startwert statt Uhrzeit-Aussaat (wie EngineGenerator::prepare) -
    // derselbe Regelweg muss zweimal dasselbe ergeben.
    std::uint32_t rngState = 0x5eed4a11u;
};
