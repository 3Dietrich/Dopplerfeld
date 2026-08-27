#include "Params.h"

#include "Util/Utf8.h"

#include <cmath>

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

    // Höhe über dem Boden. Anders als x/y NICHT normiert, sondern in echten
    // Metern: die Körpergröße eines Hörers ändert sich nicht, wenn man den
    // Feldmaßstab von 100 m auf 10000 m stellt.
    //
    // Unter die Grundflaeche ist erlaubt, solange sie nichts zurueckwirft: dann
    // ist sie nur ein Massstab zum Abschaetzen der Weite und keine Flaeche
    // (@dpa: "und man kann auch z<0 setzen"). Reflektiert sie, haelt die
    // Anzeige die Quelle beim Ziehen darueber, siehe FieldComponent.
    //
    // Ein gewoehnliches setSkewForCentre() geht von einer einseitigen Basis bei
    // 0 aus und verzerrt falsch, sobald der Bereich (wie hier) ueber Null
    // hinaus ins Negative reicht - der Nullpunkt landet nicht in der Mitte des
    // Reglerwegs, und um ihn herum ist die Aufloesung nicht mehr logarithmisch
    // fein (@dpa 20260820: "in der Mitte ist 0, wo ich nun nicht mehr
    // logarithmisch von klein nach gross einstellen kann"). Deshalb hier ein
    // eigenes, bipolares Skew ueber die vier NormalisableRange-Konverter: der
    // Nullpunkt liegt exakt bei normalisiert 0,5, und je Seite wird der
    // Betrag ueber eine Potenzfunktion abgebildet - Exponent > 1 macht die
    // Kurve nahe Null flach (fein) und an den Enden steil (grob), symmetrisch
    // zur Mitte, auch wenn die Grenzen selbst (-1000/+10000) asymmetrisch
    // bleiben. Die Grenzen selbst bleiben unveraendert (keine versteckten
    // Limits) - nur der Weg dorthin wird neu verteilt.
    auto heightRange = []
    {
        constexpr float rangeMin  = -1000.0f;
        constexpr float rangeMax  = 10000.0f;
        constexpr float exponent  = 3.0f;

        return juce::NormalisableRange<float> (
            rangeMin, rangeMax,
            // normalisiert (0..1, 0.5 = Null) -> Wert
            [] (float, float, float norm) -> float
            {
                const float signedNorm = (norm - 0.5f) * 2.0f;   // -1..1
                const float sign       = signedNorm < 0.0f ? -1.0f : 1.0f;
                const float shaped     = std::pow (std::abs (signedNorm), exponent);

                return sign >= 0.0f ? shaped * rangeMax : -shaped * rangeMin;
            },
            // Wert -> normalisiert (0..1), die Umkehrung des obigen
            [] (float, float, float value) -> float
            {
                const float sign   = value < 0.0f ? -1.0f : 1.0f;
                const float shaped = sign >= 0.0f ? value / rangeMax : value / rangeMin;
                const float norm   = std::pow (std::abs (shaped), 1.0f / exponent);

                return (sign >= 0.0f ? norm : -norm) * 0.5f + 0.5f;
            });
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
    layout.add (floatParam (lisYaw, "Listener Yaw", { -180.0f, 180.0f }, 0.0f, Text::utf8 ("°")));
    layout.add (floatParam (earSpacing, "Ear Spacing", { 0.10f, 0.25f, 0.001f }, 0.17f, "m"));

    // Position-Jitter der Quelle M. Default 0m/aus, damit bestehende Presets
    // beim Laden unveraendert klingen. Obergrenze bewusst weit offen statt
    // auf einen "vernuenftigen" Wert gedeckelt (keine versteckten Limits) -
    // Skew haelt den ueblichen, dezenten Bereich trotzdem fein bedienbar.
    {
        // Bereich bis 1000 m (@dpa: "exponentiel, also langsam steigend").
        // Skew-Mittelpunkt bei 1 m statt wie vorher 0,3 m in nur 50 m Bereich -
        // der Regler haengt dadurch noch deutlich laenger im feinen,
        // dezenten Arbeitsbereich (Bruchteile bis wenige Meter) und
        // schwingt erst auf dem letzten Stueck des Wegs in die grossen
        // Ausschlaege hoch.
        auto range = juce::NormalisableRange<float> (0.0f, 1000.0f);
        range.setSkewForCentre (20.0f);
        layout.add (floatParam (srcJitterAmount, "Source Jitter Amount", range, 0.0f, "m"));
    }
    {
        // Tempo des Wacklers: die zweite und letzte Groesse der Bewegung
        // (@dpa 20260825). Sie sagt, WIE SCHNELL sich die Quelle bewegt, der
        // Ausschlag darueber, WIE WEIT. Die Frequenz ergibt sich aus beiden
        // und steht nirgends mehr als Regler.
        //
        // Der Wert ist die SPITZE der Bahngeschwindigkeit, nicht ihr Mittel -
        // nur so laesst er sich mit der Schallgeschwindigkeit vergleichen,
        // und genau darum geht es: ein Wackler ueber 340 m/s loest von sich
        // aus N-Wellen aus.
        //
        // Nach oben derselbe Bereich, den der alte "Jit Max" hatte (keine
        // versteckten Limits, @dpa): wer bei grossem Ausschlag hektisch
        // wackeln will, braucht sehr wohl vierstellige Werte - 50 m Ausschlag
        // bei 3 Hz sind rechnerisch schon 3260 m/s. Skew auf 340, damit der
        // hoerbare Bereich darunter den Grossteil des Reglerwegs behaelt.
        //
        // Default 20 m/s: das ist bei ein paar Metern Ausschlag rund 0,2 Hz,
        // also genau die Trägheit, die vorher als Default-Hektik dastand.
        auto range = juce::NormalisableRange<float> (0.0f, 100000.0f);
        range.setSkewForCentre (340.0f);
        layout.add (floatParam (srcJitterSpeed, "Source Jitter Speed", range, 20.0f, "m/s"));
    }
    // Default an: der Ausschlag steht ohnehin auf 0, das Wackeln beginnt also
    // erst, wenn jemand ihn aufdreht - bestehende Presets klingen unveraendert.
    layout.add (boolParam (srcJitterOn, "Source Jitter On", true));

    // Default 1: der Wackler war bisher auf allen drei Achsen gleich stark,
    // und das soll er ohne Zutun bleiben.
    layout.add (floatParam (srcJitterZAmount, "Source Jitter Z Amount", unitRange(), 1.0f));

    // Einen Tempo-Deckel gibt es nicht mehr: ein Tempo, das man in m/s
    // einstellt, braucht keine Obergrenze gegen sich selbst.
    layout.add (boolParam (masterOn, "On", true));

    // --- Motor ---
    {
        // Skew Richtung niedrige Werte: die Klangänderung beim Hochdrehen ist
        // unten am dichtesten, dort soll der Regler die meiste Auflösung haben.
        //
        // Obergrenze 96000 statt der frueheren 12000 (@dpa 20260826: "max ist
        // derzeit 12000, da ist aber theoretisch noch viel Platz. bitte
        // erweitere es um 2-3 Oktaven"): drei Oktaven, also 8x. 96000 RPM sind
        // 1600 Hz Grundfrequenz - mit den Teiltoenen darueber reicht das bis
        // an den oberen Rand des Hoerbaren, und weiter zu gehen brauchte
        // niemand. Der Skew bleibt bei 1000, der brauchbare Bereich liegt
        // weiterhin unten.
        auto range = juce::NormalisableRange<float> (0.0f, 96000.0f);
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
    // Bereich nach oben erweitert (@dpa: "Noise Gain Lo bitte auch lauter") -
    // das tiefe Rauschband soll auch ueber 0dB hinaus verstaerkbar sein, wie
    // schon der laute Anschlag von noiseGainHi.
    layout.add (floatParam (noiseGainLo, "Noise Gain Lo", { -48.0f, 24.0f, 0.1f }, -24.0f, "dB"));
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
        range.setSkewForCentre (100.0f);
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

    // Dauerschleife des Vorbeifluges, Default aus - ein Flug endet weiterhin
    // von selbst, solange niemand die Schleife einschaltet.
    layout.add (boolParam (flyLoop, "Fly Loop", false));

    // --- Physik ---
    layout.add (floatParam (boomLimitDb, "Boom Limit", { 0.0f, 60.0f, 0.1f }, 30.0f, "dB"));
    layout.add (floatParam (airAbsorbAmount, "Air Absorption", unitRange(), 1.0f));

    // Lufttemperatur (Params::airTempC) - bestimmt c(T) in MediumState, siehe
    // Physics/Medium.h. Grosszuegiger Bereich statt eines "vernuenftigen"
    // Wetterausschnitts (keine versteckten Limits): -60°C deckt die
    // Stratosphaeren-Kaelte in Flughoehe ab, +60°C Extremhitze. Default 20°C
    // bleibt exakt der bisherige feste Wert aus MediumState, damit sich am
    // Klang bestehender Presets nichts aendert.
    layout.add (floatParam (airTempC, "Air Temperature", { -60.0f, 60.0f, 0.1f }, 20.0f, Text::utf8 ("°C")));

    // Hoehe ueber dem Meeresspiegel (Params::airAltitude) - wirkt NICHT auf airTempC (siehe
    // dort), sondern ueber die barometrische Hoehenformel auf die Luftdichte
    // und damit den Ausgangspegel (PluginProcessor::applyParameters,
    // "--- Ausgang ---"). Default 0 m = Meereshoehe, wie MediumState es bisher
    // fest annahm. WICHTIG fuer bestehende Presets: bei den Defaults (20°C,
    // 0 m) ist der physikalische Dichtefaktor rho/rho0 NICHT exakt 1.0
    // (rho0 = 1,225 kg/m^3 gilt bei 15°C, nicht bei 20°C) - er liegt bei rund
    // 0,983. Damit ein frisch geladenes Preset ohne diese beiden Parameter
    // trotzdem exakt wie bisher klingt, wird der Dichte-Pegelfaktor an der
    // Verwendungsstelle (PluginProcessor.cpp) auf den bei den DEFAULTWERTEN
    // gemessenen Faktor normiert (durch densityGain() eines MediumState mit
    // Default-Werten geteilt) statt hier einen festen Korrekturwert
    // einzutragen - so bleibt die Normierung automatisch richtig, falls sich
    // die Defaults je aendern sollten.
    {
        auto range = juce::NormalisableRange<float> (0.0f, 20000.0f);
        range.setSkewForCentre (2000.0f);
        layout.add (floatParam (airAltitude, "Air Altitude", range, 0.0f, "m"));
    }

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
    layout.add (floatParam (wall1Angle, "Wall 1 Angle", { -180.0f, 180.0f, 0.1f }, 0.0f, Text::utf8 ("°")));
    layout.add (floatParam (wall1Tilt,  "Wall 1 Tilt",  { -90.0f, 90.0f, 0.1f }, 0.0f, Text::utf8 ("°")));
    layout.add (floatParam (wall1Damp,  "Wall 1 Damp",  unitRange(), 0.3f));
    layout.add (floatParam (wall1Gain,  "Wall 1 Gain",  { -36.0f, 36.0f, 0.1f }, 0.0f, "dB"));

    layout.add (boolParam  (wall2On,    "Wall 2", false));
    layout.add (floatParam (wall2X,     "Wall 2 X",     unitRange(), 0.5f));
    layout.add (floatParam (wall2Y,     "Wall 2 Y",     unitRange(), 0.05f));
    layout.add (floatParam (wall2Angle, "Wall 2 Angle", { -180.0f, 180.0f, 0.1f }, 0.0f, Text::utf8 ("°")));
    layout.add (floatParam (wall2Tilt,  "Wall 2 Tilt",  { -90.0f, 90.0f, 0.1f }, 0.0f, Text::utf8 ("°")));
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
        // Groessenordnung), nach unten bis 0 m offen (keine versteckten
        // Limits) und nach oben bis 200 m offen. PropagationPath::setNWave()
        // floort den intern verwendeten Wert selbst auf 0,01 m, ein Regler-
        // wert von exakt 0 fuehrt also nicht zu einer Division durch 0.
        auto range = juce::NormalisableRange<float> (0.0f, 200.0f);
        range.setSkewForCentre (15.0f);
        layout.add (floatParam (nWaveSize, "N-Wave Size", range, 15.0f, "m"));
    }
    {
        // Grosszuegig nach oben offen: der Knall DARF uebersteuern, dafuer gibt
        // es den sichtbaren Limiter. 0 dB ist die eingemessene Voreinstellung.
        layout.add (floatParam (nWaveGainDb, "N-Wave Gain", { -36.0f, 36.0f, 0.1f }, 0.0f, "dB"));
    }
    {
        // Schaerfe der Stossfronten. Die Mitte (0,5) ist der Wert, den die
        // Fronten ohne Regler haetten - bestehende Presets, die den Parameter
        // nicht kennen, klingen damit unveraendert. Nach beiden Seiten je fuenf
        // Oktaven Anstiegszeit, siehe PropagationPath::nWaveEdgeOctaves.
        layout.add (floatParam (nWaveEdge, "N-Wave Edge", { 0.0f, 1.0f, 0.01f }, 0.5f, ""));
    }

    // Sinus statt Saegezahn fuer die vier Motor-Teiltoene, Default aus =
    // bisheriges Verhalten.
    // Wellenform je Teilton, Default Saegezahn - bestehende Snapshots klingen
    // unveraendert.
    for (int i = 0; i < 4; ++i)
        layout.add (boolParam (harmSine[i], "Sine " + juce::String (i + 1), false));

    // Sammelschalter der vier Teiltoene, Default AN: bestehende Snapshots
    // klingen unveraendert, und wer den Schalter nie anfasst, merkt nicht,
    // dass es ihn gibt.
    layout.add (boolParam (oscOn, "OSC", true));

    // Pegel der Betriebsart. Grosszuegiger Bereich, denn hier geht es um den
    // Unterschied zwischen einem Modellflugzeug und einem Hubschrauber in drei
    // Metern Abstand (@dpa: "die Lautstaerken sind noch irgendwie voellig
    // unrealistisch"). Default +12 dB: die Betriebsarten sind von sich aus
    // lauter als der freie Modus, weil ihre Pegel aus der Sache kommen und
    // nicht aus vier einzeln gedrehten Teiltoenen.
    layout.add (floatParam (engineLevelDb, "Engine Level", { -36.0f, 36.0f, 0.1f }, 12.0f, "dB"));

    // Bis 4 statt bis 1, genau wie beim Knattern des Rotors, und Default 1
    // statt 0,6 (@dpa 20260825: "die Druckstoesse sind bei mir immer auf 1,
    // leiser machts keinen sinn"). Wenn der ganze bisherige Regelweg unter
    // dem liegt, was man tatsaechlich einstellt, ist der Regelweg falsch
    // gewaehlt und nicht die Einstellung. Skew auf 1, damit die bisherige
    // Obergrenze den halben Reglerweg behaelt.
    {
        auto range = juce::NormalisableRange<float> (0.0f, 4.0f);
        range.setSkewForCentre (1.0f);
        layout.add (floatParam (rocketShock, "Rocket Shock", range, 1.0f));
    }
    // Bis 4 statt bis 1 (@dpa 20260824: "der Control 'Knattern' reicht
    // einfach nicht"). Ueber 1 regelt er nicht mehr nur den Pegel des
    // Schlages, sondern zieht auch die Richtwirkung der Blattspitze hoch -
    // siehe slapSharpness in EngineGenerator. Skew auf 1, damit der
    // physikalisch pure Bereich den halben Reglerweg behaelt.
    {
        auto range = juce::NormalisableRange<float> (0.0f, 4.0f);
        range.setSkewForCentre (1.0f);
        layout.add (floatParam (rotorSlap, "Rotor Slap", range, 0.7f));
    }

    // --- Klangformung der beiden Rausch-Betriebsarten ---
    //
    // Zwei getrennte Vorlagenlisten statt einer gemeinsamen: ein Duesenstrahl
    // und ein Raketenbruellen haben nicht dieselben Klangfarben, und eine
    // Liste, in der die Haelfte der Eintraege nicht zur gewaehlten
    // Betriebsart passt, waere eine Liste zum Wegsortieren.
    //
    // Reihenfolge ist bindend fuer die Tabellen in EngineGenerator.cpp
    // (jetVoiceTable / rocketVoiceTable) - wer hier umsortiert, muss dort
    // mitziehen.
    layout.add (choiceParam (jetVoice, "Jet Voice",
                              juce::StringArray { "Turbofan",
                                                  "Turbojet",
                                                  "Nachbrenner",
                                                  "Ferne",
                                                  "Breit" }, 0));

    layout.add (choiceParam (rocketVoice, "Rocket Voice",
                              juce::StringArray { "Vollschub",
                                                  "Feststoff",
                                                  Text::utf8 ("Zündung"),
                                                  "Ferne",
                                                  "Breit" }, 0));

    // 0,5 = die Vorlage unveraendert, darum genau die Mitte des Reglerwegs.
    layout.add (floatParam (jetTone,    "Jet Tone",    unitRange(), 0.5f));
    layout.add (floatParam (rocketTone, "Rocket Tone", unitRange(), 0.5f));

    // Ausdehnung einer Stosszelle im Raketenstrahl. Untergrenze 10 m
    // (@dpa 20260825: "die Stosslaenge min. 10m sonst klingt es irgendwie
    // unecht"): darunter wird die N-Welle kuerzer als 58 ms, und was so kurz
    // ist, hoert man als Klick statt als Druckstoss - genau das "unecht".
    // Nach oben grosszuegig bis 600 m, das ist das Donnern einer
    // Traegerrakete. Skew auf 30 m, dort liegt der interessante Bereich.
    //
    // Die tatsaechliche Dauer je Stoss streut um diesen Wert herum
    // multiplikativ (Faktor 0,37 bis 2,7, siehe EngineGenerator) - der
    // Regler stellt den Mittelwert, nicht eine feste Groesse.
    {
        auto range = juce::NormalisableRange<float> (10.0f, 600.0f);
        range.setSkewForCentre (30.0f);
        layout.add (floatParam (rocketShockSize, "Rocket Shock Size", range, 20.0f, "m"));
    }

    // Verfaerbung durch die Entfernung, in Oktaven je Verdopplung des
    // Abstands. 0 = die Rakete klingt in zwei Kilometern wie am Startplatz,
    // nur leiser.
    //
    // Default 0,25 statt der anfaenglichen 1,0. Eine ganze Oktave je
    // Verdopplung klingt nach wenig und ist es nicht: in 300 m sind das
    // dreieinhalb Oktaven, und damit landete das Tiefband der Rakete im
    // Infraschall. Gemessen lagen dort 89,5 Prozent der Energie unter 20 Hz -
    // unhoerbar, aber voll ausgesteuert, und uebrig blieb das, was @dpa am
    // 25.08. gehoert hat: "ein kleines Stossen mit hohem Zischen (wie bei
    // einem undichten Ventil am Fahrrad mit 3Bar)".
    //
    // 0,25 Oktaven je Verdopplung sind in 300 m knapp eine Oktave. Das hoert
    // man als "weiter weg", ohne dass der Klang unter die Hoerschwelle
    // rutscht. Bis 3 bleibt der Regler offen - wer die Rakete in den
    // Infraschall schieben will, kann das.
    layout.add (floatParam (rocketFarColour, "Rocket Far Colour", { 0.0f, 3.0f, 0.01f }, 0.25f, "Okt"));

    // Mittlere Folge der Stoesse. Unten einzelne Schlaege, oben ein
    // zusammenhaengender Teppich - das Knattern echter Raketen ("crackle")
    // sitzt bei einigen zehn bis hundert Stoessen je Sekunde. Skew auf den
    // bisherigen Festwert 18 Hz, damit die Voreinstellung dort steht, wo sie
    // vorher fest verdrahtet war.
    {
        auto range = juce::NormalisableRange<float> (0.2f, 800.0f);
        range.setSkewForCentre (18.0f);
        layout.add (floatParam (rocketShockRate, "Rocket Shock Rate", range, 18.0f, "Hz"));
    }

    // Betriebsart des Motors (@dpa 20260824). Reihenfolge ist bindend fuer
    // EngineGenerator::kindWeightTable - nicht umsortieren, ohne dort
    // mitzuziehen. Default "Frei" (Index 0) = bisheriges Verhalten, damit
    // bestehende Presets bitgleich klingen.
    layout.add (choiceParam (engineKind, "Engine Kind",
                              juce::StringArray { "Frei",
                                                  Text::utf8 ("Düsenantrieb"),
                                                  "Raketenantrieb",
                                                  "Hubschrauber",
                                                  "Propeller" }, 0));

    // Rotordrehzahl/Blattzahl des Hubschrauber-Rotors, nur in dieser
    // Betriebsart wirksam. Grosszuegiger Bereich statt eines realistischen
    // Deckels (keine versteckten Limits) - Skew Richtung typischer
    // Hauptrotor-Drehzahl (3-7 Hz).
    {
        auto range = juce::NormalisableRange<float> (0.1f, 50.0f);
        range.setSkewForCentre (5.0f);
        layout.add (floatParam (heliRotorHz, "Heli Rotor", range, 5.0f, "Hz"));
    }
    layout.add (floatParam (heliBladeCount, "Heli Blades", { 2.0f, 8.0f, 1.0f }, 4.0f));

    // Default aus: "aus: wie derzeit: gefaket" (@dpa). Der echte Weg ist der
    // neue, und ob er besser klingt, entscheidet das Ohr - deshalb nicht
    // stillschweigend vorbelegt.
    layout.add (boolParam (heliDoppler, "Heli Doppler", false));

    // Default aus: der frei laufende Rotor erzeugt Schwebungen gegen den
    // Motortakt, und die sind ausdruecklich erwuenscht (@dpa 20260824).
    layout.add (boolParam (heliQuantise, "Heli Quantise", false));
    {
        // Blattlaenge: ein Modellhubschrauber hat einen halben Meter, eine
        // Chinook acht, und wer den Effekt uebertreiben will, dreht weiter.
        // Obergrenze wie in EngineGenerator::maxRotorRadiusM.
        auto range = juce::NormalisableRange<float> (0.1f, 40.0f);
        range.setSkewForCentre (6.0f);
        layout.add (floatParam (heliRotorRadius, "Heli Rotor Radius", range, 6.0f, "m"));
    }

    // Fluegelspanne und Pegel des Propellerpaars. Die Spanne ist grosszuegig
    // nach oben offen: ein Modellflugzeug hat einen Meter, eine Transportmaschine
    // vierzig, und wer den Effekt uebertreiben will, dreht weiter.
    {
        auto range = juce::NormalisableRange<float> (0.0f, 200.0f);
        range.setSkewForCentre (10.0f);
        layout.add (floatParam (propSpan, "Prop Span", range, 10.0f, "m"));
    }
    layout.add (floatParam (propLevelDb, "Prop Level", { -36.0f, 36.0f, 0.1f }, 0.0f, "dB"));

    // Rueckwaerts-Pegel, Stossfront-Absenkung, Schattenausklang und die beiden
    // Wege, einen Bewegungssprung hoerbar zu machen.
    //
    // Bis auf die Stossfront-Absenkung ist ueberall das bisherige Verhalten
    // voreingestellt. Die Absenkung steht voll aufgedreht, weil waehrend einer
    // N-Welle nichts anderes zu hoeren sein soll - auch nicht zwischen Bug- und
    // Heckstoss (@dpa 20260823).
    layout.add (floatParam (reverseGainDb, "Reverse Gain", { -60.0f, 12.0f, 0.1f }, 0.0f, "dB"));
    layout.add (floatParam (shockDuckAmount, "Shock Duck", unitRange(), 1.0f));
    {
        // Reichweite der Absenkung, siehe Params.h. Skew auf 300 m, damit der
        // nahe Bereich - in dem die Stossfront wirklich noch eine ist - den
        // Grossteil des Reglerwegs bekommt. Bis 20 km offen, damit "gilt
        // ueberall" als Reglerstellung erreichbar bleibt und kein Deckel
        // erfunden werden muss.
        auto range = juce::NormalisableRange<float> (0.0f, 20000.0f);
        range.setSkewForCentre (300.0f);
        layout.add (floatParam (shockDuckRange, "Shock Duck Range", range, 300.0f, "m"));
    }

    // Bewegungssprung: Kante durchlassen (aus) und Druckwelle darauf (0).
    // Default nicht mehr 0 (@dpa 20260824: "Knall bei Bewegung/Startvariante
    // ... soll wie der Raketen-Stoss hoerbar sein, ist es aber nicht"). Wer die
    // Startvariante "Knall-Start" waehlt, meint den Knall - der musste bisher
    // in einem anderen Panel erst aufgedreht werden, und ohne das war der
    // Unterschied zur weichen Startvariante nur an der Bahn zu sehen, nicht zu
    // hoeren. Gemessen im load_check-Abschnitt "Knall-Start": bei 1,0 hebt die
    // Druckwelle die Spitze im Ankunftsfenster um Faktor 9,5 an.
    {
        // Bis 4 statt bis 1 (@dpa 20260825: "bei Startknall maximum! das muss
        // mehr wummsen"). Dieselbe Erweiterung wie beim Knattern des Rotors
        // und beim Druckstoss der Rakete, und aus demselben Grund: wenn der
        // ganze bisherige Regelweg unter dem liegt, was man tatsaechlich
        // einstellt, ist der Regelweg falsch gewaehlt. Skew auf 1, damit der
        // bisherige Bereich den halben Reglerweg behaelt.
        //
        // Ueber 1 darf das uebersteuern - dafuer gibt es den sichtbaren
        // Begrenzer.
        auto range = juce::NormalisableRange<float> (0.0f, 4.0f);
        range.setSkewForCentre (1.0f);
        layout.add (floatParam (jumpBoom, "Jump Boom", range, 1.0f));
    }
    {
        // Laenge des Startknalls, und damit seine Haerte (@dpa 20260825: "mehr
        // als eine Beule ist es nicht, bei Startknall maximum! das muss mehr
        // wummsen! mach doch bitte einen knallregler dazu").
        //
        // Vorher hing sie an "N-Wave Size", der Koerpergroesse fuer den
        // Ueberschallknall. Bei den dort ueblichen 15 m dauert die Welle
        // 2 * 15 / 343 = 87 ms, und ihre Energie sitzt damit um 11 Hz - das
        // ist Infraschall. Man spuert eine Beule und hoert keinen Knall, und
        // mehr Pegel macht es nur noch mehr spuerbar.
        //
        // Der Startknall bildet auch gar keinen Koerper ab, sondern eine
        // Beschleunigung. Wie lange die dauert, hat mit der Groesse des
        // Objekts nichts zu tun - deshalb ein eigener Regler.
        //
        // Default 1,5 m sind 8,7 ms und damit rund 115 Hz: ein Schlag, den man
        // hoert. Bis 60 m bleibt der Weg zum Donnern offen.
        auto range = juce::NormalisableRange<float> (0.1f, 60.0f);
        range.setSkewForCentre (2.0f);
        layout.add (floatParam (jumpBoomSize, "Jump Boom Size", range, 1.5f, "m"));
    }
    {
        // Ab 1 ms bis 1 s. Skew unten, denn der interessante Bereich liegt bei
        // wenigen bis einigen zehn Millisekunden.
        //
        // Default 30 ms und nicht die Untergrenze: die rechnerische Dauer
        // faellt bei schnellen Vorbeifluegen praktisch immer auf sie zurueck,
        // und dann entscheidet allein dieser Wert, ob ein Hoerweg ausklingt
        // oder auf seinem Hoehepunkt abreisst - in @dpas Aufnahme vom
        // 20260827 ein Pegelsturz von 17 dB in 2 ms.
        auto range = juce::NormalisableRange<float> (1.0f, 1000.0f);
        range.setSkewForCentre (30.0f);
        layout.add (floatParam (shadowTailMs, "Shadow Tail", range, 30.0f, "ms"));
    }

    // --- Klone ("Schrot") ---
    //
    // Nur noch EINE Zahl: cloneTotal ist die Gesamtzahl der Klone, und alle
    // davon bekommen volle Loeserphysik (@dpa: "nur echte Klones, alles
    // andere weg, keine 'billigen', die bringen nichts"). Eine billige
    // Nachbildung mit eigenem Anteil-Regler und Automatik gibt es seither
    // nicht mehr - die Loeserlast waechst linear mit dieser Zahl, sichtbar am
    // CPU-Balken unten am Fensterrand, immer sichtbar.
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { cloneTotal, 1 }, "Clones", 0, 20, 0));
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
    // Gain der Klone in dB (@dpa: "die klone sind bei Pegel=1 noch zu leise.
    // nenne es einfach 'Gain' und mache die ueblichen -36..36dB"). 0dB =
    // unveraendert.
    layout.add (floatParam (cloneRealLevel, "Clone Gain", { -36.0f, 36.0f, 0.1f }, 0.0f, "dB"));

    // Kein Crossfade-Parameterpaar mehr: die Fadedauer kommt seit
    // @dpa 20260825 ausschliesslich aus dem Anlass (siehe
    // computeFadeSamples). Alte Presets, die "fadeAuto"/"fadeManualMs" noch
    // enthalten, laden weiterhin - unbekannte Eintraege ignoriert die APVTS.

    // --- Ausgang ---
    layout.add (floatParam (panAmount, "Panning", { 0.0f, 100.0f, 1.0f }, 0.0f, "%"));

    layout.add (floatParam (outputGain, "Output Gain", { -36.0f, 36.0f, 0.1f }, 0.0f, "dB"));
    layout.add (boolParam (limiterOn, "Limiter", true));

    return layout;
}
