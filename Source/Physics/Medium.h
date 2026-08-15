#pragma once

#include <cmath>

// Zustand des Ausbreitungsmediums, pro Block einmal aus den Parametern
// gelesen und als Wert an alle Rechenteile durchgereicht (Plan 3.1) - keine
// globale Konstante, kein Singleton, damit T später ein echter Parameter
// werden kann, ohne dass sich an der Signatur etwas ändert.
struct MediumState
{
    double tempCelsius = 20.0;   // Phase 1 fest, Phase 2 bedienbar (Plan 2.2)

    // c(T) = 331.3 * sqrt(1 + T/273.15) [m/s], siehe Plan 2.2.
    // Bei 20°C ergibt das rund 343,21 m/s.
    double speedOfSound() const
    {
        return 331.3 * std::sqrt (1.0 + tempCelsius / 273.15);
    }
};
