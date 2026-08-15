#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Alle Parameter-IDs des Plugins an einem Ort (Plan Abschnitt 3.11).
// Tippfehler in einem String-Literal an der Verwendungsstelle würden sonst
// stumm einen nicht existierenden Parameter erzeugen bzw. `nullptr` liefern -
// als `constexpr`-Konstante meldet der Compiler das stattdessen sofort.
namespace Params
{
    // --- Feld ---
    constexpr const char* fieldMetres = "fieldMetres";
    constexpr const char* airTempC    = "airTempC";      // vorbereitet, Phase 1 nicht in der UI

    // --- Quelle ---
    constexpr const char* srcX = "srcX";
    constexpr const char* srcY = "srcY";

    // --- Hörer ---
    constexpr const char* lisX        = "lisX";
    constexpr const char* lisY        = "lisY";
    constexpr const char* lisYaw      = "lisYaw";
    constexpr const char* earSpacing  = "earSpacing";

    // --- Motor ---
    constexpr const char* rpm = "rpm";

    constexpr const char* harmRatio1  = "harmRatio1";
    constexpr const char* harmDetune1 = "harmDetune1";
    constexpr const char* harmTrack1  = "harmTrack1";
    constexpr const char* harmLevel1  = "harmLevel1";

    constexpr const char* harmRatio2  = "harmRatio2";
    constexpr const char* harmDetune2 = "harmDetune2";
    constexpr const char* harmTrack2  = "harmTrack2";
    constexpr const char* harmLevel2  = "harmLevel2";

    constexpr const char* harmRatio3  = "harmRatio3";
    constexpr const char* harmDetune3 = "harmDetune3";
    constexpr const char* harmTrack3  = "harmTrack3";
    constexpr const char* harmLevel3  = "harmLevel3";

    constexpr const char* harmRatio4  = "harmRatio4";
    constexpr const char* harmDetune4 = "harmDetune4";
    constexpr const char* harmTrack4  = "harmTrack4";
    constexpr const char* harmLevel4  = "harmLevel4";

    constexpr const char* noiseFcLo    = "noiseFcLo";
    constexpr const char* noiseFcHi    = "noiseFcHi";
    constexpr const char* noiseGainLo  = "noiseGainLo";
    constexpr const char* noiseGainHi  = "noiseGainHi";
    constexpr const char* noiseQ       = "noiseQ";
    constexpr const char* jitterAmount = "jitterAmount";
    constexpr const char* jitterRateHz = "jitterRateHz";
    constexpr const char* imbalance    = "imbalance";

    // --- Sample ---
    constexpr const char* sampleGain  = "sampleGain";
    constexpr const char* samplePitch = "samplePitch";
    constexpr const char* loopStart   = "loopStart";
    constexpr const char* loopEnd     = "loopEnd";
    constexpr const char* loopXfadeMs = "loopXfadeMs";
    constexpr const char* eqLowGain   = "eqLowGain";
    constexpr const char* eqMidGain   = "eqMidGain";
    constexpr const char* eqMidFreq   = "eqMidFreq";
    constexpr const char* eqHighGain  = "eqHighGain";

    // --- Bewegung ---
    constexpr const char* smootherType = "smootherType";
    constexpr const char* smootherTau  = "smootherTau";
    constexpr const char* slewVmax     = "slewVmax";
    constexpr const char* slewAmax     = "slewAmax";
    constexpr const char* playSpeed    = "playSpeed";
    constexpr const char* playInterp   = "playInterp";
    constexpr const char* playLoop     = "playLoop";

    // --- Physik ---
    constexpr const char* boomLimitDb     = "boomLimitDb";
    constexpr const char* airAbsorbAmount = "airAbsorbAmount";
    constexpr const char* solverStride    = "solverStride";   // nur Debug

    // --- Crossfade ---
    constexpr const char* fadeAuto     = "fadeAuto";
    constexpr const char* fadeManualMs = "fadeManualMs";

    // --- Ausgang ---
    constexpr const char* outputGain = "outputGain";
    constexpr const char* limiterOn  = "limiterOn";

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
