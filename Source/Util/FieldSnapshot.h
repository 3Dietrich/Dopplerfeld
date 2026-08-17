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
    static constexpr int maxPaths       = 12;   // Plan 2.12: bis zu 12 Leser (2 Ohren + Spiegelquellen)

    // Quellposition, dezimierte Spur der letzten Sekunden.
    Vec3 sourcePos;
    std::array<Vec3, maxTrailPoints> trail {};
    int  trailCount = 0;

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
        bool   mirrored        = false;   // Bodenspiegelung statt Direktschall
        int    activeBranches  = 0;
        double delaySeconds    = 0.0;
        double machRadial      = 0.0;
    };
    std::array<PathInfo, maxPaths> paths {};
    int pathCount = 0;
};
