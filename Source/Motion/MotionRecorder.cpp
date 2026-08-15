#include "MotionRecorder.h"

#include <algorithm>

void MotionRecorder::prepare (double controlRateHzIn, double maxSecondsIn)
{
    controlRateHz = controlRateHzIn;
    maxSeconds = maxSecondsIn;

    // Obergrenze vorab berechnen statt bei jedem push - reserve() gleich mit,
    // damit während der Aufnahme kein Realloc im Audio-/Controlpfad passiert.
    maxFrameCount = (size_t) std::max (0.0, controlRateHz * maxSeconds);

    recordedFrames.clear();
    recordedFrames.reserve (maxFrameCount);
    recording = false;
}

void MotionRecorder::startRecording (double now)
{
    startTime = now;
    recordedFrames.clear();
    recording = true;
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
}
