#include "MotionRecorder.h"

#include <algorithm>

void MotionRecorder::prepare (double controlRateHzIn, double maxSecondsIn)
{
    controlRateHz = controlRateHzIn;
    maxSeconds = maxSecondsIn;

    // Obergrenze vorab berechnen statt bei jedem push - reserve() gleich mit,
    // damit während der Aufnahme kein Realloc im Audio-/Controlpfad passiert.
    maxFrameCount = (size_t) std::max (0.0, controlRateHz * maxSeconds);

    // KEIN clear() hier: der Host ruft prepare() bei jeder Blockgrößen-/
    // Samplerate-Änderung erneut auf (@dpa-Repro: Buffergröße in den Audio-
    // Settings ändern löschte eine fertige Aufnahme). reserve() wächst nur
    // bei Bedarf und ist sonst ein No-op, bestehende Frames bleiben erhalten.
    recordedFrames.reserve (maxFrameCount);
    recording = false;
}

void MotionRecorder::startRecording (double now)
{
    startTime = now;

    // Zwei Erhöhungen um den Inhaltswechsel herum: ein Leser, der irgendwo
    // dazwischen liest, sieht vorher und nachher verschiedene Zählerstände
    // und verwirft seine Kopie (siehe copyFrames).
    takeGeneration.fetch_add (1, std::memory_order_release);
    publishedFrames.store (0, std::memory_order_release);

    recordedFrames.clear();
    recording = true;

    takeGeneration.fetch_add (1, std::memory_order_release);
}

void MotionRecorder::stopRecording()
{
    recording = false;
}

void MotionRecorder::pushSmoothed (Vec3 pos, double /*t*/)
{
    if (! recording)
        return;

    // Obergrenze erreicht: still verwerfen statt zu wachsen - eine vergessene
    // laufende Aufnahme darf nicht unbegrenzt Speicher fressen (Plan 3.9).
    if (recordedFrames.size() >= maxFrameCount)
        return;

    recordedFrames.push_back (pos);

    // Erst schreiben, dann die Anzahl veröffentlichen - in dieser Reihenfolge
    // sieht ein Leser nie einen halb geschriebenen Frame (siehe copyFrames).
    publishedFrames.store ((int) recordedFrames.size(), std::memory_order_release);
}

bool MotionRecorder::copyFrames (std::vector<Vec3>& dest) const
{
    // Vier Versuche, wie bei der Snapshot-Übergabe der Engine: jeder
    // gescheiterte bedeutet, dass der Audiothread genau dazwischen eine neue
    // Aufnahme begonnen oder einen Clip geladen hat.
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const unsigned int before = takeGeneration.load (std::memory_order_acquire);
        const int          count   = publishedFrames.load (std::memory_order_acquire);

        // Zusätzlich an der tatsächlichen Größe geklemmt: nach einem clear()
        // im Audiothread steht die veröffentlichte Anzahl kurzzeitig über der
        // Größe, und über das Ende hinaus gelesen wird hier nicht - erkannt
        // wird der Fall danach an der Zählung.
        const int available = std::min (count, (int) recordedFrames.size());

        dest.clear();

        if (available > 0)
            dest.assign (recordedFrames.begin(), recordedFrames.begin() + available);

        if (takeGeneration.load (std::memory_order_acquire) == before)
            return true;
    }

    dest.clear();
    return false;
}

void MotionRecorder::setFrames (const std::vector<Vec3>& src)
{
    // Abschneiden statt allokieren: die Kapazität steht seit prepare() fest,
    // und im Audiothread darf sie nicht wachsen. Ein Preset mit einer anderen
    // Höchstlänge verliert dadurch sein Ende - das ist der einzige zulässige
    // Ausweg, Allokieren oder Abstürzen sind es nicht.
    const size_t count = std::min (src.size(), maxFrameCount);

    recording = false;

    takeGeneration.fetch_add (1, std::memory_order_release);
    publishedFrames.store (0, std::memory_order_release);

    recordedFrames.assign (src.begin(), src.begin() + (std::ptrdiff_t) count);

    publishedFrames.store ((int) count, std::memory_order_release);
    takeGeneration.fetch_add (1, std::memory_order_release);
}
