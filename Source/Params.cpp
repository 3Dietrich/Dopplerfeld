#include "Params.h"

namespace
{
    // Kleine Fabrik, damit `juce::ParameterID { id, 1 }` und die Attribute
    // nicht bei jedem der ~60 Regler ausgeschrieben werden müssen. Der
    // Versionshint bleibt bei 1, solange noch keine Version veröffentlicht
    // wurde, die ihn hochzählen müsste.
    std::unique_ptr<juce::AudioParameterFloat> floatParam (const char* id,
                                                             const juce::String& name,
                                                             juce::NormalisableRange<float> range,
                                                             float defaultValue,
                                                             const juce::String& label = {})
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 },
            name,
            std::move (range),
            defaultValue,
            juce::AudioParameterFloatAttributes().withLabel (label));
    }

    std::unique_ptr<juce::AudioParameterBool> boolParam (const char* id, const juce::String& name, bool defaultValue)
    {
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, defaultValue);
    }

    std::unique_ptr<juce::AudioParameterChoice> choiceParam (const char* id,
                                                               const juce::String& name,
                                                               const juce::StringArray& choices,
                                                               int defaultIndex)
    {
        return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, choices, defaultIndex);
    }

    // Eine normierte 0..1-Range braucht weder Skew noch Einheit; eigener
    // Kurzname, weil sie an mehreren Stellen (Positionen, Loop-Punkte) auftaucht.
    juce::NormalisableRange<float> unitRange()
    {
        return { 0.0f, 1.0f };
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout Params::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // --- Feld ---
    {
        // Skew auf 100 m als Mitte: die meisten Szenen spielen sich zwischen
        // wenigen und ein paar hundert Metern ab, der Regler soll dort fein sein.
        auto range = juce::NormalisableRange<float> (1.0f, 10000.0f);
        range.setSkewForCentre (100.0f);
        layout.add (floatParam (fieldMetres, "Field Size", range, 100.0f, "m"));
    }
    layout.add (floatParam (airTempC, "Air Temperature", { -20.0f, 40.0f, 0.1f }, 20.0f, "°C"));

    // Höhe über dem Boden. Anders als x/y NICHT normiert, sondern in echten
    // Metern: die Körpergröße eines Hörers ändert sich nicht, wenn man den
    // Feldmaßstab von 100 m auf 10000 m stellt. Skew auf 10 m, weil sich die
    // interessanten Höhen (Kopf, Fahrzeug, Dach) alle im einstelligen bis
    // zweistelligen Meterbereich abspielen - oben bleibt trotzdem Platz für
    // Flugzeuge, statt den Regler auf einen "vernünftigen" Wert zu deckeln.
    auto heightRange = []
    {
        // Unter die Grundflaeche ist erlaubt, solange sie nichts zurueckwirft:
        // dann ist sie nur ein Massstab zum Abschaetzen der Weite und keine
        // Flaeche (@dpa: "und man kann auch z<0 setzen"). Reflektiert sie, haelt
        // die Anzeige die Quelle beim Ziehen darueber, siehe FieldComponent.
        //
        // Der Schwerpunkt bleibt bei zehn Metern, damit die uebliche Hoehe fein
        // einstellbar bleibt und die Extreme nicht den halben Reglerweg fressen.
        auto r = juce::NormalisableRange<float> (-1000.0f, 10000.0f);
        r.setSkewForCentre (10.0f);
        return r;
    };

    // --- Quelle ---
    layout.add (floatParam (srcX, "Source X", unitRange(), 0.5f));
    layout.add (floatParam (srcY, "Source Y", unitRange(), 0.5f));
    // Default 0 m: Autos und Motorräder - die häufigsten Szenen - fahren auf
    // dem Boden, und genau dort liegt auch die Reflexionsebene.
    layout.add (floatParam (srcZ, "Source Z", heightRange(), 0.0f, "m"));

    // --- Hörer ---
    layout.add (floatParam (lisX, "Listener X", unitRange(), 0.5f));
    layout.add (floatParam (lisY, "Listener Y", unitRange(), 0.5f));
    // Default 1,75 m: Ohrhöhe eines stehenden Hörers.
    layout.add (floatParam (lisZ, "Listener Z", heightRange(), 1.75f, "m"));
    layout.add (floatParam (lisYaw, "Listener Yaw", { -180.0f, 180.0f }, 0.0f, "°"));
    layout.add (floatParam (earSpacing, "Ear Spacing", { 0.10f, 0.25f, 0.001f }, 0.17f, "m"));

    // Position-Jitter der Quelle M. Default 0m/aus, damit bestehende Presets
    // beim Laden unveraendert klingen. Obergrenzen bewusst weit offen statt
    // auf einen "vernuenftigen" Wert gedeckelt (keine versteckten Limits) -
    // Skew haelt den ueblichen, dezenten Bereich trotzdem fein bedienbar.
    {
        auto range = juce::NormalisableRange<float> (0.0f, 50.0f);
        range.setSkewForCentre (0.3f);
        layout.add (floatParam (srcJitterAmount, "Source Jitter Amount", range, 0.0f, "m"));
    }
    {
        auto range = juce::NormalisableRange<float> (0.01f, 20.0f);
        range.setSkewForCentre (0.3f);
        layout.add (floatParam (srcJitterRateHz, "Source Jitter Rate", range, 0.2f, "Hz"));
    }

    // --- Motor ---
    {
        // Skew Richtung niedrige Werte: die Klangänderung beim Hochdrehen ist
        // unten am dichtesten, dort soll der Regler die meiste Auflösung haben.
        auto range = juce::NormalisableRange<float> (0.0f, 12000.0f);
        range.setSkewForCentre (1000.0f);
        layout.add (floatParam (rpm, "RPM", range, 1000.0f, "RPM"));
    }

    // Verhältnisse bewusst leicht schief (nicht 1/2/3/4), sonst klingt der
    // Motor elektronisch statt mechanisch (Plan 3.10).
    layout.add (floatParam (harmRatio1, "Harm 1 Ratio", { 0.1f, 16.0f, 0.001f }, 1.000f));
    layout.add (floatParam (harmDetune1, "Harm 1 Detune", { -100.0f, 100.0f, 0.1f }, 0.0f, "ct"));
    // Track < 1: dieser Teilton bleibt beim Hochdrehen leicht zurück ("Schlupf").
    layout.add (floatParam (harmTrack1, "Harm 1 Track", unitRange(), 0.85f));
    layout.add (floatParam (harmLevel1, "Harm 1 Level", { -60.0f, 6.0f, 0.1f }, 0.0f, "dB"));

    layout.add (floatParam (harmRatio2, "Harm 2 Ratio", { 0.1f, 16.0f, 0.001f }, 2.017f));
    layout.add (floatParam (harmDetune2, "Harm 2 Detune", { -100.0f, 100.0f, 0.1f }, 0.0f, "ct"));
    layout.add (floatParam (harmTrack2, "Harm 2 Track", unitRange(), 1.0f));
    layout.add (floatParam (harmLevel2, "Harm 2 Level", { -60.0f, 6.0f, 0.1f }, -6.0f, "dB"));

    layout.add (floatParam (harmRatio3, "Harm 3 Ratio", { 0.1f, 16.0f, 0.001f }, 2.981f));
    layout.add (floatParam (harmDetune3, "Harm 3 Detune", { -100.0f, 100.0f, 0.1f }, 0.0f, "ct"));
    layout.add (floatParam (harmTrack3, "Harm 3 Track", unitRange(), 1.0f));
    layout.add (floatParam (harmLevel3, "Harm 3 Level", { -60.0f, 6.0f, 0.1f }, -12.0f, "dB"));

    layout.add (floatParam (harmRatio4, "Harm 4 Ratio", { 0.1f, 16.0f, 0.001f }, 4.043f));
    layout.add (floatParam (harmDetune4, "Harm 4 Detune", { -100.0f, 100.0f, 0.1f }, 0.0f, "ct"));
    layout.add (floatParam (harmTrack4, "Harm 4 Track", unitRange(), 1.0f));
    layout.add (floatParam (harmLevel4, "Harm 4 Level", { -60.0f, 6.0f, 0.1f }, -18.0f, "dB"));

    {
        auto lo = juce::NormalisableRange<float> (20.0f, 5000.0f);
        lo.setSkewForCentre (500.0f);
        layout.add (floatParam (noiseFcLo, "Noise Fc Lo", lo, 400.0f, "Hz"));

        auto hi = juce::NormalisableRange<float> (20.0f, 10000.0f);
        hi.setSkewForCentre (2000.0f);
        layout.add (floatParam (noiseFcHi, "Noise Fc Hi", hi, 3000.0f, "Hz"));
    }
    layout.add (floatParam (noiseGainLo, "Noise Gain Lo", { -60.0f, 0.0f, 0.1f }, -24.0f, "dB"));
    layout.add (floatParam (noiseGainHi, "Noise Gain Hi", { -60.0f, 0.0f, 0.1f }, -6.0f, "dB"));
    layout.add (floatParam (noiseQ, "Noise Q", { 0.1f, 10.0f, 0.01f }, 1.2f));
    layout.add (floatParam (jitterAmount, "Jitter Amount", { 0.0f, 20.0f, 0.01f }, 1.5f, "%"));
    layout.add (floatParam (jitterRateHz, "Jitter Rate", { 3.0f, 15.0f, 0.01f }, 8.0f, "Hz"));
    layout.add (floatParam (imbalance, "Imbalance", unitRange(), 0.0f));
    layout.add (floatParam (imbalanceOctave, "Imbalance Octave", { -2.0f, 6.0f, 1.0f }, 0.0f, "Okt"));

    // --- Sample ---
    // Obergrenze bewusst hoch (@dpa: leise Samples brauchen bei den hohen
    // Dynamiken im Feld mehr Spielraum als der übliche +12dB-Regler).
    layout.add (floatParam (sampleGain, "Sample Gain", { -60.0f, 36.0f, 0.1f }, 0.0f, "dB"));
    layout.add (floatParam (samplePitch, "Sample Pitch", { -24.0f, 24.0f, 0.01f }, 0.0f, "st"));
    // Loop-Punkte normiert (0..1 der geladenen Datei), weil die Länge des
    // Samples zum Zeitpunkt des Layouts noch nicht bekannt ist.
    layout.add (floatParam (loopStart, "Loop Start", unitRange(), 0.0f));
    layout.add (floatParam (loopEnd, "Loop End", unitRange(), 1.0f));
    layout.add (floatParam (loopXfadeMs, "Loop Crossfade", { 2.0f, 20.0f, 0.1f }, 10.0f, "ms"));
    layout.add (floatParam (eqLowGain, "EQ Low", { -24.0f, 24.0f, 0.1f }, 0.0f, "dB"));
    layout.add (floatParam (eqMidGain, "EQ Mid", { -24.0f, 24.0f, 0.1f }, 0.0f, "dB"));
    {
        auto range = juce::NormalisableRange<float> (100.0f, 8000.0f);
        range.setSkewForCentre (1000.0f);
        layout.add (floatParam (eqMidFreq, "EQ Mid Freq", range, 1000.0f, "Hz"));
    }
    layout.add (floatParam (eqHighGain, "EQ High", { -24.0f, 24.0f, 0.1f }, 0.0f, "dB"));

    // --- Bewegung ---
    // Reihenfolge muss zu MotionSmoother-Implementierungen aus Plan 3.8 passen.
    layout.add (choiceParam (smootherType, "Smoother", { "One-Pole", "Critically Damped Spring", "Slew Limiter", "One Euro" }, 1));
    layout.add (floatParam (smootherTau, "Smoother Tau", { 0.001f, 2.0f, 0.0f, 0.4f }, 0.05f, "s"));
    {
        auto vmax = juce::NormalisableRange<float> (0.1f, 1000.0f);
        vmax.setSkewForCentre (50.0f);
        layout.add (floatParam (slewVmax, "Slew Vmax", vmax, 50.0f, "m/s"));

        auto amax = juce::NormalisableRange<float> (0.1f, 5000.0f);
        amax.setSkewForCentre (200.0f);
        layout.add (floatParam (slewAmax, "Slew Amax", amax, 200.0f, "m/s²"));
    }
    layout.add (floatParam (playSpeed, "Play Speed", { 0.25f, 4.0f, 0.0f }, 1.0f, "x"));
    // Catmull-Rom ist der Default, weil der Pfad damit C1-stetig ist und ohne
    // Nachschalten des Smoothers direkt gesetzt werden kann (Plan 3.9).
    layout.add (choiceParam (playInterp, "Play Interp", { "Linear", "Catmull-Rom" }, 1));
    layout.add (boolParam (playLoop, "Play Loop", false));

    {
        // Gemeinsamer Tempo-Deckel fuer JEDE Bewegungsquelle der Quelle -
        // Maus/Automation-Glaettung UND Vorbeiflug gleichermassen (@dpa:
        // "ein 'max Fly speed' fuer alles"). Default so hoch, dass er ohne
        // ausdrueckliches Herunterstellen nichts begrenzt (keine versteckten
        // Limits) - Slew Vmax bleibt daneben als eigener, spezifischerer
        // Regler bestehen (siehe Tooltip dort: Vmax/Amax sind zwei
        // verschiedene Groessen, die sich nicht verlustfrei zu einem Regler
        // verschmelzen lassen).
        auto range = juce::NormalisableRange<float> (1.0f, 100000.0f);
        range.setSkewForCentre (300.0f);
        layout.add (floatParam (globalMaxSpeed, "Max Speed", range, 100000.0f, "m/s"));
    }

    // Vorbeiflug-Generatoren.
    layout.add (choiceParam (flyKind, "Fly Path", { "Durch den Bildschirm", "Waagerecht querend" }, 1));
    layout.add (choiceParam (flyStart, "Fly Start", { "Kontinuierlich", "Knall-Start" }, 0));
    {
        // Skew auf 20 m: der interessante Bereich sind Vorbeifluege in wenigen
        // bis einigen zehn Metern. Nach oben trotzdem bis 2 km offen, statt
        // auf einen "vernuenftigen" Wert zu deckeln.
        auto range = juce::NormalisableRange<float> (0.5f, 2000.0f);
        range.setSkewForCentre (20.0f);
        layout.add (floatParam (flyDistance, "Fly Distance", range, 20.0f, "m"));
    }
    {
        // Anflug-/Abflugstrecke, eigenstaendig statt aus flyDistance
        // abgeleitet (vorher halfLength() = max(100, 6*flyDistance) - ein
        // Regler steuerte damit ungewollt zwei verschiedene Groessen:
        // seitlichen Abstand UND Bahnlaenge/Startpunkt). Default 300 m deckt
        // sich ungefaehr mit dem alten abgeleiteten Wert bei einem mittleren
        // flyDistance und bleibt bei sehr hohen Fluggeschwindigkeiten (>1000
        // m/s) auf Wunsch weiter hochstellbar, damit der Anflug nicht zu kurz
        // fuer eine hoerbare Annaeherung wird.
        auto range = juce::NormalisableRange<float> (10.0f, 5000.0f);
        range.setSkewForCentre (300.0f);
        layout.add (floatParam (flyApproach, "Fly Approach", range, 300.0f, "m"));
    }
    {
        // Bis 1500 m/s, also gut Mach 4 - der Ueberschallfall ist hier der
        // eigentliche Zweck, nicht der Ausnahmefall.
        auto range = juce::NormalisableRange<float> (0.0f, 1500.0f);
        range.setSkewForCentre (60.0f);
        layout.add (floatParam (flySpeed, "Fly Speed", range, 60.0f, "m/s"));
    }

    // --- Physik ---
    layout.add (floatParam (boomLimitDb, "Boom Limit", { 0.0f, 60.0f, 0.1f }, 30.0f, "dB"));
    layout.add (floatParam (airAbsorbAmount, "Air Absorption", unitRange(), 1.0f));

    // Amp-Verlauf über die Entfernung. Symmetrischer Reglerweg um die Mitte,
    // die Mitte ist der Default und trifft den Exponenten 1 exakt - bestehende
    // Presets ohne diesen Parameter klingen damit unverändert.
    layout.add (floatParam (distanceCurve, "Distance Curve", { -1.0f, 1.0f, 0.01f }, 0.0f));

    // Default aus: die Bodenreflexion verdoppelt die Löserlast, und wer sie
    // nicht braucht, soll sie nicht bezahlen.
    layout.add (boolParam (groundReflectionOn, "Ground Reflection", false));
    layout.add (floatParam (groundDampAmount, "Ground Damping", unitRange(), 0.5f));
    layout.add (floatParam (groundGain, "Ground Gain", { -36.0f, 36.0f, 0.1f }, 0.0f, "dB"));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { solverStride, 1 }, "Solver Stride", 1, 16, 1));

    // Wände. Default aus und mit derselben Begründung wie beim Boden: jede
    // eingeschaltete Fläche ist ein weiteres Pfadpaar und damit ein weiteres
    // Mal Löserlast.
    //
    // Die Vorgabepositionen legen die beiden Wände einander gegenüber an den
    // Rand des Feldes (y = 0,05 und y = 0,95), jeweils quer zur Blickrichtung.
    // Wer sie einschaltet, ohne etwas zu verstellen, hört damit sofort etwas -
    // eine Wand mitten durch den Hörer wäre der verwirrendere Startpunkt.
    // Gain je Wand (dB): reiner Amplitudenfaktor, unabhaengig vom Damp-
    // Tiefpass - Default 0dB, bestehende Presets klingen unveraendert.
    // Range +/-36dB wie sampleGain (@dpa: die Waende sollen richtig
    // reinknallen koennen, nicht nur eqXGain-Groessenordnung).
    layout.add (boolParam  (wall1On,    "Wall 1", false));
    layout.add (floatParam (wall1X,     "Wall 1 X",     unitRange(), 0.5f));
    layout.add (floatParam (wall1Y,     "Wall 1 Y",     unitRange(), 0.95f));
    layout.add (floatParam (wall1Angle, "Wall 1 Angle", { -180.0f, 180.0f, 0.1f }, 0.0f, "°"));
    layout.add (floatParam (wall1Tilt,  "Wall 1 Tilt",  { -90.0f, 90.0f, 0.1f }, 0.0f, "°"));
    layout.add (floatParam (wall1Damp,  "Wall 1 Damp",  unitRange(), 0.3f));
    layout.add (floatParam (wall1Gain,  "Wall 1 Gain",  { -36.0f, 36.0f, 0.1f }, 0.0f, "dB"));

    layout.add (boolParam  (wall2On,    "Wall 2", false));
    layout.add (floatParam (wall2X,     "Wall 2 X",     unitRange(), 0.5f));
    layout.add (floatParam (wall2Y,     "Wall 2 Y",     unitRange(), 0.05f));
    layout.add (floatParam (wall2Angle, "Wall 2 Angle", { -180.0f, 180.0f, 0.1f }, 0.0f, "°"));
    layout.add (floatParam (wall2Tilt,  "Wall 2 Tilt",  { -90.0f, 90.0f, 0.1f }, 0.0f, "°"));
    layout.add (floatParam (wall2Damp,  "Wall 2 Damp",  unitRange(), 0.3f));
    layout.add (floatParam (wall2Gain,  "Wall 2 Gain",  { -36.0f, 36.0f, 0.1f }, 0.0f, "dB"));

    // Mehrfachreflexion. Default aus, und hier mit dem staerksten Grund von
    // allen: sie ist erst ab zwei eingeschalteten Flaechen ueberhaupt moeglich
    // und kostet dann bis zu sechs weitere Pfadpaare.
    layout.add (boolParam (reflect2ndOn, "Multi Reflection", false));

    // Pegelfaktor je zusaetzlicher Generation. Unter 1, damit jede weitere
    // Generation garantiert leiser ist als die vorige - die Flaechendaempfung
    // allein leistet das nicht, sie ist ein Tiefpass mit
    // Gleichstromverstaerkung 1 und nimmt nur Hoehen.
    layout.add (floatParam (bounceGain, "Bounce Gain", { 0.0f, 0.95f, 0.01f }, 0.6f));

    // Zusaetzlicher Boost obendrauf, darf anders als bounceGain ueber 0dB
    // hinaus. Range wie bei den Wand-Gains.
    layout.add (floatParam (bounceGainDb, "Bounce Gain Boost", { -36.0f, 36.0f, 0.1f }, 0.0f, "dB"));

    // N-Wellen-Schicht. Default aus - @dpa: "ohne Schalter/Regler sowieso
    // bloed, immer drin wollen die wenigsten".
    layout.add (boolParam (nWaveOn, "N-Wave", false));
    {
        // Groesse/Masse des Koerpers in Metern. Skew auf 15 m (Kampfjet-
        // Groessenordnung), nach unten bis 0,5 m fuer knackige kleine Koerper
        // und nach oben bis 200 m offen.
        auto range = juce::NormalisableRange<float> (0.5f, 200.0f);
        range.setSkewForCentre (15.0f);
        layout.add (floatParam (nWaveSize, "N-Wave Size", range, 15.0f, "m"));
    }

    // --- Klone ("Schrot") ---
    //
    // Zwei getrennte Zahlen statt einer: die Gesamtzahl bestimmt, wie dicht
    // der Schwarm klingt, und davon unabhaengig legt cloneReal fest, wie viele
    // davon echte Loeserphysik bekommen. @dpa will sehen, was er sich
    // einkauft, statt einen stillen Deckel zu bekommen - deshalb ist beides
    // von Hand einstellbar und die Automatik nur ein Angebot.
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { cloneTotal, 1 }, "Clones", 0, 20, 0));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { cloneReal, 1 }, "Clones Real", 0, 20, 2));
    layout.add (boolParam (cloneAuto, "Clones Auto", false));
    {
        // Streuung der Klon-Routen in Metern. Klein gemeint ("die Route weicht
        // um sehr kleine Betraege ab"), nach oben trotzdem weit offen (@dpa:
        // "mehr, weiter, 0 bis 1000 oder so") - Skew bleibt unten fein, oben
        // ist trotzdem Platz fuer einen buchstaeblich auseinandergezogenen
        // Schwarm.
        auto range = juce::NormalisableRange<float> (0.0f, 1000.0f);
        range.setSkewForCentre (15.0f);
        layout.add (floatParam (cloneSpread, "Clone Spread", range, 3.0f, "m"));
    }
    layout.add (floatParam (cloneLevel, "Clone Level", unitRange(), 0.5f));
    layout.add (floatParam (cloneRealLevel, "Clone Real Level", unitRange(), 1.0f));

    // --- Crossfade ---
    layout.add (boolParam (fadeAuto, "Fade Auto", true));
    layout.add (floatParam (fadeManualMs, "Fade Manual", { 5.0f, 500.0f, 0.1f }, 50.0f, "ms"));

    // --- Ausgang ---
    layout.add (floatParam (panAmount, "Panning", { 0.0f, 100.0f, 1.0f }, 0.0f, "%"));

    layout.add (floatParam (outputGain, "Output Gain", { -36.0f, 36.0f, 0.1f }, 0.0f, "dB"));
    layout.add (boolParam (limiterOn, "Limiter", true));

    return layout;
}
