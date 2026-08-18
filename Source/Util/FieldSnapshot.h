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

    // Bild-Wellenfronten der Reflexionen (@dpa: "kannst Du die Reflektionen
    // ... darstellen, ähnlich den Cyan Kreisen"). Dieselbe Konstruktion wie
    // wavefrontPositions/-EmitTimes oben, nur durch die jeweilige
    // Wandspiegelung geschickt: eine Spiegelung ist eine Isometrie, deshalb
    // erzeugt "Quelle an der Wand spiegeln" exakt dieselbe Bildquellen-
    // Position, die auch die Empfänger-Spiegelung (siehe PathTransform.h)
    // physikalisch benutzt - der Kreis erscheint dort, wo die Reflexion
    // ihren Ursprung zu haben SCHEINT, wie ein Spiegelbild. Emissionszeiten
    // und -count sind identisch mit wavefrontEmitTimes/wavefrontCount oben,
    // nur der Kreis-Mittelpunkt wandert je Wand/Wandpaar.
    struct ImageWavefronts
    {
        bool active = false;
        std::array<Vec3, maxWavefronts> positions {};

        // Stetiges 0..1-Mass dafuer, ob Quelle und Hoerer ueberhaupt auf der
        // Seite der Wand(en) stehen, auf der eine Spiegelquellen-Reflexion
        // entstehen kann (DopplerEngine::wallSideGain - dieselbe Groesse, die
        // auch den Reflexions-PEGEL im Audiothread bestimmt). Ohne dieses
        // Gate zeichnete der Editor Reflexions-Ringe auch dort, wo/wenn die
        // Wand laengst nichts mehr zurueckwirft - sichtbar als Kreise, die
        // durch die Wand hindurch bzw. dahinter auftauchen (@dpa 20260818,
        // "hinter den Waenden soll eigentlich nichts reflektieren").
        float gain = 1.0f;
    };

    // Einfache Reflexion, ein Satz je Wand.
    std::array<ImageWavefronts, maxWalls> wallWavefronts {};

    // Zweifache Reflexion (Mehrfachreflexion, reflect2ndOn): die zwei
    // möglichen Reihenfolgen über die zwei Wände (Wand0→Wand1 und
    // Wand1→Wand0) - jede ist ein eigener Weg mit eigener Bildquelle.
    static constexpr int maxWallPairs = 2;
    std::array<ImageWavefronts, maxWallPairs> wallPairWavefronts {};

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

    // Zweig-Todesmessung, ueber alle gerechneten Pfade aufsummiert (siehe
    // PropagationPath::branchDeaths()). Beantwortet die Frage, ob der Abbruch
    // am Ende der Ueberschall-Haelfte die Anti-Klick-Rampe ist: dann sterben
    // die Zweige bei env nahe 1, nicht nahe 0.
    std::uint64_t branchDeaths     = 0;
    std::uint64_t loudBranchDeaths = 0;   // davon mit env >= 0,5
    double        branchDeathEnvMean = 0.0;
    double        branchDeathEnvMax  = 0.0;

    // Verdraengungen: neu ankommender Zweig hat einen noch ausklingenden aus
    // seinem Steckplatz geworfen (siehe PropagationPath::freeSlot()).
    std::uint64_t branchEvictions = 0;
    std::uint64_t causticDeaths   = 0;
    double        deathTauMeanMs  = 0.0;
    double        deathTauMaxMs   = 0.0;

    // Laute Zweige (env >= 0,5), die trotzdem in unter 2 ms verschwunden sind.
    // Das ist der Abbruch als Zahl. Ziel ist null.
    std::uint64_t abruptDeaths    = 0;
    std::uint64_t trackLost       = 0;
    std::uint64_t newIds          = 0;
    std::uint64_t droppedRoots    = 0;

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
