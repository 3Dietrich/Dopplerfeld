#pragma once

#include "../Physics/Vec3.h"
#include "../Physics/Listener.h"

#include <array>
#include <cstdint>

// Was der Audiothread nach jedem Block für die Anzeige zusammenträgt (Plan
// 3.12) - dezimierte Trajektorienpunkte, ein paar zurückliegende
// Emissionszeiten (der Editor zeichnet daraus die Wellenfronten selbst,
// keine eigene Kegelgeometrie nötig), die aktuelle Ohrgeometrie und pro Pfad
// Verzögerung/Amplitude/M_r. DopplerEngine::fillSnapshot() (H6/H13) befüllt
// das; FieldComponent (H11) liest nur.
//
// Reiner Wertetyp, feste Obergrenzen statt std::vector - wird per
// Doppelpufferung (Plan 3.12: zwei Instanzen, atomarer Index-Tausch)
// zwischen Audio- und GUI-Thread gereicht, da darf nichts allokieren.
struct FieldSnapshot
{
    static constexpr int maxTrailPoints = 128;
    static constexpr int maxWavefronts  = 12;
    // 2 Ohren mal (Direktschall + 3 Flächen + 6 Flächenpaare) = 20 mögliche
    // Wege; aufgerundet, damit die Anzeige nichts abschneidet.
    static constexpr int maxPaths       = 24;
    static constexpr int maxWalls       = 2;    // frei platzierbare Wände, siehe DopplerEngine

    // Quellposition, dezimierte Spur der letzten Sekunden.
    Vec3 sourcePos;
    std::array<Vec3, maxTrailPoints> trail {};
    int  trailCount = 0;

    // Momentangeschwindigkeit der Quelle (m/s, aus der Trajektorie) und die
    // aktuelle Schallgeschwindigkeit (m/s, temperaturabhaengig) - zusammen
    // ergibt das Mach fuer die Statuszeile (@dpa-Feedback: Tempo-Anzeige).
    double sourceSpeed  = 0.0;
    double speedOfSound = 343.0;

    // Emissionszeiten (Sekunden, dieselbe Zeitbasis wie DopplerEngine::
    // currentTime()) vergangener Sample-Momente, aus denen der Editor
    // Wellenfront-Kreise um die jeweilige Quellposition zur Emissionszeit
    // zeichnet - Radius = c*(t_now - t_emit). Bei Unterschall ergibt das die
    // gestauchten Fronten vorne, bei Überschall bildet die Einhüllende den
    // Mach-Kegel von selbst (Plan 3.12).
    std::array<double, maxWavefronts> wavefrontEmitTimes {};

    // Quellposition M(t_k) zur jeweiligen Emissionszeit, aus der Trajektorie
    // geholt. Ohne sie müssten alle Kreise um die AKTUELLE Quellposition
    // gezeichnet werden - dann fehlt genau der Versatz zwischen den Fronten,
    // aus dem die gestauchte Vorderseite und bei Überschall die Einhüllende
    // des Mach-Kegels überhaupt erst entstehen.
    std::array<Vec3, maxWavefronts>   wavefrontPositions {};

    int    wavefrontCount = 0;
    double now = 0.0;

    ListenerState listener;

    // Pro Pfad ein Eintrag, parallel zu DopplerEngine::getPath(i).
    struct PathInfo
    {
        int    ear             = 0;       // 0 = links, 1 = rechts (Plan 3.6 pathEar)

        // Welche Fläche diesen Pfad erzeugt: 0 = Direktschall, 1 = Boden,
        // ab 2 die Wände (siehe DopplerEngine::Surface). Bei zwei Reflexionen
        // die zuerst getroffene.
        int    surface         = 0;

        // Anzahl der Reflexionen: 0 = Direktschall, 1 = einfach, 2 = zweifach.
        int    order           = 0;
        int    activeBranches  = 0;
        double delaySeconds    = 0.0;
        double machRadial      = 0.0;
    };
    std::array<PathInfo, maxPaths> paths {};
    int pathCount = 0;

    // Klone werden NICHT einzeln aufgelistet - bei zwanzig Stueck waere die
    // Liste unbrauchbar und die Werte saehen ohnehin fast gleich aus. Was
    // zaehlt, ist wie viele gerade gerechnet werden.
    int realCloneCount  = 0;
    int cheapCloneCount = 0;

    // Lage der Wände, damit die Feldanzeige sie zeichnen kann. Die Wand ist
    // eine unendliche Ebene; in der Draufsicht ist sie eine Gerade durch
    // anchor mit Richtung (cos azimuth, sin azimuth). Die Neigung ändert daran
    // nichts - eine geneigte Wand steht in der Draufsicht an derselben Stelle,
    // sie liegt nur anders im Raum.
    struct WallInfo
    {
        bool   on         = false;
        Vec3   anchor;
        double azimuthRad = 0.0;
        double tiltRad    = 0.0;
    };
    std::array<WallInfo, maxWalls> walls {};

    // Vorbeiflug-Wegvorschau (@dpa-Feedback), nur bei laufendem Vorbeiflug
    // gueltig (flyByActive). Geschlossene Geometrie, keine Suche: die Bahn
    // ist gerade, siehe FlyByGenerator::nearestPoint().
    bool   flyByActive          = false;
    Vec3   flyByPlannedEnd;       // Ende der geplanten Reststrecke
    Vec3   flyByNearestPoint;     // Punkt der Bahn mit kuerzestem Abstand zu L
    double flyByNearestDistance = 0.0;
};
