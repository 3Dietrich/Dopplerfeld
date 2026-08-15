#include "MotionPlayer.h"

#include <algorithm>
#include <cmath>

#include "../Physics/Interpolation.h"

void MotionPlayer::setClip (const std::vector<Vec3>& framesIn, double controlRateHz)
{
    clipFrames = framesIn;
    clipRateHz = controlRateHz > 0.0 ? controlRateHz : 200.0;
    playHeadFrames = 0.0;
    playing = false;
}

void MotionPlayer::setSpeed (double speedIn)
{
    // Bereich exakt wie Params.h playSpeed (0.25 .. 4.0) - kein eigener
    // Wertebereich hier, damit UI-Regler und DSP nie auseinanderlaufen.
    speed = std::clamp (speedIn, 0.25, 4.0);
}

void MotionPlayer::setLooping (bool shouldLoop)
{
    looping = shouldLoop;
}

void MotionPlayer::trigger (double /*now*/)
{
    if (clipFrames.empty())
        return;

    playHeadFrames = 0.0;
    playing = true;
}

Vec3 MotionPlayer::frameAt (double framePos) const
{
    const int n = (int) clipFrames.size();

    if (n == 0)
        return {};
    if (n == 1)
        return clipFrames[0];

    const int i1 = (int) std::floor (framePos);
    const double frac = framePos - (double) i1;

    if (interp == Interp::Linear)
    {
        const int a = std::clamp (i1, 0, n - 1);
        const int b = std::clamp (i1 + 1, 0, n - 1);
        return clipFrames[(size_t) a] * (1.0 - frac) + clipFrames[(size_t) b] * frac;
    }

    // CatmullRom: p0..p3 um i1..i1+1 herum, Randstützstellen an den Enden
    // gedoppelt (an der Kurve klemmen statt außerhalb zu extrapolieren).
    const int i0c = std::clamp (i1 - 1, 0, n - 1);
    const int i1c = std::clamp (i1,     0, n - 1);
    const int i2c = std::clamp (i1 + 1, 0, n - 1);
    const int i3c = std::clamp (i1 + 2, 0, n - 1);

    return catmullRom (clipFrames[(size_t) i0c], clipFrames[(size_t) i1c],
                        clipFrames[(size_t) i2c], clipFrames[(size_t) i3c], frac);
}

Vec3 MotionPlayer::tick (double dt)
{
    if (! playing || clipFrames.empty())
        return clipFrames.empty() ? Vec3 {} : clipFrames.back();

    const int n = (int) clipFrames.size();
    const double lastFrame = (double) (n - 1);

    playHeadFrames += dt * speed * clipRateHz;

    if (playHeadFrames >= lastFrame)
    {
        if (looping && n > 1)
        {
            // Modulo statt Clamp, damit die Phase über den Loop-Punkt hinweg
            // nicht "hängen bleibt", sondern sauber weiterläuft.
            playHeadFrames = std::fmod (playHeadFrames, lastFrame);
        }
        else
        {
            playHeadFrames = lastFrame;
            playing = false;
        }
    }

    return frameAt (playHeadFrames);
}
