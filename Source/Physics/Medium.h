#pragma once

#include <cmath>

// Zustand des Ausbreitungsmediums, pro Block einmal aus den Parametern
// gelesen und als Wert an alle Rechenteile durchgereicht (Plan 3.1) - keine
// globale Konstante, kein Singleton, damit T später ein echter Parameter
// werden kann, ohne dass sich an der Signatur etwas ändert.
struct MediumState
{
    double tempCelsius = 20.0;   // Lufttemperatur, siehe Params::airTempC.

    // c(T) = 331.3 * sqrt(1 + T/273.15) [m/s], siehe Plan 2.2.
    // Bei 20°C ergibt das rund 343,21 m/s.
    double speedOfSound() const
    {
        return 331.3 * std::sqrt (1.0 + tempCelsius / 273.15);
    }

    // Höhe über dem Meeresspiegel in Metern, siehe Params::airAltitude. Wirkt NICHT auf
    // tempCelsius zurück - Temperatur bleibt ein eigener, unabhängiger
    // Regler (sonst gäbe es zwei Werte für dieselbe Größe). Wer einen Jet in
    // 10 km darstellen will, stellt die Höhe UND die dort herrschende Kälte
    // separat ein.
    double altitudeMetres = 0.0;

    // Luftdruck nach der barometrischen Höhenformel der internationalen
    // Standardatmosphäre (ICAO): bis zur Tropopause bei 11 km faellt der
    // Druck mit einem linearen Temperaturgradienten (0,0065 K/m ausgehend von
    // 288,15 K auf Meereshöhe), darüber (Stratosphäre) naehert das Modell die
    // Temperatur als konstant an und der Druck faellt rein exponentiell
    // weiter. Die 11000 m sind also keine willkürliche Kappung, sondern die
    // Nahtstelle der beiden Modell-Äste selbst - der exponentielle Ast setzt
    // exakt beim Druckwert p(11000) des linearen Astes an, damit der Übergang
    // stetig bleibt.
    double pressurePa() const
    {
        constexpr double p0 = 101325.0;   // Pa, Normaldruck auf Meereshöhe

        if (altitudeMetres < 11000.0)
            return p0 * std::pow (1.0 - 0.0065 * altitudeMetres / 288.15, 5.2559);

        const double p11 = p0 * std::pow (1.0 - 0.0065 * 11000.0 / 288.15, 5.2559);
        return p11 * std::exp (-(altitudeMetres - 11000.0) / 6341.6);
    }

    // Luftdichte aus dem idealen Gasgesetz, rho = p / (R_spezifisch * T).
    // T kommt bewusst aus tempCelsius (nicht aus einem eigenen Höhenmodell) -
    // Temperatur und Höhe sind unabhängige Regler, siehe altitudeMetres oben.
    double density() const
    {
        constexpr double rSpecific = 287.058;   // J/(kg*K), trockene Luft
        return pressurePa() / (rSpecific * (tempCelsius + 273.15));
    }

    // Pegelfaktor der Dichte, bezogen auf die Normluft-Dichte rho0 = 1,225
    // kg/m^3 (15°C, Meereshöhe) - der Schalldruck einer gegebenen Quelle
    // skaliert näherungsweise mit der Luftdichte. Reine Physik ohne
    // Normierung auf die Regler-Defaults: bei tempCelsius=20/altitudeMetres=0
    // kommt hier NICHT exakt 1.0 heraus (rund 0,983), das Zurechtrücken auf
    // "Defaults klingen wie bisher" passiert an der Verwendungsstelle
    // (PluginProcessor::applyParameters), nicht hier.
    double densityGain() const
    {
        constexpr double rho0 = 1.225;
        return density() / rho0;
    }
};
