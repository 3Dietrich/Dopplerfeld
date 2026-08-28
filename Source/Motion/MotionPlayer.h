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

    // Bricht eine laufende Wiedergabe sofort ab - ohne das liefe Play nur
    // bis zum Clip-Ende oder im Loop-Betrieb ohne Ausstieg weiter.
    // Springt NICHT auf Frame 0 zurück - ein erneutes trigger() startet ohne
    // hängengebliebene Kopfposition sauber von vorn.
    void stop() { playing = false; loopEdgePending = false; }

    bool isPlaying() const { return playing; }

    // Rundenwechsel als Schnitt (@dpa 20260824: "Ende erreicht, leise, umbau,
    // laut, start"). Die Wiedergabe wickelt am Rundenende NICHT selbst um -
    // sie bleibt auf dem letzten Frame stehen und meldet es hier. Der
    // Aufrufer blendet aus, ruft restartRound() und blendet wieder ein.
    //
    // Das Modulo an dieser Stelle war der Grund fuer die Lastspitze am
    // Rundenpunkt: der Sprung vom Ende zum Anfang lief als ZIEL durch die
    // Glaettung und stand damit als echte Bewegung in der Bahn - gemessen im
    // load_check-Abschnitt "Sprungnaht" |M_r| 16 und der teuerste Block mit
    // 29558 statt 64 Loeser-Auswertungen.
    bool atLoopEdge() const { return loopEdgePending; }

    // Setzt an den Rundenanfang zurueck. Der Ueberhang, der beim Erreichen
    // des Endes ueber den letzten Frame hinausging, wird dabei mitgenommen -
    // die Runde verliert dadurch keine Zeit ausser der Schnittdauer selbst.
    void restartRound();

    // Erster Frame des Clips, in Metern - das Ziel des Schnitts.
    Vec3 firstFrame() const { return clipFrames.empty() ? Vec3{} : clipFrames.front(); }

    // Geschwindigkeit, mit der eine Runde anfaengt, in m/s: die Steigung der
    // Bahn am Rundenanfang, gemessen mit derselben Interpolation, mit der die
    // Wiedergabe sie gleich abfaehrt, und mit der Wiedergabegeschwindigkeit
    // skaliert.
    //
    // Wozu: der Rundenschnitt setzt die Bahn am ersten Frame komplett neu auf.
    // Ohne Vorgeschwindigkeit bekommt sie dort eine RUHENDE Vorgeschichte
    // (SourceTrajectory::jumpTo), der Loeser findet vor dem Schnitt ueberall
    // M_r = 0. Im Ueberschall lebt der zeitverkehrt gehoerte Zweig aber genau
    // von dieser Vorgeschichte: er laeuft in ihr rueckwaerts und stirbt an
    // ihrem kuenstlichen Rand mitten im Ton. Mit dieser Geschwindigkeit fuellt
    // die Engine stattdessen eine gleichfoermig bewegte Vorgeschichte
    // (SourceTrajectory::fillLinear), und der Zweig hat Bahn unter sich.
    Vec3 startVelocity() const;

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

    // Die Runde ist um, der Schnitt steht aus (siehe atLoopEdge).
    bool   loopEdgePending = false;
    double wrapOvershoot   = 0.0;   // was ueber den letzten Frame hinausging
};
