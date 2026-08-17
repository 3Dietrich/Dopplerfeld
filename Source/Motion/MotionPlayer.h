#pragma once

#include <vector>

#include "../Physics/Vec3.h"

// Spielt einen aufgezeichneten MotionRecorder-Clip ab (Plan 3.9). Liefert
// pro tick() nur die (ggf. unstetige) Rohposition - KEIN Nachschalten eines
// Smoothers hier drin. Bei Interp::Linear ist der Pfad nur C0-stetig, an
// jeder Stützstelle springt die Geschwindigkeit und damit hörbar die
// Tonhöhe; das muss der AUFRUFER auffangen, indem er den Player-Output bei
// Linear zusätzlich durch einen MotionSmoother schickt. Bei CatmullRom ist
// der Pfad C1 und kann direkt als Zielposition gesetzt werden.
//
// Geschwindigkeitsregler skaliert die Wiedergabegeschwindigkeit und damit
// über den Doppler auch die Tonhöhe. 2-fache Wiedergabe einer schnellen
// Aufnahme kann Überschall erzeugen - gewollt (Plan 3.9), der Löser ist von
// Anfang an überschallfähig.
class MotionPlayer
{
public:
    enum class Interp { Linear, CatmullRom };

    // Kopiert den Clip nicht laufzeitkritisch - wird vom Message-/Editor-Thread
    // aufgerufen, wenn eine Aufnahme fertig oder ein Preset geladen ist.
    void setClip (const std::vector<Vec3>& framesIn, double controlRateHz);

    void setSpeed (double speedIn);   // geklemmt auf 0.25 .. 4.0, siehe Params.h playSpeed
    void setLooping (bool shouldLoop);
    void setInterp (Interp interpIn) { interp = interpIn; }
    Interp getInterp() const { return interp; }

    void trigger (double now);

    // Bricht eine laufende Wiedergabe sofort ab (Plan-Lücke: bisher lief
    // Play nur bis zum Clip-Ende oder Loop-Endlos-Betrieb ohne Ausstieg).
    // Springt NICHT auf Frame 0 zurück - ein erneutes trigger() startet ohne
    // hängengebliebene Kopfposition sauber von vorn.
    void stop() { playing = false; }

    bool isPlaying() const { return playing; }

    // Rückt die interne Wiedergabeposition um dt*speed vor und liest die
    // Position an dieser Stelle aus den Clip-Frames. Rohposition, siehe
    // Klassenkommentar - kein Smoothing hier.
    Vec3 tick (double dt);

private:
    Vec3 frameAt (double framePos) const;

    std::vector<Vec3> clipFrames;
    double clipRateHz = 200.0;

    double speed = 1.0;
    bool looping = false;
    Interp interp = Interp::CatmullRom;

    bool playing = false;
    double playHeadFrames = 0.0;   // Position im Clip, in Frames (nicht Sekunden)
};
