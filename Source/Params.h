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
    // x/y sind auf die Feldfläche normiert (0..1), z ist die Höhe über dem
    // Boden in echten Metern - sie hängt nicht am Feldmaßstab.
    constexpr const char* srcX = "srcX";
    constexpr const char* srcY = "srcY";
    constexpr const char* srcZ = "srcZ";

    // --- Hörer ---
    constexpr const char* lisX        = "lisX";
    constexpr const char* lisY        = "lisY";
    constexpr const char* lisZ        = "lisZ";
    constexpr const char* lisYaw      = "lisYaw";
    constexpr const char* earSpacing  = "earSpacing";

    // Position-Jitter der QUELLE M (Motor/Sender, nicht der Hoerer!) - eine
    // langsame, additive Mikrobewegung um die eingestellte Position, fuer den
    // "echten Chorus" bei Stillstand (@dpa 20260818). Nicht zu verwechseln
    // mit jitterAmount/jitterRateHz weiter unten (Motor--Sektion) - das dort
    // ist Rauschen auf der Motortonhoehe, hier ist es echte Ortsbewegung.
    // srcJitter*-Praefix statt lis*, weil "M" die Quelle meint.
    constexpr const char* srcJitterAmount = "srcJitterAmount";
    constexpr const char* srcJitterRateHz = "srcJitterRateHz";

    // Ein/Aus fuer das Wackeln insgesamt - Quelle UND Klone (@dpa 20260820).
    // Ein eigener Schalter statt "Amount auf 0 drehen", weil der eingestellte
    // Ausschlag beim Abschalten erhalten bleiben soll: aus, hoeren, wieder an,
    // ohne den Regler neu zu suchen.
    constexpr const char* srcJitterOn = "srcJitterOn";

    // Hauptschalter. Aus heisst: sanft ausgeblendet, danach Stille und keine
    // Rechenlast mehr (@dpa 20260821: "Ich brauche unbedingt einen
    // Ausschalter. irgendwie huebsch ausgefadet und dann ist stille (und 0
    // CPU)").
    constexpr const char* masterOn = "masterOn";

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

    // Gemeinsamer Tempo-Deckel fuer jede Bewegungsquelle (Maus/Automation UND
    // Vorbeiflug) - letzte Stufe in DopplerfeldProcessor::advanceMotion(),
    // unabhaengig davon, welcher Smoother/Generator das Ziel geliefert hat.
    constexpr const char* globalMaxSpeed = "globalMaxSpeed";

    // Vorbeiflug-Generatoren (FlyByGenerator): Bahnart, Startvariante,
    // Vorbeiflugabstand in Metern und Geschwindigkeit in m/s. Die
    // Geschwindigkeit ist ausdrücklich live automatisierbar - der Generator
    // integriert den jeweils aktuellen Wert. flyApproach ist von flyDistance
    // entkoppelt: flyDistance ist der seitliche Abstand zu L im Vorbeiflug,
    // flyApproach die Anflug-/Abflugstrecke (halfLength() in FlyByGenerator).
    constexpr const char* flyKind     = "flyKind";
    constexpr const char* flyStart    = "flyStart";
    constexpr const char* flyDistance = "flyDistance";
    constexpr const char* flyApproach = "flyApproach";
    constexpr const char* flySpeed    = "flySpeed";

    // --- Physik ---
    constexpr const char* boomLimitDb     = "boomLimitDb";
    constexpr const char* airAbsorbAmount = "airAbsorbAmount";

    // Amp-Verlauf über die Entfernung: verstellt den Exponenten k in
    // A_geo = 1/R^k. 0 = physikalisch korrektes 1/R (Default und exakt das
    // bisherige Verhalten), positiv = fällt schneller ab, negativ = flacher.
    constexpr const char* distanceCurve   = "distanceCurve";

    // Bodenreflexion (Spiegelquelle an der Ebene z = 0) und ihre Höhendämpfung.
    constexpr const char* groundReflectionOn = "groundReflectionOn";
    constexpr const char* groundDampAmount   = "groundDampAmount";
    constexpr const char* solverStride    = "solverStride";   // nur Debug

    // Wände: unendliche Ebenen, frei im Feld platzierbar. Zwei feste Plätze,
    // keine dynamische Liste - die Pfade müssen im Audiothread allokationsfrei
    // bereitliegen, und mehr als zwei zusätzliche Reflexionen kauft sich kaum
    // jemand freiwillig ein (jede kostet ein weiteres Pfadpaar Löserlast).
    //
    // x/y sind wie alle Feldpositionen normiert (0..1) und geben den Fußpunkt
    // an, durch den die Wand läuft. Angle ist die Richtung der Wandlinie in der
    // Draufsicht, Tilt die Neigung um genau diese Linie (0 = senkrecht
    // stehend, ±90 = flach liegend). Damp ist der Höhenverlust bei der
    // Reflexion, wie beim Boden.
    constexpr const char* wall1On    = "wall1On";
    constexpr const char* wall1X     = "wall1X";
    constexpr const char* wall1Y     = "wall1Y";
    constexpr const char* wall1Angle = "wall1Angle";
    constexpr const char* wall1Tilt  = "wall1Tilt";
    constexpr const char* wall1Damp  = "wall1Damp";
    // Pegel-Regler (dB) der Reflexion, unabhaengig von Damp: Damp ist ein
    // Tiefpass mit Gleichstromverstaerkung 1 (nimmt nur Hoehen), Gain ist ein
    // reiner Amplitudenfaktor - hoert man die Wand trotz Tiefpass zu leise,
    // ist das hier der Regler dafuer, nicht Damp herunterdrehen.
    constexpr const char* wall1Gain  = "wall1Gain";

    constexpr const char* wall2On    = "wall2On";
    constexpr const char* wall2X     = "wall2X";
    constexpr const char* wall2Y     = "wall2Y";
    constexpr const char* wall2Angle = "wall2Angle";
    constexpr const char* wall2Tilt  = "wall2Tilt";
    constexpr const char* wall2Damp  = "wall2Damp";
    constexpr const char* wall2Gain  = "wall2Gain";

    // Mehrfachreflexion: genau eine zusätzliche Generation (Quelle -> Fläche X
    // -> Fläche Y -> Ohr, X != Y), plus der Pegelfaktor je Generation.
    constexpr const char* reflect2ndOn = "reflect2ndOn";
    constexpr const char* bounceGain   = "bounceGain";

    // Zusaetzlicher, unabhaengiger Boost-Gain (dB) obendrauf, der - anders
    // als bounceGain selbst - ausdruecklich ueber 0dB hinaus verstaerken
    // darf. bounceGain bleibt der Generationsfaktor <1 (siehe
    // DopplerEngine::setBounceGain: garantiert, dass jede weitere
    // Reflexionsgeneration leiser wird als die vorige) - das waere gebrochen,
    // wenn er selbst zum Boost umgebaut wuerde. bounceGainDb greift deshalb
    // separat, ebenfalls unabhaengig vom Tiefpass der Flaechendaempfung.
    constexpr const char* bounceGainDb = "bounceGainDb";

    // Druckwellen-/N-Wellen-Schicht für den Überschallknall. Eigener Schalter
    // (Default aus) und ein eigener Größenregler; beides ist ausdrücklich
    // getrennt von boomLimitDb (reine Amplitudendeckelung) und vom
    // Master-Softclip (limiterOn).
    constexpr const char* nWaveOn   = "nWaveOn";
    constexpr const char* nWaveSize = "nWaveSize";

    // Pegel des Knalls in dB. Eigener Regler, weil "wie laut ist ein
    // Ueberschallknall" keine Frage ist, die eine Konstante im Code
    // beantworten darf - je nach Szene soll er die Aufnahme beherrschen oder
    // sich einreihen (@dpa 20260820: "nichts was an einen Schlag oder Druck
    // oder gar nur Lautheit erinnert").
    constexpr const char* nWaveGainDb = "nWaveGainDb";

    // "Schrot"-Muster: Klone der Quelle. cloneTotal ist die Gesamtzahl, und alle
    // davon bekommen volle Loeserphysik - eine billige Nachbildung gibt es
    // nicht mehr (@dpa: "nur echte Klones, alles andere weg, keine 'billigen',
    // die bringen nichts").
    constexpr const char* cloneTotal  = "cloneTotal";
    constexpr const char* cloneSpread = "cloneSpread";

    // --- Crossfade ---
    constexpr const char* fadeAuto     = "fadeAuto";
    constexpr const char* fadeManualMs = "fadeManualMs";

    // Pegel der Bodenreflexion in dB, wie bei den Waenden (@dpa 20260819: "bei
    // Waende habe ich ja Gain, bei Boden noch nicht, was bei tiefem lopass
    // lauter gestellt werden muesste"). Ein Tiefpass, der bis 100 Hz zumacht,
    // nimmt der Reflexion fast die ganze Energie - ohne einen eigenen Pegel
    // waere sie dann schlicht weg statt dumpf.
    // Oktavlage der Unwucht im Motor. 0 ist der Zuendtakt (halbe Grundfrequenz),
    // jede Stufe verdoppelt bzw. halbiert ihn.
    // Gain der Klone in dB (@dpa: "die klone sind bei Pegel=1 noch zu leise").
    // 0 dB = unveraendert, Bereich wie bei outputGain +/-36dB - Klone kamen
    // vorher mit vollem Pegel dazu, sodass schon acht Stueck den Ausgang an den
    // Limiter druecken und der Schwarm zu einem Brei zusammengefahren wird,
    // statt breiter zu klingen.
    constexpr const char* cloneRealLevel = "cloneRealLevel";

    constexpr const char* imbalanceOctave = "imbalanceOctave";

    constexpr const char* groundGain = "groundGain";

    // Anteil des gewoehnlichen Pegel-Pannings, 0..100 % (@dpa 20260819: "bitte
    // noch ein normales Panning fuer die Kopfdrehung anbieten"). 0 laesst das
    // Stereobild allein aus der Ohrgeometrie entstehen, wie zuvor.
    constexpr const char* panAmount = "panAmount";

    // --- Ausgang ---
    constexpr const char* outputGain = "outputGain";
    constexpr const char* limiterOn  = "limiterOn";

    // Der frueher eigenstaendige Regler "Lauter" (0..+36 dB) steckt seit
    // 20260819 in outputGain: dessen Bereich reicht jetzt von -36 bis +36 dB
    // (@dpa: "Lauter soll sich in Output integrieren"). Der Name bleibt hier
    // stehen, weil gespeicherte Zustaende ihn noch enthalten und beim Laden auf
    // outputGain aufaddiert werden (setStateInformation).
    constexpr const char* loudBoostLegacy = "loudBoost";

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
