#pragma once

#include <atomic>
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

    // Kopie für den Message-Thread (Preset-Speicherung), lock-frei und ohne
    // Rückwirkung auf den Audiothread. numFrames()/frames() taugen dafür
    // nicht: der Audiothread hängt dort währenddessen an.
    //
    // false heißt "während des Kopierens hat der Inhalt komplett gewechselt"
    // (neue Aufnahme begonnen oder Clip geladen) - dann ist die Kopie eine
    // Mischung zweier Aufnahmen und wird verworfen, dest bleibt leer.
    bool copyFrames (std::vector<Vec3>& dest) const;

    // Setzt den kompletten Inhalt aus einem geladenen Zustand. Läuft im
    // Audiothread und allokiert nicht, solange prepare() gelaufen ist: die
    // Kapazität steht seitdem fest, überzählige Frames werden abgeschnitten
    // statt sie zu erzwingen.
    void setFrames (const std::vector<Vec3>& src);

private:
    double controlRateHz = 200.0;
    double maxSeconds = 120.0;
    size_t maxFrameCount = 0;

    bool recording = false;
    double startTime = 0.0;

    std::vector<Vec3> recordedFrames;

    // Anzahl fertig geschriebener Frames, lock-frei für copyFrames(). Der
    // Audiothread hängt an, ohne je neu zu allokieren (die Kapazität steht
    // seit prepare() fest), und veröffentlicht die neue Anzahl DANACH - ein
    // Leser, der erst die Anzahl liest und dann so viele Frames kopiert, sieht
    // deshalb ausschließlich fertig geschriebene Einträge.
    std::atomic<int> publishedFrames { 0 };

    // Wird bei jedem vollständigen Inhaltswechsel erhöht (neue Aufnahme,
    // geladener Clip) - und nur dabei wird bestehender Speicher überschrieben
    // statt angehängt. Genau dann kann ein laufender Leser eine Mischung
    // zweier Aufnahmen erwischen, und genau daran erkennt er es.
    std::atomic<unsigned int> takeGeneration { 0 };
};
