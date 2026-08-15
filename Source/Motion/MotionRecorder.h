#pragma once

#include <vector>

#include "../Physics/Vec3.h"

// Zeichnet die GEGLÄTTETE Quellposition auf (Plan 3.9) - reine Speicherklasse,
// glättet selbst nichts. Der Aufrufer schickt pro Regelrate-Tick den bereits
// fertigen MotionSmoother-Output rein, damit Aufnahme und Live-Wiedergabe
// akustisch identisch klingen (sonst hört man den Unterschied zwischen roher
// Maus und geglätteter Bewegung).
//
// Wächst während der Aufnahme linear (kein Ringpuffer nötig - Aufnahme hat
// ein klares Ende via stopRecording), aber mit fester Obergrenze
// (maxSeconds * controlRateHz Frames), damit eine vergessene laufende
// Aufnahme nicht unbegrenzt Speicher frisst. Danach wird stillschweigend
// nicht mehr angehängt, kein Crash/Überlauf.
class MotionRecorder
{
public:
    // Default 200 Hz Regelrate, 120 s max. Aufnahmelänge (Plan 3.9).
    void prepare (double controlRateHz = 200.0, double maxSeconds = 120.0);

    void startRecording (double now);
    void stopRecording();

    // GEGLÄTTETE Position, nicht die rohe Mauseingabe - siehe Klassenkommentar.
    // t wird aktuell nicht ausgewertet (feste Regelrate reicht für Phase 1),
    // bleibt aber Teil der Schnittstelle für spätere variable Raten (Plan 3.9).
    void pushSmoothed (Vec3 pos, double t);

    bool isRecording() const { return recording; }

    int numFrames() const { return (int) recordedFrames.size(); }
    const std::vector<Vec3>& frames() const { return recordedFrames; }

private:
    double controlRateHz = 200.0;
    double maxSeconds = 120.0;
    size_t maxFrameCount = 0;

    bool recording = false;
    double startTime = 0.0;

    std::vector<Vec3> recordedFrames;
};
