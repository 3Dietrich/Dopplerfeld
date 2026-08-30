#pragma once

#include "../Util/Utf8.h"
#include <juce_core/juce_core.h>

// Zentrale Stelle fuer alle Hilfehinweise (Tooltips) der Oberflaeche, deutsch
// und englisch. Jeder Regler bekommt einen Schluessel statt eines direkten
// Text-Literals; die Sprache ist ein einfacher globaler Zustand in dieser
// Uebersetzungseinheit, umschaltbar per setLanguage()/toggleLanguage().
//
// Reiner Header (siehe CMakeLists.txt-Regel dieses Projekts: neue .cpp-Dateien
// muessten dort eingetragen werden, ein Header nicht). Die Texte liegen als
// rohe C-String-Konstanten in einer switch()-Funktion, nicht in einem
// global konstruierten Container (std::map o.ae.) - ein solcher Container
// wuerde vor main() allozieren, mit undefinierter Reihenfolge gegenueber
// anderen globalen JUCE-Objekten. rawText() alloziert nichts, erst text()
// baut daraus bei Bedarf einen juce::String.
namespace Tooltips
{
    enum class Language { De, En };

    // Ein Schluessel pro Hilfetext. Bei Reglern, die in einer Schleife fuer
    // mehrere Instanzen aufgebaut werden (z.B. die vier Harmonischen, die
    // zwei Waende), teilen sich alle Instanzen einen Schluessel - der
    // Hilfetext ist dort unabhaengig vom Index derselbe, nur die sichtbare
    // Beschriftung (Label) unterscheidet sich.
    enum class Key
    {
        // --- EngineControlPanel ---
        FieldPerspectiveHelp,
        EngineRpm,
        EngineImbalanceOctave,
        EngineImbalance,
        EngineMotorGate,

        // --- EnginePanel ---
        HarmRatio,
        HarmDetune,
        HarmTrack,
        HarmLevel,
        NoiseFcLo,
        NoiseFcHi,
        NoiseGainLo,
        NoiseGainHi,
        NoiseQ,
        JitterAmount,
        JitterRate,
        EngineKind,
        HeliRotorHz,
        HeliBladeCount,
        HeliDoppler,
        HeliQuantise,
        HeliRotorRadius,
        EngineLevel,
        RocketShock,
        RocketFarColour,
        RocketShockSize,
        RocketShockRate,
        JetVoice,
        JetTone,
        RocketVoice,
        RocketTone,
        RotorSlap,
        PropSpan,
        PropLevel,

        // --- FieldPanel ---
        FieldSize,
        BoomLimit,
        AirAbsorb,
        AirTemperature,
        AirAltitude,
        OutputGain,
        Panning,
        DistanceCurve,
        SourceZ,
        ListenerZ,
        SrcJitterAmount,
        SrcJitterSpeed,
        SrcJitterOn,
        SrcJitterSmooth,
        TapChain,
        EngineSine,
        EngineOscOn,
        ShockDuckRange,
        JumpBoom,
        JumpBoomSize,
        SrcJitterZAmount,
        WindLevel,
        NoiseSpeed,
        BoomHold,
        ThrottleFromAccel,
        ThrottleTau,
        MasterOn,
        GroundGain,
        GroundDamp,
        GroundReflection,
        NWaveSize,
        NWaveGain,
        NWaveEdge,
        NWavePressure,
        ExtraPaths,
        NWave,
        LimiterOn,
        LevelMeter,

        // --- Zustandsleiste (PresetBar) ---
        PresetList,
        PresetStep,
        PresetSave,
        PresetSaveAs,
        PresetFolder,

        // --- WallPanel ---
        WallOn,
        WallX,
        WallY,
        WallAngle,
        WallTilt,
        WallDamp,
        WallGain,

        TapSelect,
        TapOn,
        TapX,
        TapY,
        TapZ,
        TapType,
        TapRoom,
        TapDecay,
        TapDamp,
        TapPhase,
        TapEarly,
        TapEchoes,
        TapSeed,
        TapGain,
        TapWidth,
        TapPredelay,
        TapCopy,
        TapPaste,
        DirectGain,
        ReverbBypass,
        SecondOrder,
        BounceGain,
        BounceGainBoost,

        // --- SamplePanel ---
        SampleGain,
        SamplePitch,
        LoopStart,
        LoopEnd,
        LoopXfade,
        EqLow,
        EqMid,
        EqMidFreq,
        EqHigh,
        LoadSample,

        // --- ScopeComponent ---
        Scope,

        // --- MotionPanel ---
        SmootherTau,
        SlewVmax,
        PlaySpeed,
        GlobalMaxSpeed,
        SmootherType,
        PlayInterp,
        PlayLoop,
        Coast,
        LiveTab,
        MouseFrame,
        Record,
        Play,
        FlyKind,
        FlyStart,
        FlyDistance,
        FlyApproach,
        FlySpeed,
        Fly,
        FlyLoop,

        // --- SwarmPanel ---
        CloneTotal,
        CloneSpread,
        CloneRealLevel,
        CloneShow,

        // --- PluginEditor (Kopfzeile, Scope-Toolbar) ---
        SourceButton,
        FieldDrag,
        ViewToggle,
        SpeedUnitToggle,
        EngineReset,
        TooltipsToggle,
        ScopeToggle,
        ScopeFreeze,
        ScopeSync,
        ScopeEventTrigger,
        ScopeHold,
        ScopeZoomIn,
        // Text mit eingeschobener Zahl (maximale Zoom-Zeitbasis in Sekunden) -
        // deshalb in Prefix/Suffix aufgeteilt, die Zahl kommt vom Aufrufer
        // dazwischen (siehe DopplerfeldProcessor::scopeMaxDisplaySeconds).
        ScopeZoomOutPrefix,
        ScopeZoomOutSuffix,
        ScopeSave,
        ScopePlay,
        LanguageToggle,

        // Begrenzer-Marke in der Loeserlast-Zeile (nicht Kopfzeile/Scope-
        // Toolbar wie der Rest dieses Abschnitts, aber ebenfalls direkt in
    };

    namespace detail
    {
        // Funktionslokales static statt eines globalen Objekts - ein
        // globales juce::String/Language-Objekt mit eigener Initialisierung
        // vor main() waere von der Reihenfolge gegenueber anderen
        // Uebersetzungseinheiten abhaengig. Bei einer Funktion, die als
        // `inline` markiert ist, ist die statische lokale Variable ueber
        // alle Uebersetzungseinheiten hinweg garantiert dieselbe Instanz.
        inline Language& currentLanguageRef()
        {
            static Language lang = Language::De;
            return lang;
        }

        // Liefert den rohen Text als C-String-Konstante - keine Allokation,
        // die Zeichenketten liegen als Programmkonstanten im Binary.
        inline const char* rawText (Key key, Language lang)
        {
            switch (key)
            {
                // --- EngineControlPanel ---
                case Key::FieldPerspectiveHelp:
                    return lang == Language::De
                        ? " In der Perspektive: Klick auf den gelben Marker (auch am Bildrand "
                          "oder unten, wenn die Quelle M gerade außerhalb des Blickfelds ist) holt sie "
                          "Quelle an diese Stelle. 2 Finger senkrecht oder Pinch zoomt, 2 Finger "
                          "waagerecht verschiebt den Horizont (mehr oder weniger Boden im Bild) - "
                          "Umschalt+Mausrad geht ebenfalls. '0' setzt Zoom und Horizont zurück. "
                          "'L' oder Doppelklick wechselt zwischen Kamera hinter dem Hörer und "
                          "Kamera aus Hörer-Sicht (Blick entlang seiner Nase)."
                        : " In perspective view: clicking the yellow marker (including at the "
                          "screen edge or at the bottom, when source M is currently outside the field of "
                          "view) moves the source there. Two fingers vertically or a pinch zooms, "
                          "two fingers horizontally shifts the horizon (more or less ground in "
                          "frame) - shift+wheel also works. '0' resets zoom and horizon. 'L' or a "
                          "double click switches between the camera behind the listener and the "
                          "camera from the listener's point of view (looking along the nose).";

                case Key::EngineRpm:
                    return lang == Language::De
                        ? "Drehzahl des Motors. Treibt die Grundfrequenz (f = RPM/60) und färbt "
                          "Rauschband + Jitter mit ein - der zentrale Regler des Motorklangs."
                        : "Engine speed. Drives the fundamental frequency (f = RPM/60) and also "
                          "colours the noise band and jitter - the central control of the engine sound.";
                case Key::EngineImbalanceOctave:
                    return lang == Language::De
                        ? "Oktavlage der Unwucht. 0 ist der Zündtakt, also die halbe "
                          "Grundfrequenz - jede Stufe nach oben verdoppelt sie, jede nach unten "
                          "halbiert sie. Moduliert wird mit einer positiven Welle (0 bis 1), "
                          "damit die Flanken auch in hohen Lagen erhalten bleiben; wie tief sie "
                          "einschneiden, regelt der Unwucht-Regler."
                        : "Octave of the imbalance. 0 is the firing rate, i.e. half the "
                          "fundamental - each step up doubles it, each step down halves it. "
                          "Modulation uses a positive wave (0 to 1) so the edges survive in high "
                          "registers; how deep they cut is set by the imbalance control.";

                case Key::EngineImbalance:
                    return lang == Language::De
                        ? "Zusätzliche Amplitudenmodulation bei der halben Grundfrequenz - simuliert "
                          "den Zündtakt eines Viertakters. 0 = aus."
                        : "Additional amplitude modulation at half the fundamental frequency - "
                          "simulates the firing cycle of a four-stroke engine. 0 = off.";
                case Key::EngineMotorGate:
                    return lang == Language::De
                        ? "Motor klingt nur, während/nachdem die Quelle M gegriffen ist: Start beim "
                          "Greifen, nach dem Loslassen erst zur Ruhe kommen (Nachlauf), "
                          "dann in Ruhe ausfaden (~2,5s). Wirkt nur bei Quelle 'Motor'."
                        : "Engine only sounds while/after source M is grabbed: starts on grab, "
                          "settles after release (coasting), then fades out quietly "
                          "(~2.5s). Only applies to source 'Motor'.";

                // --- EnginePanel ---
                case Key::HarmRatio:
                    return lang == Language::De
                        ? "Frequenzverhältnis dieses Sägezahn-Teiltons zur Grundfrequenz. Bewusst "
                          "nicht ganzzahlig - exakt 1/2/3/4 klingt elektronisch statt mechanisch."
                        : "Frequency ratio of this sawtooth partial to the fundamental. "
                          "Deliberately non-integer - exactly 1/2/3/4 sounds electronic rather "
                          "than mechanical.";
                case Key::HarmDetune:
                    return lang == Language::De
                        ? "Feste Verstimmung dieses Teiltons in Cent, unabhängig von der Drehzahl."
                        : "Fixed detune of this partial in cents, independent of RPM.";
                case Key::HarmTrack:
                    return lang == Language::De
                        ? "Wie stark dieser Teilton der RPM-Aenderung folgt. 100% = exakt "
                          "proportional. Niedriger = er 'schleift hinterher', wodurch sich die "
                          "Teiltöne beim Hochdrehen zueinander verschieben (mechanischer Schlupf)."
                        : "How strongly this partial follows RPM changes. 100% = exactly "
                          "proportional. Lower = it 'lags behind', causing the partials to drift "
                          "apart from each other while revving (mechanical slip).";
                case Key::HarmLevel:
                    return lang == Language::De
                        ? "Lautstärke dieses Teiltons in dB."
                        : "Level of this partial in dB.";
                case Key::NoiseFcLo:
                    return lang == Language::De
                        ? "Mittenfrequenz des Rauschbands bei niedriger Drehzahl (RPM = 0)."
                        : "Centre frequency of the noise band at low RPM (RPM = 0).";
                case Key::NoiseFcHi:
                    return lang == Language::De
                        ? "Mittenfrequenz des Rauschbands bei maximaler Drehzahl. Wirkt nur bei hohen "
                          "RPM hörbar, dazwischen wird linear zwischen Fc Lo und Fc Hi überblendet."
                        : "Centre frequency of the noise band at maximum RPM. Only audible at "
                          "high RPM, crossfades linearly between Fc Lo and Fc Hi in between.";
                case Key::NoiseGainLo:
                    return lang == Language::De
                        ? "Pegel des Rauschbands bei niedriger Drehzahl (dB)."
                        : "Level of the noise band at low RPM (dB).";
                case Key::NoiseGainHi:
                    return lang == Language::De
                        ? "Pegel des Rauschbands bei maximaler Drehzahl (dB) - mehr Drehzahl klingt "
                          "durch mehr Reibungs-/Luftgeräusch heller und lauter."
                        : "Level of the noise band at maximum RPM (dB) - higher RPM sounds "
                          "brighter and louder due to more friction/air noise.";
                case Key::NoiseQ:
                    return lang == Language::De
                        ? "Güte (Schmalbandigkeit) des Rauschband-Filters. Höher = schmaler/toniger."
                        : "Q (narrowness) of the noise band filter. Higher = narrower/more tonal.";
                case Key::JitterAmount:
                    return lang == Language::De
                        ? "Stärke der langsamen Zufallsschwankung auf der Grundfrequenz (in %), "
                          "skaliert mit der Drehzahl - simuliert Lastschwankung/Unwucht statt eines "
                          "starren, toten Tons."
                        : "Strength of the slow random fluctuation on the fundamental frequency "
                          "(in %), scaled with RPM - simulates load fluctuation/imbalance instead "
                          "of a rigid, dead tone.";
                case Key::JitterRate:
                    return lang == Language::De
                        ? "Geschwindigkeit der Jitter-Schwankung in Hz (3-15 Hz, langsames Wackeln)."
                        : "Speed of the jitter fluctuation in Hz (3-15 Hz, slow wobble).";
                case Key::EngineKind:
                    return lang == Language::De
                        ? "Betriebsart des Motors. Jede ist ein eigener Klangerzeuger, keine "
                          "Umgewichtung derselben Bausteine - und das Panel zeigt jeweils nur "
                          "die Regler, die diese Betriebsart auch braucht.\n\n"
                          "'Frei' ist der Motor, wie es ihn immer gab: vier Teiltöne, "
                          "Rauschband, Jitter, Unwucht. Alte Einstellungen liegen hier und "
                          "klingen unverändert.\n\n"
                          "'Düsenantrieb': EIN Verdichterton, hoch und leise, darüber das "
                          "Strahlrauschen, das den Klang trägt. Keine vier Teiltöne - ein "
                          "Triebwerk hat einen Rotor, keine vier.\n\n"
                          "'Raketenantrieb': gar kein Ton, nur Brüllen, dazu die Druckstöße "
                          "aus der Düse (siehe 'Druckstoß').\n\n"
                          "'Hubschrauber': der Verbrennermotor sind die vier Teiltöne, dazu "
                          "der Rotor - ein Schwirren, das mit jedem Blatt an- und abschwillt, "
                          "und ein harter Schlag je Blatt (siehe 'Knattern', 'Rotor Hz', "
                          "'Blätter').\n\n"
                          "'Propeller': ein einzelner leiser Ton plus derselbe Blattschlag, "
                          "weicher - und zwei Schallquellen an den Flügeln statt einer in der "
                          "Mitte (siehe 'Spannweite').\n\n"
                          "Alle außer 'Frei' bekommen zusätzlich das Fahrtwindrauschen: je "
                          "schneller die Quelle fliegt, desto lauter. Beim Wechsel wird kurz "
                          "aus- und wieder eingeblendet, es gibt also keinen Sprung im Signal."
                        : "Engine operating mode. Each one is its own sound generator, not a "
                          "reweighting of the same building blocks - and the panel only shows "
                          "the controls that mode actually needs.\n\n"
                          "'Frei' (free) is the engine as it always was: four partials, noise "
                          "band, jitter, imbalance. Older settings live here and sound "
                          "unchanged.\n\n"
                          "'Düsenantrieb' (jet): ONE compressor tone, high and quiet, with the "
                          "jet noise on top carrying the sound. No four partials - a turbine "
                          "has one rotor, not four.\n\n"
                          "'Raketenantrieb' (rocket): no tone at all, just a roar, plus the "
                          "pressure shocks from the nozzle (see 'Druckstoß').\n\n"
                          "'Hubschrauber' (helicopter): the piston engine is the four "
                          "partials, plus the rotor - a swish swelling and fading with every "
                          "blade, and a hard slap per blade (see 'Knattern', 'Rotor Hz', "
                          "'Blätter').\n\n"
                          "'Propeller': a single quiet tone plus the same blade slap, softer - "
                          "and two sound sources out on the wings instead of one in the middle "
                          "(see 'Spannweite').\n\n"
                          "Everything except 'Frei' also gets airflow noise: the faster the "
                          "source flies, the louder. Switching fades out and back in briefly, "
                          "so there is no jump in the signal.";
                case Key::HeliRotorHz:
                    return lang == Language::De
                        ? "Umlaufgeschwindigkeit des Hubschrauber-Rotors in Hz, unabhängig von "
                          "der Motor-Drehzahl (RPM) - der Rotor hat sein eigenes Tempo. Zusammen "
                          "mit der Blattzahl ergibt sich die Blattschlagfrequenz, das "
                          "charakteristische Wummern. Wirkt nur in Betriebsart 'Hubschrauber'."
                        : "Orbital rate of the helicopter rotor in Hz, independent of the engine "
                          "RPM - the rotor has its own tempo. Together with the blade count this "
                          "gives the blade-passing frequency, the characteristic thump. Only "
                          "applies in 'Hubschrauber' (helicopter) mode.";
                case Key::EngineLevel:
                    return lang == Language::De
                        ? "Gesamtpegel der Betriebsart in dB. Gilt für alles außer 'Frei' - "
                          "dort machen die vier Teilton-Pegel den Pegel, und daran ändert sich "
                          "nichts, damit alte Einstellungen unverändert klingen.\n\n"
                          "Die anderen Betriebsarten brauchen ihn, weil ihre Lautstärke aus "
                          "der Sache kommt und nicht aus vier einzeln gedrehten Reglern: ein "
                          "Hubschrauber in drei Metern Abstand ist ohrenbetäubend, ein "
                          "Modellflugzeug in derselben Entfernung nicht. Der Bereich ist "
                          "großzügig, weil genau dieser Unterschied hier eingestellt wird."
                        : "Overall level of the operating mode in dB. Applies to everything "
                          "except 'Frei' (free) - there the four partial levels make the level, "
                          "and that stays untouched so older settings sound unchanged.\n\n"
                          "The other modes need it because their loudness comes from the thing "
                          "itself rather than from four separately turned knobs: a helicopter "
                          "three metres away is deafening, a model aircraft at the same "
                          "distance is not. The range is generous because that difference is "
                          "exactly what gets set here.";
                case Key::RocketFarColour:
                    return lang == Language::De
                        ? "Wie stark die Entfernung die Klangfarbe nach unten schiebt, in "
                          "Oktaven je Verdopplung des Abstands. 0 = die Rakete klingt in zwei "
                          "Kilometern wie am Startplatz, nur leiser. 1 = in 2 km liegt der "
                          "Klang gut sechs Oktaven tiefer: Energie von weit weg, die man eher "
                          "spürt als hört. Der PEGEL hängt nicht hier dran, den rechnet die "
                          "Ausbreitung mit 1/Abstand - hier geht es allein um die Farbe."
                        : "How strongly distance shifts the timbre downwards, in octaves per "
                          "doubling of range. 0 = the rocket sounds the same at two kilometres "
                          "as on the pad, just quieter. 1 = at 2 km the sound sits a good six "
                          "octaves lower: energy from far away, felt more than heard. LEVEL is "
                          "not affected here - propagation handles that with 1/distance; this "
                          "is purely about colour.";
                case Key::RocketShockSize:
                    return lang == Language::De
                        ? "Nur beim Raketenantrieb: Ausdehnung einer Stoßzelle im Abgasstrahl, "
                          "in Metern. Daraus wird die Dauer der Stoßwelle - der Schall braucht "
                          "die Zelle einmal hin und zurück, also T = 2 x Größe / "
                          "Schallgeschwindigkeit.\n\n"
                          "Klein ist ein Peitschenknall (ein halber Meter sind knapp 3 ms), "
                          "groß ein Donnern. Nach oben offen: eine Modellrakete steht unten, "
                          "eine Trägerrakete weit oben. Die Längen der einzelnen Stöße streuen "
                          "um diesen Wert herum, sonst klänge es nach Maschinengewehr statt "
                          "nach Strahl."
                        : "Rocket mode only: size of a shock cell in the exhaust jet, in "
                          "metres. It sets the duration of the shock wave - sound has to cross "
                          "the cell and come back, so T = 2 x size / speed of sound.\n\n"
                          "Small is a whip crack (half a metre is just under 3 ms), large is a "
                          "roll of thunder. Open at the top: a model rocket sits at the bottom, "
                          "a launch vehicle far up. The individual shocks scatter around this "
                          "value, otherwise it would sound like a machine gun rather than a jet.";

                case Key::RocketShockRate:
                    return lang == Language::De
                        ? "Nur beim Raketenantrieb: mittlere Folge der Druckstöße. Unten "
                          "einzelne Schläge, oben ein zusammenhängender Teppich - das "
                          "Knattern echter Raketen liegt bei einigen zehn bis hundert Stößen "
                          "je Sekunde, weil sich die Wellen dort überlagern.\n\n"
                          "Der Abstand ist bewusst unregelmäßig (zwischen dem halben und dem "
                          "anderthalbfachen mittleren Abstand): Stoßzellen sind keine Maschine "
                          "mit fester Drehzahl."
                        : "Rocket mode only: average rate of the pressure shocks. Single hits "
                          "at the bottom, a continuous carpet at the top - the crackle of real "
                          "rockets sits at a few tens to a hundred shocks per second, because "
                          "that is where the waves start to overlap.\n\n"
                          "The spacing is deliberately irregular (between half and one and a "
                          "half times the mean): shock cells are not a machine running at a "
                          "fixed speed.";

                case Key::JetVoice:
                    return lang == Language::De
                        ? "Nur beim Düsenantrieb: fertige Klangformung des Strahlrauschens. "
                          "Drei Bänder mit je eigenem Pegel - was die Vorlagen unterscheidet, "
                          "ist nicht die Lautstärke, sondern wie die Energie sich auf tief, "
                          "mittig und hoch verteilt.\n\n"
                          "Turbofan ist der moderne Verkehrsjet: der Mantelstrom macht den "
                          "Klang, tief und breit. Turbojet ist die ältere, schärfere Bauart, "
                          "mit dem singenden Verdichter darüber. Nachbrenner brüllt statt zu "
                          "zischen. Ferne ist dasselbe Triebwerk mit von der Luft "
                          "geschluckten Höhen. Breit ist der neutrale Ausgangspunkt zum "
                          "Selberdrehen."
                        : "Jet mode only: ready-made voicing of the jet noise. Three bands "
                          "with individual levels - what separates the presets is not loudness "
                          "but how the energy is spread across low, mid and high.\n\n"
                          "Turbofan is the modern airliner: the bypass stream makes the sound, "
                          "low and wide. Turbojet is the older, sharper design with the "
                          "singing compressor on top. Afterburner roars instead of hissing. "
                          "Distant is the same engine with the highs absorbed by the air. "
                          "Broad is the neutral starting point for dialling in your own.";

                case Key::JetTone:
                    return lang == Language::De
                        ? "Nur beim Düsenantrieb: schiebt stufenlos über die gewählte "
                          "Klangformung - links dunkel, Mitte die Vorlage unverändert, rechts "
                          "hell.\n\n"
                          "Der Regler zieht beides mit, Bänderpegel UND Eckfrequenzen (gut "
                          "eine Oktave in jede Richtung). Nur die Pegel zu kippen klänge nach "
                          "einer Höhenblende; erst die wandernden Frequenzen machen daraus "
                          "einen anderen Klang statt eines lauteren Bandes."
                        : "Jet mode only: sweeps continuously across the chosen voicing - "
                          "dark on the left, the preset untouched in the middle, bright on the "
                          "right.\n\n"
                          "The control moves both band levels AND corner frequencies (a good "
                          "octave either way). Tilting only the levels would sound like a "
                          "treble control; it takes the moving frequencies to make it a "
                          "different sound rather than a louder band.";

                case Key::RocketVoice:
                    return lang == Language::De
                        ? "Nur beim Raketenantrieb: fertige Klangformung des Brüllens, nach "
                          "demselben Dreiband-Muster wie beim Düsenantrieb - nur mit eigenen "
                          "Vorlagen, denn ein Raketenstrahl und ein Düsenstrahl klingen nicht "
                          "gleich.\n\n"
                          "Vollschub ist ein Flüssigkeitstriebwerk unter Last, fast alles "
                          "sitzt unten. Feststoff prasselt statt nur zu wummern, mit deutlich "
                          "mehr Mitten. Zündung ist der Augenblick, in dem der Strahl "
                          "aufreißt, breiter und heller. Ferne ist nur noch Grollen. Breit "
                          "ist der neutrale Ausgangspunkt.\n\n"
                          "Die Druckstöße gehen NICHT durch diese Formung: sie sind N-Wellen, "
                          "und ihre Form ist ihr Klang - ein Filter darüber würde genau die "
                          "senkrechten Fronten abrunden, um die es geht."
                        : "Rocket mode only: ready-made voicing of the roar, using the same "
                          "three-band scheme as the jet - but with its own presets, because a "
                          "rocket plume and a jet exhaust do not sound alike.\n\n"
                          "Full thrust is a liquid engine under load, nearly everything sits "
                          "low. Solid fuel crackles rather than just rumbling, with far more "
                          "midrange. Ignition is the moment the plume tears open, wider and "
                          "brighter. Distant is nothing but a rumble. Broad is the neutral "
                          "starting point.\n\n"
                          "The pressure shocks do NOT run through this voicing: they are "
                          "N-waves, and their shape is their sound - a filter would round off "
                          "exactly the vertical fronts that matter.";

                case Key::RocketTone:
                    return lang == Language::De
                        ? "Nur beim Raketenantrieb: schiebt stufenlos über die gewählte "
                          "Klangformung des Brüllens - links dunkel, Mitte die Vorlage "
                          "unverändert, rechts hell. Wirkt wie beim Düsenantrieb auf "
                          "Bänderpegel und Eckfrequenzen zugleich.\n\n"
                          "Betrifft nur das Brüllen, nicht die Druckstöße."
                        : "Rocket mode only: sweeps continuously across the chosen voicing of "
                          "the roar - dark on the left, the preset untouched in the middle, "
                          "bright on the right. Like the jet control, it moves band levels and "
                          "corner frequencies together.\n\n"
                          "Affects the roar only, not the pressure shocks.";

                case Key::RocketShock:
                    return lang == Language::De
                        ? "Nur beim Raketenantrieb: Stärke der Druckstöße aus der Düse. Der "
                          "Abgasstrahl einer Rakete ist selbst schneller als der Schall, seine "
                          "Stoßzellen knallen - und zwar unabhängig davon, wie schnell die "
                          "Rakete durch die Luft fliegt. Deshalb ist das auch dann zu hören, "
                          "wenn die Rakete noch langsamer als der Schall unterwegs ist.\n\n"
                          "Nicht zu verwechseln mit dem Überschallknall der Ausbreitung (der "
                          "hängt an M_r, der auf den Hörer bezogenen Mach-Zahl, und entsteht "
                          "erst auf dem Weg zum Ohr). Das hier sitzt im Klang der Quelle "
                          "selbst. Die Abstände der Stöße sind bewusst unregelmäßig - "
                          "Stoßzellen sind keine Maschine mit fester Drehzahl."
                        : "Rocket mode only: strength of the pressure shocks from the nozzle. "
                          "A rocket's exhaust jet is itself faster than sound, and its shock "
                          "cells crack - regardless of how fast the rocket travels through the "
                          "air. That is why this is audible even while the rocket itself is "
                          "still slower than sound.\n\n"
                          "Not to be confused with the sonic boom of propagation (which "
                          "depends on M_r, the Mach number relative to the listener, and only "
                          "arises on the way to the ear). This one sits in the sound of the "
                          "source itself. The spacing of the shocks is deliberately irregular - "
                          "shock cells are not a machine running at a fixed speed.";
                case Key::RotorSlap:
                    return lang == Language::De
                        ? "Nur bei Hubschrauber und Propeller: Stärke des Blattknallens. Die "
                          "Blattspitzen laufen viel schneller als der Rumpf und schlagen bei "
                          "jedem Umlauf in die Wirbelschleppe des vorigen Blattes - das ist "
                          "das laute Knattern, das einen Hubschrauber schon ankündigt, bevor "
                          "man ihn sieht.\n\n"
                          "Es kommt zum Schwirren des Rotors hinzu, das ohnehin mit jedem "
                          "Blatt an- und abschwillt. Am Propeller fällt der Schlag deutlich "
                          "weicher aus als am Hubschrauber-Rotor."
                        : "Helicopter and propeller only: strength of the blade slap. The "
                          "blade tips travel far faster than the fuselage and strike the vortex "
                          "left by the previous blade on every revolution - that is the loud "
                          "clatter that announces a helicopter long before you see it.\n\n"
                          "It comes on top of the rotor's swish, which swells and fades with "
                          "every blade anyway. On a propeller the slap is much softer than on a "
                          "helicopter rotor.";
                case Key::PropSpan:
                    return lang == Language::De
                        ? "Nur in Betriebsart 'Propeller': Abstand der beiden Propeller in "
                          "Metern. Sie sitzen an den Flügeln, also quer zur Flugrichtung und "
                          "waagerecht - und weil die Richtung aus der tatsächlich geflogenen "
                          "Bahn kommt, drehen sie sich mit jeder Kurve mit, statt im Raum "
                          "festzustehen. Große Spannweiten machen den Doppler der beiden "
                          "Seiten hörbar verschieden: beim Vorbeiflug kommt der nähere "
                          "Propeller früher und höher. Es sind zwei echte Schallwege mit "
                          "voller Löserphysik (vollständig durch den Ausbreitungs-Löser "
                          "berechnet), keine Verdopplung des Signals - sie kosten entsprechend "
                          "Rechenzeit."
                        : "Propeller mode only: distance between the two propellers in metres. "
                          "They sit on the wings, so across the flight direction and level - "
                          "and since that direction comes from the path actually flown, they "
                          "turn with every curve instead of staying fixed in space. Large "
                          "spans make the doppler of the two sides audibly different: on a "
                          "fly-by the nearer propeller arrives earlier and higher. These are "
                          "two real propagation paths with full solver physics (fully computed "
                          "by the propagation solver), not a doubled signal - they cost "
                          "accordingly.";
                case Key::PropLevel:
                    return lang == Language::De
                        ? "Nur in Betriebsart 'Propeller': Pegel der beiden Propeller in dB. "
                          "Sie kommen zum Rumpfschall HINZU, statt ihn zu ersetzen - wer nur "
                          "die Propeller hören will, nimmt die Motor-Pegel darüber zurück."
                        : "Propeller mode only: level of the two propellers in dB. They are "
                          "added to the hull sound rather than replacing it - to hear only the "
                          "propellers, turn down the engine levels above.";
                case Key::HeliBladeCount:
                    return lang == Language::De
                        ? "Anzahl der Rotorblätter, 2 bis 8. Multipliziert mit der Rotordrehzahl "
                          "ergibt sich die Blattschlagfrequenz - mehr Blätter machen das Wummern "
                          "bei gleicher Rotordrehzahl dichter/schneller. Wirkt nur in Betriebsart "
                          "'Hubschrauber'."
                        : "Number of rotor blades, 2 to 8. Multiplied by the rotor speed this "
                          "gives the blade-passing frequency - more blades make the thump denser/"
                          "faster at the same rotor speed. Only applies in 'Hubschrauber' "
                          "(helicopter) mode.";

                // --- FieldPanel ---
                case Key::HeliDoppler:
                    return lang == Language::De
                        ? "Knattern über echten Doppler statt nachgebauter Modulation. "
                          "Aus: das Schwirren atmet mit der Blattfolge, der Schlag kommt "
                          "je Blatt - eine Modulation, die von der Blickrichtung nichts "
                          "weiß. An: jedes Blatt ist eine eigene Quelle auf der "
                          "Kreisbahn, sein Rauschen läuft durch eine Verzögerung, die "
                          "sich mit seiner Stellung ändert. Dadurch ändert sich das "
                          "Knattern beim Überflug von selbst: von der Seite hart, von "
                          "direkt darunter gleichmäßig."
                        : "Blade slap through real Doppler instead of a modelled "
                          "modulation. Off: the swish breathes with the blade rate and "
                          "the slap comes per blade - a modulation that knows nothing "
                          "about the viewing direction. On: every blade is its own "
                          "source on the circle, its noise runs through a delay that "
                          "changes with its position. The slap then changes during a "
                          "flyover by itself: hard from the side, even from directly "
                          "below.";
                case Key::HeliQuantise:
                    return lang == Language::De
                        ? "Quant: rastet die Rotordrehzahl in das Frequenzraster des "
                          "Motors ein - auf das nächste ganzzahlige Verhältnis zur "
                          "Grundfrequenz (RPM/60), nach oben wie nach unten.\n\n"
                          "Die Unwucht sitzt seit jeher auf einem festen Verhältnis zur "
                          "Grundfrequenz und klingt deshalb wirksam. Der Rotor lief frei "
                          "dagegen: Blattfolge und Motortakt gingen aneinander vorbei, "
                          "und was man hörte, war eine wandernde Schwebung.\n\n"
                          "Aus bleibt genau diese Schwebung erhalten - zwei leicht "
                          "verschiedene Tempi ergeben Verläufe zwischen hart und weich, "
                          "die es eingerastet nicht gibt. Der Regler behält seinen Wert; "
                          "gerastet wird nur, was daraus wird."
                        : "Quant: locks the rotor speed into the engine's frequency "
                          "grid - to the nearest whole-number ratio of the fundamental "
                          "(RPM/60), upwards as well as downwards.\n\n"
                          "The imbalance has always sat at a fixed ratio to the "
                          "fundamental, which is why it sounds effective. The rotor ran "
                          "freely against it: blade rate and engine cycle passed each "
                          "other by, and what you heard was a drifting beat.\n\n"
                          "Off keeps exactly that beat - two slightly different tempos "
                          "give shifts between hard and soft that locking removes. The "
                          "control keeps its value; only what comes of it is snapped.";
                case Key::HeliRotorRadius:
                    return lang == Language::De
                        ? "Blattlänge. Sie bestimmt, wie weit die Laufzeit eines Blattes "
                          "je Umlauf schwankt (2r/c bei flacher Sicht) und damit, wie "
                          "tief der Doppler-Effekt geht. Wirkt nur bei eingeschaltetem "
                          "Doppler."
                        : "Blade length. It sets how far a blade's travel time varies "
                          "per revolution (2r/c seen edge-on) and therefore how deep the "
                          "Doppler effect goes. Only takes effect with Doppler on.";
                case Key::FieldSize:
                    return lang == Language::De
                        ? "Breite der Feldfläche in Metern (1-10000) - der Maßstab des 700x400px-"
                          "Feldes. Aenderung überblendet weich (kein Klick), Positionen bleiben "
                          "normiert erhalten."
                        : "Width of the field area in metres (1-10000) - the scale of the "
                          "700x400px field. Change crossfades smoothly (no click), positions "
                          "stay normalized.";
                case Key::BoomLimit:
                    return lang == Language::De
                        ? "Regularisierung der Amplitude bei Überschall (1-M_r nahe 0, M_r = die "
                          "auf den Hörer bezogene Mach-Zahl der Quelle). Kleinere Werte = "
                          "stärkerer, spitzerer Knall; größere Werte = sanftere Begrenzung."
                        : "Regularization of the amplitude at supersonic speed (1-M_r near 0, "
                          "M_r = the source's Mach number relative to the listener). Smaller "
                          "values = a stronger, sharper boom; larger values = softer limiting.";
                case Key::AirAbsorb:
                    return lang == Language::De
                        ? "Stärke der distanzabhängigen Luftdämpfung (Höhenverlust über "
                          "Entfernung). 0 = aus, 1 = voll."
                        : "Strength of the distance-dependent air absorption (loss of highs over "
                          "distance). 0 = off, 1 = full.";
                case Key::AirTemperature:
                    return lang == Language::De
                        ? "Lufttemperatur in Grad Celsius. Bestimmt die Schallgeschwindigkeit c und "
                          "damit direkt die Überschall-Schwelle (Mach 1) - kalte Luft in großer "
                          "Höhe hat ein niedrigeres c als warme Luft am Boden. Unabhängig vom "
                          "Höhenregler daneben: wer einen Düsenjet in 10 km darstellen will, "
                          "stellt hier die dort herrschende Kälte UND dort die Höhe ein."
                        : "Air temperature in degrees Celsius. Sets the speed of sound c and "
                          "therefore the supersonic threshold (Mach 1) directly - cold air at "
                          "altitude has a lower c than warm air at ground level. Independent of "
                          "the altitude control next to it: to portray a jet at 10 km, set both "
                          "the cold there AND the altitude there.";
                case Key::AirAltitude:
                    return lang == Language::De
                        ? "Höhe des ganzen Schauplatzes über dem Meeresspiegel, in Metern. "
                          "Nicht zu verwechseln mit 'Source Z' und 'Listener Z' - die sagen, wie "
                          "hoch Quelle und Hörer über dem Boden stehen, dieser Regler sagt, wo "
                          "dieser Boden liegt.\n\n"
                          "Wirkt NICHT auf die Temperatur (eigener Regler daneben), sondern über "
                          "den mit der Höhe fallenden Luftdruck auf die Luftdichte - und damit "
                          "als reiner Pegelfaktor auf den Ausgang: in dünner Höhenluft ist alles "
                          "leiser. So lässt sich die Peitsche knapp über dem Boden (dichte Luft) "
                          "vom Düsenjet in großer Höhe (dünne, kalte Luft - Temperatur separat "
                          "einstellen) unterscheiden."
                        : "Height of the whole scene above sea level, in metres. Not to be "
                          "confused with 'Source Z' and 'Listener Z' - those say how high source "
                          "and listener stand above the ground; this one says where that ground "
                          "is.\n\n"
                          "Does NOT affect temperature (its own control next to it), but lowers "
                          "air density as pressure drops with altitude - acting as a pure level "
                          "factor on the output: thin high-altitude air makes everything "
                          "quieter. This is how a whip crack near the ground (dense air) differs "
                          "from a jet at high altitude (thin, cold air - set temperature "
                          "separately).";
                case Key::OutputGain:
                    return lang == Language::De
                        ? "Ausgangslautstärke, -36 bis +36 dB. Der Pegel folgt 1/Abstand ohne "
                          "Referenzdistanz - bei großen Feldern ist das leise, hier lässt sich "
                          "gegensteuern. Der Bereich nach oben deckt die hohe Dynamik des "
                          "Doppler-Materials ab: leiser Direktschall neben lauten Überschall-Knallen."
                        : "Output level, -36 to +36 dB. Level follows 1/distance with no "
                          "reference distance - quiet in large fields, this is where to "
                          "compensate. The upper range covers the high dynamic range of the "
                          "doppler material: quiet direct sound next to loud sonic booms.";
                case Key::Panning:
                    return lang == Language::De
                        ? "Anteil eines gewöhnlichen Panorama-Reglers, 0 bis 100 %. Bei 0 entsteht das "
                          "Stereobild allein aus der Ohrgeometrie - dort verschiebt eine Kopfdrehung vor "
                          "allem die Laufzeit, der Pegelunterschied zwischen den Ohren ist bei weiter "
                          "Quelle winzig. Höhere Werte legen den Pegelunterschied darüber. Gerechnet "
                          "wird mit der Richtung, aus der der Schall wirklich kommt, nicht mit der "
                          "aktuellen Position - jede Spiegelung bekommt so ihre eigene Seite."
                        : "Share of an ordinary pan control, 0 to 100 %. At 0 the stereo image "
                          "comes purely from ear geometry - there, a head turn mainly shifts "
                          "arrival time, the level difference between the ears is tiny for a "
                          "distant source. Higher values add the level difference on top. "
                          "Calculated from the direction the sound actually arrives from, not "
                          "from the current position - each reflection gets its own side this way.";
                case Key::DistanceCurve:
                    return lang == Language::De
                        ? "Wie stark die Entfernung auf die Lautstärke wirkt. Mitte (0) = "
                          "physikalisch korrektes 1/R, wie bisher. Nach rechts: fällt schneller "
                          "ab (schärfer abgegrenzt). Nach links: fällt flacher ab (trägt "
                          "weiter, verschwimmt mehr)."
                        : "How strongly distance affects loudness. Centre (0) = physically "
                          "correct 1/R, as before. To the right: falls off faster (sharper "
                          "boundary). To the left: falls off flatter (carries further, blurs more).";
                case Key::SourceZ:
                    return lang == Language::De
                        ? "Höhe der Quelle über dem Boden in Metern. 0 = auf dem Boden (Auto, "
                          "Motorrad), größere Werte für Ueberflug o. Ä. x/y stellt man mit der "
                          "Maus im Feld ein, die Höhe nur hier."
                        : "Height of the source above the ground in metres. 0 = on the ground "
                          "(car, motorcycle), larger values for fly-overs etc. x/y are set with "
                          "the mouse in the field, height only here.";
                case Key::ListenerZ:
                    return lang == Language::De
                        ? "Ohrhöhe des Hörers über dem Boden in Metern (Standard 1.75 = stehend). "
                          "Erst ein Höhenunterschied zwischen Quelle und Hörer macht die "
                          "Bodenreflexion hörbar."
                        : "Ear height of the listener above the ground in metres (default "
                          "1.75 = standing). Only a height difference between source and "
                          "listener makes the ground reflection audible.";
                case Key::SrcJitterAmount:
                    return lang == Language::De
                        ? "Auslenkung einer langsamen, ständigen Mikrobewegung der Schallquelle M in "
                          "Metern, 0 bis 1000 - 0 = aus (Default). Kennlinie stark exponentiell: "
                          "der untere, dezente Arbeitsbereich (Bruchteile bis wenige Meter) ist "
                          "fein aufgelöst, die großen Ausschläge liegen erst ganz am Ende des "
                          "Reglerwegs. Wirkt immer additiv, auch während normaler Bewegung; im "
                          "Stillstand ist es der 'echte Chorus', bei Bewegung geht es im normalen "
                          "Doppler unter."
                        : "Amplitude of a slow, continuous micro-movement of source M in "
                          "metres, 0 to 1000 - 0 = off (default). Strongly exponential curve: "
                          "the lower, subtle working range (fractions up to a few metres) is "
                          "finely resolved, the large excursions only appear at the very end of "
                          "the control's travel. Always acts additively, even during normal "
                          "movement; at standstill it is the 'real chorus', during movement it "
                          "gets absorbed into the normal doppler.";
                case Key::SrcJitterSpeed:
                    return lang == Language::De
                        ? "Wie SCHNELL sich die Quelle beim Wackeln bewegt, in m/s. Zusammen "
                          "mit dem Ausschlag daneben (wie WEIT) beschreibt das die Bewegung "
                          "vollständig - die Frequenz ergibt sich aus beiden und ist kein "
                          "Regler mehr.\n\n"
                          "Vorher standen hier 'Hektik' (eine Frequenz) und 'Jit Max' (eine "
                          "Tempogrenze). Beide hingen mit dem Ausschlag multiplikativ zusammen: "
                          "wer einen der drei drehte, verschob die Wirkung der anderen beiden, "
                          "und das passende Fenster war kaum zu finden. Jetzt bedeutet jeder "
                          "Regler genau eine Sache.\n\n"
                          "Gemeint ist die SPITZE: schneller als hier eingestellt wird die "
                          "Quelle nie. Damit lässt sich der Wert direkt mit der "
                          "Schallgeschwindigkeit vergleichen - über 340 m/s löst das Wackeln "
                          "von sich aus Überschallknalle aus. Die meiste Zeit läuft die "
                          "Bewegung langsamer, sie soll ja atmen und nicht kreiseln.\n\n"
                          "0 friert sie ein - dann bleibt die Quelle stehen, wo sie gerade "
                          "ist, auch wenn der Ausschlag verstellt wird."
                        : "How FAST the source moves while jittering, in m/s. Together with "
                          "the excursion next to it (how FAR) this describes the motion "
                          "completely - frequency follows from the two and is no longer a "
                          "control.\n\n"
                          "This used to be 'Agitation' (a frequency) and 'Jit Max' (a speed "
                          "limit). Both were multiplicatively tied to the excursion: turning "
                          "any one of the three shifted what the other two did, and the right "
                          "window was hard to find. Now each control means exactly one thing."
                          "\n\n"
                          "This is the PEAK: the source never moves faster than the value "
                          "set here. That makes it directly comparable with the speed of "
                          "sound - above 340 m/s the jitter sets off sonic booms by itself. "
                          "Most of the time the motion runs slower; it is meant to breathe, "
                          "not to spin.\n\n"
                          "0 freezes it - the source then stays where it is, even if the "
                          "excursion is changed.";
                case Key::MasterOn:
                    return lang == Language::De
                        ? "Hauptschalter. Aus blendet in gut einer Zehntelsekunde aus, danach "
                          "ist Stille - und es wird auch nichts mehr gerechnet, die "
                          "CPU-Anzeige geht auf null. Beim Einschalten setzt die Bahn neu auf, "
                          "denn während der Stille lief keine Bewegung mit."
                        : "Main switch. Off fades out over about a tenth of a second, then "
                          "there is silence - and nothing is computed any more, the CPU meter "
                          "drops to zero. Switching back on restarts the trajectory, since no "
                          "motion was recorded while it was silent.";
                case Key::EngineOscOn:
                    return lang == Language::De
                        ? "Die vier Oszillatoren zusammen stumm - zum Hören, was ohne sie "
                          "noch alles läuft: Rauschband, Fahrtwind, Unwucht und beim "
                          "Hubschrauber der Rotor. Ein reiner Mute, kein Pegel: die vier "
                          "Level-Regler behalten ihre Werte und stehen beim Wiedereinschalten "
                          "unverändert da. Wird überblendet, nicht geschaltet, und die Phasen "
                          "laufen weiter - kein Knacks in beide Richtungen."
                        : "Mutes the four oscillators together - to hear what is still "
                          "running without them: noise band, slipstream, imbalance and, on the "
                          "helicopter, the rotor. A pure mute, not a level: the four level "
                          "controls keep their values and are unchanged when switched back on. "
                          "Crossfaded rather than switched, with the phases running on - no "
                          "click in either direction.";
                case Key::EngineSine:
                    return lang == Language::De
                        ? "Wellenform DIESES Teiltons. Aus = Sägezahn (obertonreich, der "
                          "bisherige Motorklang), an = reiner Sinus. Jeder der vier Teiltöne "
                          "hat seinen eigenen Schalter, sie lassen sich also mischen: zwei "
                          "Sägezähne für den rauhen Kern, zwei Sinus für die reinen Obertöne "
                          "darüber. Umgeschaltet wird überblendet, und der Sinus kommt aus "
                          "derselben Phase wie der Sägezahn - kein Knacks, kein Tonhöhensprung."
                        : "Waveform of THIS partial. Off = sawtooth (harmonically rich, the "
                          "engine sound so far), on = pure sine. Each of the four partials has "
                          "its own switch, so they can be mixed: two sawtooths for the rough "
                          "core, two sines for the clean overtones above. Switching crossfades, "
                          "and the sine comes from the same phase as the sawtooth - no click, "
                          "no jump in pitch.";
                case Key::PresetList:
                    return lang == Language::De
                        ? "Gespeicherte Zustände aus dem gewählten Ordner. Auswählen "
                          "lädt sofort - es sind dieselben Dateien, die die "
                          "Standalone-App über ihr Optionen-Menü schreibt, nur ohne "
                          "dessen Dateidialog."
                        : "Saved states from the chosen folder. Selecting loads "
                          "immediately - these are the same files the standalone app "
                          "writes from its options menu, just without its file dialog.";
                case Key::PresetStep:
                    return lang == Language::De
                        ? "Einen Zustand zurück oder vor, direkt geladen. Damit lässt "
                          "sich eine Sammlung durchhören, ohne die Liste zu öffnen."
                        : "One state back or forward, loaded straight away. Lets you "
                          "listen through a collection without opening the list.";
                case Key::PresetSave:
                    return lang == Language::De
                        ? "Überschreibt den gewählten Zustand mit dem aktuellen Stand. "
                          "Ist keiner gewählt, wird nach einem Namen gefragt."
                        : "Overwrites the selected state with the current settings. If "
                          "none is selected, asks for a name instead.";
                case Key::PresetSaveAs:
                    return lang == Language::De
                        ? "Sichert den aktuellen Stand unter einem neuen Namen im "
                          "gewählten Ordner. Nur eine Namensabfrage, kein Dateidialog."
                        : "Saves the current settings under a new name in the chosen "
                          "folder. Just a name prompt, no file dialog.";
                case Key::PresetFolder:
                    return lang == Language::De
                        ? "Ordner mit den Zuständen wählen. Wird gemerkt - das ist der "
                          "einzige Dateidialog, der hier noch vorkommt."
                        : "Choose the folder holding the states. It is remembered - the "
                          "only file dialog left here.";
                case Key::ShockDuckRange:
                    return lang == Language::De
                        ? "Bis zu welcher Entfernung die Absenkung voll wirkt. Näher "
                          "als das ist die Stoßfront eine echte Unstetigkeit und nimmt "
                          "alles andere mit - zwischen den beiden Knallen bleibt es "
                          "still. Doppelt so weit wirkt sie noch halb, dahinter kommt "
                          "das Grollen samt allem drumherum. 0 = gilt überall gleich."
                        : "The distance up to which the ducking works at full depth. "
                          "Closer than that the shock front is a real discontinuity and "
                          "takes everything else with it - between the two bangs it "
                          "stays silent. At twice the distance it is at half depth, "
                          "beyond that the rumble arrives along with everything around "
                          "it. 0 = applies everywhere equally.";
                case Key::JumpBoomSize:
                    return lang == Language::De
                        ? "Länge des Startknalls in Metern - kurz knallt, lang wummert. "
                          "1,5 m sind knapp 9 ms und damit ein hörbarer Schlag; ab etwa 15 m "
                          "rutscht die Welle unter die Hörschwelle und ist nur noch als "
                          "Druckbeule zu spüren."
                        : "Length of the start bang in metres - short cracks, long rumbles. "
                          "1.5 m is just under 9 ms and gives an audible hit; from about 15 m "
                          "the wave drops below the hearing threshold and is only felt as a "
                          "pressure bulge.";
                case Key::JumpBoom:
                    return lang == Language::De
                        ? "Lautstärke des Knalls beim Losfliegen. Wirkt nur bei der "
                          "Startvariante 'Knall-Start'. 0 = aus."
                        : "Loudness of the bang when the fly-by starts. Only has an effect "
                          "with start mode 'Bang Start'. 0 = off.";
                case Key::SrcJitterZAmount:
                    return lang == Language::De
                        ? "Wie weit die Höhe mitwackelt, als Anteil am eingestellten "
                          "Ausschlag. 1 = z wackelt genauso weit wie x und y, 0 = gar "
                          "nicht, die Quelle bleibt auf ihrer eingestellten Höhe und "
                          "wackelt nur in der Ebene.\n\n"
                          "Ein Anteil statt einer eigenen Auslenkung: \"Jitter\" bleibt "
                          "der eine Regler für die Größe der Bewegung, dieser hier sagt "
                          "nur, ob sie flach liegt oder den Raum füllt.\n\n"
                          "Derselbe Regler steht im Schwarm-Panel neben \"Streuung\" und "
                          "gilt dort für die Höhe der Klone - ein Parameter, zwei Räder, "
                          "immer gleich."
                        : "How far the height joins the wobble, as a share of the set "
                          "amount. 1 = z wobbles as far as x and y, 0 = not at all, the "
                          "source stays at its set height and wobbles only in the "
                          "plane.\n\n"
                          "A share rather than its own amount: \"Jitter\" stays the one "
                          "control for the size of the movement, this one only says "
                          "whether it lies flat or fills the space.\n\n"
                          "The same control sits next to \"Spread\" in the swarm panel and "
                          "governs the height of the clones there - one parameter, two "
                          "knobs, always in step.";
                case Key::TapChain:
                    return lang == Language::De
                        ? "Schickt diesen Abgriffpunkt in einen spaeteren, statt ihn direkt auf die "
                          "Ohren zu geben - aus zwei Hallbauarten wird eine Kette. Der Empfaenger "
                          "hoert dann nicht mehr das Feld, sondern nur noch diesen Punkt; seine "
                          "Lage im Feld spielt dann keine Rolle mehr. Nur nach hinten, damit kein "
                          "Kreis entsteht."
                        : "Feeds this tap into a later one instead of sending it straight to the "
                          "ears - two reverb types become a chain. The receiving tap no longer "
                          "hears the field, only this tap; its position then no longer matters. "
                          "Forward only, so no loop can form.";

                case Key::SrcJitterSmooth:
                    return lang == Language::De
                        ? "Schickt das Wackeln durch die Bewegungsglaettung, statt daran vorbei. "
                          "Aus: voller Ausschlag, wie eingestellt. An: runder und traeger, mit der "
                          "Zeitkonstante der Glaettung und unter dem Tempo-Deckel."
                        : "Runs the wobble through the motion smoothing instead of past it. Off: "
                          "full amount as set. On: rounder and more sluggish, with the smoothing's "
                          "time constant and under the speed cap.";

                case Key::SrcJitterOn:
                    return lang == Language::De
                        ? "Schaltet das Wackeln der Schallquelle M und aller Klone komplett ab. Die "
                          "Regler Jitter/Hektik behalten dabei ihren Wert - beim Wiedereinschalten "
                          "wackelt es sofort mit dem alten Ausschlag weiter, statt bei null neu "
                          "anzufangen."
                        : "Switches the wobble of source M and all clones off completely. The "
                          "Jitter/Hektik controls keep their value while off - switching back on "
                          "resumes wobbling at the previous amount instead of starting from zero.";
                case Key::GroundGain:
                    return lang == Language::De
                        ? "Pegel der Bodenreflexion in dB. Eigener Regler neben der Dämpfung, "
                          "weil ein Tiefpass, der bis 100 Hz zumacht, der Reflexion fast die "
                          "ganze Energie nimmt - ohne Nachregeln wäre sie dann weg statt dumpf."
                        : "Level of the ground reflection in dB. A separate control next to the "
                          "damping, because a low-pass closing down to 100 Hz takes almost all "
                          "energy out of the reflection - without turning it up it would be gone "
                          "rather than dull.";

                case Key::GroundDamp:
                    return lang == Language::De
                        ? "Wie stark der Boden bei der Reflexion die Höhen schluckt. 0 = ideal "
                          "harte Fläche (Reflexion klingt wie der Direktschall), 1 = weicher Boden "
                          "(Gras/Erde). Wirkt nur auf den gespiegelten Pfad, nicht auf den "
                          "Direktschall - und nur bei eingeschalteter Bodenreflexion."
                        : "How strongly the ground absorbs highs on reflection. 0 = ideal hard "
                          "surface (reflection sounds like the direct sound), 1 = soft ground "
                          "(grass/earth). Only affects the mirrored path, not the direct "
                          "sound - and only while ground reflection is enabled.";
                case Key::GroundReflection:
                    return lang == Language::De
                        ? "Zweiter Ausbreitungsweg pro Ohr über den Boden (Spiegelquelle an der Ebene "
                          "z=0), mit eigener Laufzeit, eigenem Doppler und eigener Dämpfung. "
                          "Achtung: bei Source Z = 0 liegt die Spiegelquelle exakt auf der echten "
                          "Quelle, die Reflexion ist dann nur eine gedämpfte Verdopplung ohne eigene "
                          "Laufzeit - hörbar getrennt wird sie erst, wenn die Quelle über dem Boden "
                          "liegt. Kostet die doppelte Löserlast, deshalb standardmäßig aus."
                        : "Second propagation path per ear via the ground (mirror source at "
                          "plane z=0), with its own delay, doppler and damping. Note: at "
                          "Source Z = 0 the mirror source sits exactly on the real source, "
                          "the reflection is then just a damped doubling with no delay of "
                          "its own - it only becomes audibly separate once the source is "
                          "above ground. Costs double the solver load, therefore off by default.";
                case Key::NWaveGain:
                    return lang == Language::De
                        ? "Lautstärke des Überschallknalls in dB. Er darf übersteuern - "
                          "ein Knall aus der Nähe IST ohrenbetäubend, abgefangen wird das "
                          "vom Limiter, nicht von einer stillen Bremse in der Rechnung."
                        : "Loudness of the sonic boom in dB. It may clip - a boom heard from "
                          "close by IS deafening; that is caught by the limiter, not by a "
                          "silent brake inside the calculation.";
                case Key::ExtraPaths:
                    return lang == Language::De
                        ? "Pegel der zusätzlichen Hörwege - des Nachlaufs nach einem "
                          "Überschall-Vorbeiflug. Bei Überschall trifft Schall von mehreren "
                          "Emissionszeitpunkten gleichzeitig ein; der jüngste Weg trägt, was "
                          "die Quelle zuletzt gesendet hat, die übrigen tragen Älteres nach. "
                          "Ganz nach links ist der Nachlauf weg, der Rest bleibt."
                        : "Level of the additional listening paths - the trail after a "
                          "supersonic fly-by. In supersonic flight, sound from several emission "
                          "times arrives at once; the youngest path carries what the source "
                          "sent last, the others carry older material. Fully left removes the "
                          "trail and leaves the rest.";
                case Key::NWavePressure:
                    return lang == Language::De
                        ? "Stärke der Druckwelle: der langsamen Auslenkung der Nulllinie "
                          "zwischen den beiden Stoßfronten, auf der der übrige Sound reitet. "
                          "0 lässt nur den Doppelknall stehen, 1 ist die vollständige N-Welle, "
                          "darüber wird die Auslenkung betont."
                        : "Strength of the pressure wave: the slow displacement of the zero "
                          "line between the two shock fronts, which the rest of the sound rides "
                          "on. 0 leaves only the double bang, 1 is the complete N-wave, above "
                          "that the displacement is emphasised.";
                case Key::NWaveEdge:
                    return lang == Language::De
                        ? "Schärfe der beiden Stoßfronten: links Grollen, rechts Peitschenknall. "
                          "Mitte = physikalische Anstiegszeit, die mit der Entfernung von "
                          "selbst weicher wird. Wirkt auf Nah- und Fernanteil zugleich, damit "
                          "auch der Knall aus mehreren Kilometern eine Kante bekommen kann."
                        : "Sharpness of the two shock fronts: rumble on the left, whipcrack on "
                          "the right. Centre = the physical rise time, which softens with "
                          "distance by itself. Acts on the near and far part at once, so even "
                          "a boom from several kilometres away can be given an edge.";
                case Key::NWaveSize:
                    return lang == Language::De
                        ? "Größe/Masse des Körpers in Metern, 0 bis 200 - sie bestimmt die "
                          "Dauer der Druckwelle. Größer = tiefer und länger "
                          "(Verkehrsflugzeug), kleiner = kürzer und knackiger (Geschoss). Wirkt "
                          "nur bei eingeschalteter N-Welle."
                        : "Size/mass of the body in metres, 0 to 200 - determines the duration "
                          "of the pressure wave. Larger = deeper and longer (airliner), smaller "
                          "= shorter and sharper (projectile). Only affects the N-wave while "
                          "enabled.";
                case Key::NWave:
                    return lang == Language::De
                        ? "Echte N-Wellen-Druckwelle beim Überschallknall: steiler Anstieg, Nulldurchgang, "
                          "steiler Abfall. Ausgelöst pro Hörweg in dem Moment, in dem die Mach-Front ihn "
                          "überstreicht (M_r, die auf den Hörer bezogene Mach-Zahl der Quelle, "
                          "durchquert 1). Kommt ADDITIV oben auf den normalen Klang, die "
                          "bestehende Amplitudenformel bleibt unverändert. Nicht zu verwechseln mit 'Boom "
                          "Limit' (reine Amplitudendeckelung, keine Pulsform) und nicht mit dem Limiter am "
                          "Ausgang. Standardmäßig aus."
                        : "Real N-wave pressure pulse at the sonic boom: steep rise, zero "
                          "crossing, steep fall. Triggered per listening path at the moment "
                          "the mach front sweeps over it (M_r, the source's Mach number "
                          "relative to the listener, crosses 1). Comes ADDITIVELY "
                          "on top of the normal sound, the existing amplitude formula stays "
                          "unchanged. Not to be confused with 'Boom Limit' (pure amplitude "
                          "capping, no pulse shape) or with the output limiter. Off by default.";
                case Key::LimiterOn:
                    return lang == Language::De
                        ? "Sicherheitsbegrenzer am Ausgang (weiche Kniekennlinie, ein sanfter "
                          "statt abrupter Übergang in die Begrenzung) - fängt "
                          "Überschall-Spitzen ab, ohne sie hart zu clippen."
                        : "Safety limiter on the output (soft knee, a gradual instead of "
                          "abrupt transition into limiting) - catches supersonic peaks "
                          "without hard clipping.";
                case Key::LevelMeter:
                    return lang == Language::De
                        ? "Ausgangspegel (nach Gain+Limiter). Weißer Strich = -6dB. "
                          "Rote LED oben = Clipping ODER der Begrenzer greift, hält 500ms. "
                          "Der Begrenzer ist ein Sicherheits-Softclip auf dem Summen-Ausgang "
                          "hinter dem Output-Gain: er fängt Spitzen ab (Überschallknall/"
                          "N-Welle, viele Klone), damit nichts übersteuert - deshalb sitzt "
                          "seine Meldung auf derselben LED. Leuchtet sie dauernd, klingt ein "
                          "Schwarm nach einer einzigen Stimme, weil alles auf dieselbe "
                          "Obergrenze zusammengefahren wird. Schaltbar über 'Limiter'."
                        : "Output level (after gain+limiter). White line = -6dB. Red LED at "
                          "top = clipping OR the limiter kicking in, holds for 500ms. The "
                          "limiter is a safety soft clip on the summed output after the output "
                          "gain: it catches peaks (sonic boom/N-wave, many clones) so nothing "
                          "overshoots - which is why its indication shares that same LED. If "
                          "it stays lit, a swarm sounds like a single voice, because "
                          "everything gets squeezed onto the same ceiling. Toggled via "
                          "'Limiter'.";

                // --- WallPanel ---
                case Key::WallOn:
                    return lang == Language::De
                        ? "Zusätzlicher Ausbreitungsweg pro Ohr über eine unendlich große Ebene "
                          "(Spiegelquelle wie beim Boden), mit eigener Laufzeit, eigenem Doppler und "
                          "eigener Dämpfung. Kostet ein weiteres Pfadpaar Löserlast, deshalb "
                          "standardmäßig aus. Die Wand ist im Feld als Linie eingezeichnet."
                        : "Additional propagation path per ear via an infinitely large plane "
                          "(mirror source like the ground), with its own delay, doppler and "
                          "damping. Costs one more path pair of solver load, therefore off "
                          "by default. The wall is drawn in the field as a line.";
                case Key::WallX:
                    return lang == Language::De
                        ? "Fußpunkt der Wand, waagerecht - dieselbe normierte Feldkoordinate wie "
                          "Quelle und Hörer. Die Wand ist unendlich groß, der Punkt legt nur "
                          "fest, wo sie durchläuft."
                        : "Foot point of the wall, horizontal - the same normalized field "
                          "coordinate as source and listener. The wall is infinitely large, "
                          "the point only fixes where it runs.";
                case Key::WallY:
                    return lang == Language::De
                        ? "Fußpunkt der Wand, in die Tiefe. Siehe X."
                        : "Foot point of the wall, in depth. See X.";
                case Key::WallAngle:
                    return lang == Language::De
                        ? "Richtung der Wandlinie in der Draufsicht. 0 Grad = die Wand läuft quer "
                          "von links nach rechts, 90 Grad = von vorn nach hinten."
                        : "Direction of the wall line in top-down view. 0 degrees = the wall "
                          "runs crosswise from left to right, 90 degrees = from front to back.";
                case Key::WallTilt:
                    return lang == Language::De
                        ? "Neigung der Wand um genau ihre eigene Linie. 0 = senkrecht stehend, "
                          "+/-90 = flach liegend - dann ist sie eine zweite Bodenebene in der Höhe "
                          "ihres Fußpunkts (also auf z = 0, deckungsgleich mit dem Boden)."
                        : "Tilt of the wall around exactly its own line. 0 = standing "
                          "upright, +/-90 = lying flat - it then becomes a second ground "
                          "plane at the height of its foot point (i.e. at z = 0, "
                          "coinciding with the ground).";
                case Key::WallDamp:
                    return lang == Language::De
                        ? "Wie stark die Wand bei der Reflexion die Höhen schluckt. 0 = ideal harte "
                          "Fläche, 1 = weich/absorbierend. Wandflächen sind in der Regel härter "
                          "als Gras oder Erde, deshalb wirkt derselbe Reglerwert hier heller als "
                          "beim Boden."
                        : "How strongly the wall absorbs highs on reflection. 0 = ideal "
                          "hard surface, 1 = soft/absorbent. Wall surfaces are usually "
                          "harder than grass or earth, so the same control value sounds "
                          "brighter here than for the ground.";
                case Key::WallGain:
                    return lang == Language::De
                        ? "Pegel der Reflexion in dB, unabhängig von Damp. Damp ist ein Tiefpass "
                          "mit Gleichstromverstärkung 1 (nimmt nur Höhen, keinen Gesamtpegel) - "
                          "hört man die Wand trotzdem zu leise, ist das hier der Regler dafür."
                        : "Level of the reflection in dB, independent of Damp. Damp is a "
                          "low-pass with unity DC gain (only removes highs, not overall "
                          "level) - if the wall still sounds too quiet, this is the control for that.";
                case Key::TapSelect:
                    return lang == Language::De
                        ? "Welcher der acht Abgriffpunkte gerade eingestellt wird. Ein heller "
                          "Rahmen heisst: dieser Punkt laeuft."
                        : "Which of the eight pickup points is currently being edited. A bright "
                          "outline means this point is running.";
                case Key::TapOn:
                    return lang == Language::De
                        ? "Schaltet diesen Abgriffpunkt ein. Er kostet einen Loeser - halb so viel "
                          "wie eine Reflexionsflaeche, die immer fuer beide Ohren rechnet."
                        : "Switches this pickup point on. It costs one solver - half of what a "
                          "reflecting surface costs, which always computes for both ears.";
                case Key::TapX:
                    return lang == Language::De
                        ? "Ort des Abgriffpunkts quer, 0 = links, 1 = rechts. Was hier ankommt, "
                          "geht in den Hall."
                        : "Position of the pickup point across the field, 0 = left, 1 = right. "
                          "Whatever arrives here feeds the reverb.";
                case Key::TapY:
                    return lang == Language::De
                        ? "Ort des Abgriffpunkts in der Tiefe, 0 = vorn, 1 = hinten."
                        : "Position of the pickup point in depth, 0 = front, 1 = back.";
                case Key::TapZ:
                    return lang == Language::De
                        ? "Hoehe des Abgriffpunkts ueber dem Boden, in Metern."
                        : "Height of the pickup point above the ground, in metres.";
                case Key::TapType:
                    return lang == Language::De
                        ? "Bauart des Halls. Diffusor streut kurz ohne Nachhall, Schroeder ist der "
                          "klassische Kammfilter-Hall, FDN klingt dichter und kostet trotzdem "
                          "weniger. Draussen ist kein Raum, sondern die Antwort EINER Flaeche - "
                          "ohne Nachechos, weil die anderen Punkte die uebernehmen."
                        : "Reverb type. Diffuser scatters briefly without a tail, Schroeder is the "
                          "classic comb-filter reverb, FDN sounds denser and still costs less. "
                          "Outdoors is not a room but the answer of ONE surface - without later "
                          "echoes, because the other points take care of those.";
                case Key::TapRoom:
                    return lang == Language::De
                        ? "Kantenlaenge des gedachten Raums in Metern. Bestimmt Echodichte und "
                          "Faerbung. Bei der Bauart Draussen die Ausdehnung der Flaeche: ihre "
                          "Raender antworten spaeter als ihre Mitte."
                        : "Edge length of the imagined room in metres. Sets echo density and "
                          "colour. For the Outdoors type it is the extent of the surface: its "
                          "edges answer later than its centre.";
                case Key::TapDecay:
                    return lang == Language::De
                        ? "Zeit, bis der Nachhall auf ein Tausendstel gefallen ist. Bei Draussen "
                          "oeffnet der Regler den Weg der Flaeche zurueck auf sich selbst: bei "
                          "null eine freistehende Flanke, aufgedreht ein Talkessel mit Echos der "
                          "Echos."
                        : "Time until the tail has dropped to a thousandth. For Outdoors it opens "
                          "the surface's path back onto itself: at zero a free-standing slope, "
                          "turned up a bowl-shaped valley with echoes of echoes.";
                case Key::TapDamp:
                    return lang == Language::De
                        ? "Wie viel Hoehen der Hall je Umlauf verliert - ein Tiefpass im Umlauf, "
                          "sonst nichts. Das Verdrehen der Rueckwuerfe sitzt daneben im Regler "
                          "Phase."
                        : "How much treble the reverb loses per round trip - a low-pass in the "
                          "loop, nothing else. Phase rotation sits next to it in the Phase "
                          "control.";
                case Key::BoomHold:
                    return lang == Language::De
                        ? "Sperrzeit nach einem Knall: was so kurz danach noch einmal ausloesen "
                          "wuerde, faellt weg. Gegen das Stolpern, wenn die Quelle die "
                          "Schallmauer hin und gleich wieder zurueck durchquert."
                        : "Hold time after a bang: anything that would trigger again this soon is "
                          "dropped. Against the stumble when the source crosses the sound barrier "
                          "and back again.";
                case Key::ThrottleFromAccel:
                    return lang == Language::De
                        ? "Wie stark die Beschleunigung der Quelle die Drehzahl mitzieht. Anziehen "
                          "dreht hoch, Ausrollen faellt zurueck; bei einem g und voll aufgedrehtem "
                          "Regler ist es die doppelte Drehzahl."
                        : "How much the source's acceleration pulls the RPM along. Speeding up "
                          "revs up, coasting falls back; at one g with the control full up it is "
                          "double the RPM.";
                case Key::ThrottleTau:
                    return lang == Language::De
                        ? "Traegheit dieser Nachfuehrung. Kurz klingt elektrisch - die Drehzahl "
                          "folgt sofort. Lang klingt nach Verbrenner mit Schwungmasse. Zugleich das "
                          "Messfenster der Beschleunigung: kurz eingestellt kommt auch das Zittern "
                          "der Bewegung als Gas an."
                        : "Inertia of that response. Short sounds electric - the RPM follows at "
                          "once. Long sounds like a combustion engine with flywheel. It is also the "
                          "window the acceleration is measured over: set short, the jitter of the "
                          "motion arrives as throttle too.";
                case Key::WindLevel:
                    return lang == Language::De
                        ? "Pegel des Fahrtwinds - das Rauschen, das die Quelle allein vom "
                          "Fliegen hat. Es waechst mit dem Tempo, oberhalb von 120 m/s nur noch "
                          "mit der Wurzel."
                        : "Level of the slipstream - the noise a source makes just from moving. "
                          "It grows with speed, above 120 m/s only with the square root.";
                case Key::NoiseSpeed:
                    return lang == Language::De
                        ? "Wie stark das Rauschband dem Tempo folgt statt nur der Drehzahl. "
                          "Aufgedreht waechst es mit dem Quadrat der Geschwindigkeit; bei "
                          "120 m/s steht der eingestellte Pegel."
                        : "How much the noise band follows speed instead of just RPM. Turned up "
                          "it grows with the square of the speed; at 120 m/s the set level "
                          "applies.";
                case Key::TapPhase:
                    return lang == Language::De
                        ? "Wie stark die Rueckwuerfe der Flaeche gegeneinander verdreht werden. "
                          "Aufgedreht loeschen sie sich gegenseitig anders aus, der Klang wird "
                          "hohler und breiter. Nur bei der Bauart Draussen."
                        : "How strongly the surface's reflections are phase-rotated against each "
                          "other. Turned up they cancel each other differently, the sound gets "
                          "hollower and wider. Outdoors type only.";
                case Key::TapEarly:
                    return lang == Language::De
                        ? "Staerke der ersten Einzelechos, bevor der Hall diffus wird. Sie machen "
                          "einen Knall wuchtig: ein Nachhallnetz verteilt seine Energie auf "
                          "tausende winzige Echos, eine harte Flanke wirft EINES zurueck, das "
                          "fast so laut ist wie das Original. 0 = reiner Nachhall."
                        : "Strength of the first discrete echoes before the reverb turns diffuse. "
                          "They are what makes a bang forceful: a reverb network spreads its "
                          "energy over thousands of tiny echoes, a hard surface throws back ONE "
                          "that is almost as loud as the original. 0 = tail only.";
                case Key::TapCopy:
                    return lang == Language::De
                        ? "Merkt die Halleinstellungen dieses Punktes. Der ORT bleibt aussen vor - "
                          "kopiert wird der Hall, nicht die Stelle."
                        : "Remembers this point's reverb settings. The LOCATION stays out of it - "
                          "what gets copied is the reverb, not the place.";
                case Key::TapPaste:
                    return lang == Language::De
                        ? "Setzt die gemerkten Halleinstellungen auf den gewaehlten Punkt."
                        : "Applies the remembered reverb settings to the selected point.";
                case Key::ReverbBypass:
                    return lang == Language::De
                        ? "Legt alle acht Abgriffpunkte auf einmal still. Ein echter Bypass: die "
                          "Wege werden uebersprungen und kosten dann auch keine Rechenzeit."
                        : "Silences all eight pickup points at once. A real bypass: the paths are "
                          "skipped and cost no CPU time while off.";
                case Key::DirectGain:
                    return lang == Language::De
                        ? "Pegel des Direktschalls, also der Wege ohne Reflexion. Zugedreht bleiben "
                          "Boden, Waende und die Abgriffpunkte stehen - so hoert man, was der Raum "
                          "allein macht. Gilt fuers ganze Plugin, nicht je Punkt."
                        : "Level of the direct sound, meaning the paths without reflection. Turned "
                          "down, ground, walls and pickup points remain - that way you hear what "
                          "the room alone does. Applies to the whole plugin, not per point.";
                case Key::TapEchoes:
                    return lang == Language::De
                        ? "Wie viele Rueckwuerfe die Flaeche liefert. Wenige klingen als einzelne "
                          "Anschlaege, viele als Flaeche. Nur bei der Bauart Draussen."
                        : "How many reflections the surface delivers. Few sound like separate "
                          "hits, many like a surface. Outdoors type only.";
                case Key::TapSeed:
                    return lang == Language::De
                        ? "Wuerfelbecher fuer die Verteilung der Rueckwuerfe. Dieselbe Zahl gibt "
                          "immer dieselbe Flaeche. Nur bei der Bauart Draussen."
                        : "Dice cup for how the reflections are spread. The same number always "
                          "gives the same surface. Outdoors type only.";
                case Key::TapGain:
                    return lang == Language::De
                        ? "Pegel dieses Abgriffpunkts im Ausgang, in dB."
                        : "Level of this pickup point in the output, in dB.";
                case Key::TapWidth:
                    return lang == Language::De
                        ? "Stereobreite des Halls. 0 = ein Punkt in der Mitte, ueber 1 wird die "
                          "Seite ueberhoeht."
                        : "Stereo width of the reverb. 0 = a point in the centre, above 1 the "
                          "sides are exaggerated.";
                case Key::TapPredelay:
                    return lang == Language::De
                        ? "Bildet den Weg vom Abgriffpunkt zurueck zum Hoerer als Laufzeit ab. Aus "
                          "klingt der Hall, als saesse er am Ohr."
                        : "Renders the way back from the pickup point to the listener as travel "
                          "time. Off, the reverb sounds as if it sat at the ear.";
                case Key::SecondOrder:
                    return lang == Language::De
                        ? "Genau EINE zusätzliche Reflexionsgeneration: Wege der Form Quelle -> Fläche X "
                          "-> Fläche Y -> Ohr, mit X ungleich Y. Braucht mindestens zwei eingeschaltete "
                          "Flächen, sonst gibt es solche Wege gar nicht. Zwei parallele Wände ergeben so "
                          "das typische Flatterecho. Kostet bis zu sechs weitere Pfadpaare - der CPU-Wert "
                          "in der Statuszeile zeigt, was man sich einkauft."
                        : "Exactly ONE additional reflection generation: paths of the form "
                          "source -> surface X -> surface Y -> ear, with X not equal Y. "
                          "Needs at least two enabled surfaces, otherwise such paths do not "
                          "exist at all. Two parallel walls produce the typical flutter "
                          "echo this way. Costs up to six more path pairs - the CPU value "
                          "in the status line shows what this buys.";
                case Key::BounceGain:
                    return lang == Language::De
                        ? "Pegelfaktor je zusätzlicher Reflexionsgeneration, immer unter 1. Die "
                          "Flächendämpfung allein reicht dafür nicht: die ist ein Tiefpass mit "
                          "Gleichstromverstärkung 1 und nimmt nur Höhen, keinen Pegel. Kleinere "
                          "Werte = die zweite Reflexion tritt weiter zurück."
                        : "Level factor per additional reflection generation, always below "
                          "1. Surface damping alone is not enough for this: it is a "
                          "low-pass with unity DC gain and only removes highs, not level. "
                          "Smaller values = the second reflection sits further back.";
                case Key::BounceGainBoost:
                    return lang == Language::De
                        ? "Zusätzlicher, unabhängiger Pegel-Boost (dB) obendrauf - anders als Bounce "
                          "Gain darf dieser Regler auch über 0dB hinaus verstärken, damit die "
                          "zweifache Reflexion trotz Tiefpass hörbar bleibt."
                        : "Additional, independent level boost (dB) on top - unlike Bounce "
                          "Gain, this control may also boost above 0dB, so the double "
                          "reflection stays audible despite the low-pass.";

                // --- SamplePanel ---
                case Key::SampleGain:
                    return lang == Language::De
                        ? "Lautstärke des geladenen Samples (dB)."
                        : "Level of the loaded sample (dB).";
                case Key::SamplePitch:
                    return lang == Language::De
                        ? "Tonhöhenverschiebung des Samples in Halbtönen (Resampling)."
                        : "Pitch shift of the sample in semitones (resampling).";
                case Key::LoopStart:
                    return lang == Language::De
                        ? "Loop-Anfang, normiert 0-1 der geladenen Datei."
                        : "Loop start, normalized 0-1 of the loaded file.";
                case Key::LoopEnd:
                    return lang == Language::De
                        ? "Loop-Ende, normiert 0-1 der geladenen Datei."
                        : "Loop end, normalized 0-1 of the loaded file.";
                case Key::LoopXfade:
                    return lang == Language::De
                        ? "Ueberblendzeit an der Loop-Naht (ms) - verhindert einen hörbaren Klick "
                          "beim Sprung von Loop-Ende zurück zu Loop-Anfang."
                        : "Crossfade time at the loop seam (ms) - prevents an audible click "
                          "when jumping from loop end back to loop start.";
                case Key::EqLow:
                    return lang == Language::De
                        ? "Bass-Anhebung/Absenkung (Low-Shelf, feste Eckfrequenz 200 Hz)."
                        : "Bass boost/cut (low shelf, fixed corner frequency 200 Hz).";
                case Key::EqMid:
                    return lang == Language::De
                        ? "Anhebung/Absenkung im Mittenband (Glockenfilter)."
                        : "Boost/cut in the mid band (bell filter).";
                case Key::EqMidFreq:
                    return lang == Language::De
                        ? "Mittenfrequenz des Glockenfilters (Hz)."
                        : "Centre frequency of the bell filter (Hz).";
                case Key::EqHigh:
                    return lang == Language::De
                        ? "Höhen-Anhebung/Absenkung (High-Shelf, feste Eckfrequenz 8 kHz)."
                        : "Treble boost/cut (high shelf, fixed corner frequency 8 kHz).";
                case Key::LoadSample:
                    return lang == Language::De
                        ? "Audiodatei laden (WAV/AIFF/FLAC/MP3) - wird als endlos "
                          "loopende Klangquelle verwendet, ersetzt bei Bedarf per "
                          "Quelle-Knopf oben den Motor-Generator."
                        : "Load an audio file (WAV/AIFF/FLAC/MP3) - used as an endlessly "
                          "looping sound source, replaces the engine generator via the "
                          "source button above when needed.";

                // --- ScopeComponent ---
                case Key::Scope:
                    return lang == Language::De
                        ? "Oszilloskop des Ausgangs (nach Gain/Limiter). Mausrad senkrecht zoomt, Pinch "
                          "zoomt ebenfalls. Freeze hält das Bild an UND schaltet auf die komplette "
                          "Historie um - darin waagerecht scrollen oder ziehen, um frei zu suchen. "
                          "Speichern legt den sichtbaren Ausschnitt als CSV in Downloads ab."
                        : "Oscilloscope of the output (after gain/limiter). Vertical mouse "
                          "wheel zooms, pinch zooms too. Freeze halts the image AND "
                          "switches to the complete history - scroll or drag horizontally "
                          "in it to search freely. Save stores the visible section as CSV "
                          "in Downloads.";

                // --- MotionPanel ---
                case Key::SmootherTau:
                    return lang == Language::De
                        ? "Zeitkonstante der Bewegungsglättung: so lange braucht die geglättete "
                          "Position, um einer Zieländerung zu folgen. Kleiner = direkter/schneller "
                          "(schon normale Mausbewegungen über wenige Meter können dann hohe "
                          "Geschwindigkeiten und starken Doppler erzeugen), größer = träger."
                        : "Time constant of the movement smoothing: how long the smoothed "
                          "position takes to follow a target change. Smaller = more "
                          "direct/faster (even normal mouse movements over a few metres "
                          "can then produce high speeds and strong doppler), larger = "
                          "more sluggish.";
                case Key::SlewVmax:
                    return lang == Language::De
                        ? "Höchstgeschwindigkeit des Slew Limiters in m/s. Aus dem Stand "
                          "braucht er eine Sekunde bis dorthin - eine eigene "
                          "Beschleunigungsgrenze gibt es deshalb nicht.\n\n"
                          "Wirkt auch dann, wenn ein anderes Glättungsverfahren gewählt ist: "
                          "während der Clip-Wiedergabe mit Catmull-Rom bremst er Ausreißer an "
                          "scharfen Bahn-Umkehrpunkten ab."
                        : "Top speed of the slew limiter in m/s. From standstill it takes one "
                          "second to get there - which is why there is no separate "
                          "acceleration limit.\n\n"
                          "Also active when another smoothing method is selected: during "
                          "Catmull-Rom clip playback it brakes outliers at sharp path "
                          "reversal points.";
                case Key::PlaySpeed:
                    return lang == Language::De
                        ? "Wiedergabegeschwindigkeit einer Aufnahme (0.25-4x). Skaliert die Bewegung "
                          "und damit den Doppler - schnelle Wiedergabe kann Überschall erzeugen."
                        : "Playback speed of a recording (0.25-4x). Scales the movement "
                          "and thus the doppler - fast playback can produce supersonic speed.";
                case Key::GlobalMaxSpeed:
                    return lang == Language::De
                        ? "Gemeinsamer Tempo-Deckel für ALLE Bewegung - Maus/Automation-Glättung "
                          "UND Vorbeiflug zusammen, unabhängig vom gewählten Smoother. Anders als "
                          "'Slew Vmax' (nur bei Slew Limiter, begrenzt dessen eigene Dynamik) wirkt "
                          "das hier immer, als letzte Sicherung. Default sehr hoch = wirkungslos, "
                          "bis bewusst heruntergestellt."
                        : "Shared speed cap for ALL movement - mouse/automation smoothing "
                          "AND fly-by together, independent of the chosen smoother. "
                          "Unlike 'Slew Vmax' (only with Slew Limiter, limits its own "
                          "dynamics) this always applies, as a last safeguard. Default "
                          "very high = has no effect until deliberately lowered.";
                case Key::SmootherType:
                    return lang == Language::De
                        ? "Glättungsverfahren für die Quell-/Hörerbewegung - bestimmt, wie aus "
                          "ruckartigen Mausbewegungen eine 'bewegte Maschine' statt einer 'digitalen "
                          "Maus' wird."
                        : "Smoothing method for source/listener movement - determines how "
                          "jerky mouse movements become a 'moving machine' instead of a "
                          "'digital mouse'.";
                case Key::PlayInterp:
                    return lang == Language::De
                        ? "Interpolation der Wiedergabe zwischen aufgezeichneten Punkten: Linear "
                          "(einfach) oder Catmull-Rom (weich, ohne Tonhöhensprung an den Stützstellen)."
                        : "Interpolation of playback between recorded points: Linear "
                          "(simple) or Catmull-Rom (smooth, no pitch jump at the control points).";
                case Key::PlayLoop:
                    return lang == Language::De
                        ? "Wiedergabe am Ende des Clips von vorn beginnen statt zu stoppen."
                        : "Restart playback from the beginning at the end of the clip "
                          "instead of stopping.";
                case Key::LiveTab:
                    return lang == Language::De
                        ? "Einstellungen für den Livezugriff: wie die Quelle einer Bewegung "
                          "von Maus oder Automation folgt und wie sie nach dem Loslassen "
                          "ausläuft. Das Glättungsverfahren formt auch eine linear "
                          "abgespielte Aufzeichnung."
                        : "Settings for live control: how the source follows a movement from "
                          "mouse or automation, and how it coasts to a halt after release. The "
                          "smoothing method also shapes a linearly played back recording.";
                case Key::Coast:
                    return lang == Language::De
                        ? "Nach dem Loslassen von Quelle/Hörer im Feld noch kurz mit Schwung "
                          "weiterlaufen und abbremsen, statt abrupt zu stoppen."
                        : "Keep coasting briefly with momentum and decelerate after "
                          "releasing source/listener in the field, instead of stopping abruptly.";
                case Key::MouseFrame:
                    return lang == Language::De
                        ? "Die Maus wird auf einem festen Bildtakt abgefragt statt bei "
                          "jedem Ereignis. Mausereignisse kommen unregelmäßig, und dieser "
                          "Takt steckt sonst in der Bewegung - und damit im Doppler, dessen "
                          "Tonhöhe an der Geschwindigkeit hängt, nicht an der Position."
                        : "The mouse is polled on a fixed frame rate instead of on every "
                          "event. Mouse events arrive irregularly, and this timing would "
                          "otherwise end up in the movement - and thus in the doppler, "
                          "whose pitch depends on speed, not on position.";
                case Key::Record:
                    return lang == Language::De
                        ? "Aufnahme der (geglätteten) Quellbewegung starten/stoppen."
                        : "Start/stop recording the (smoothed) source movement.";
                case Key::Play:
                    return lang == Language::De
                        ? "Aufgezeichnete Bewegung abspielen bzw. stoppen."
                        : "Play back or stop the recorded movement.";
                case Key::FlyKind:
                    return lang == Language::De
                        ? "Bahnart des Generators. 'Durch den Bildschirm' fliegt in die "
                          "Tiefe an einem seitlich versetzten Hörer vorbei, 'Waagerecht "
                          "querend' von links nach rechts in n Metern Abstand."
                        : "Path type of the generator. 'Through the screen' flies into "
                          "the depth past a laterally offset listener, 'Horizontal "
                          "crossing' from left to right at n metres distance.";
                case Key::FlyStart:
                    return lang == Language::De
                        ? "'Kontinuierlich' belegt die Vorgeschichte mit genau derselben Geraden vor - "
                          "der Löser sieht eine Quelle, die schon immer geflogen ist, es gibt keinen "
                          "Sprung. 'Knall-Start' lässt die Quelle schlagartig in voller Fahrt erscheinen: "
                          "bewusst unphysikalisch, dafür ein reproduzierbarer Testfall für den "
                          "Überschallknall."
                        : "'Continuous' pre-fills the history with exactly the same "
                          "straight line - the solver sees a source that has always been "
                          "flying, there is no jump. 'Knall-Start' makes the source "
                          "appear suddenly at full speed: deliberately unphysical, but a "
                          "reproducible test case for the sonic boom.";
                case Key::FlyDistance:
                    return lang == Language::De
                        ? "Abstand, in dem die Bahn am Hörer vorbeiläuft - senkrecht zur "
                          "Flugrichtung. Kleiner Abstand = kräftigerer Doppler-Umschlag beim "
                          "Vorbeiflug. Aendert NICHT die Bahnlänge, dafür 'Fly Approach'."
                        : "Distance at which the path passes the listener - perpendicular "
                          "to the flight direction. Smaller distance = stronger doppler "
                          "swing during the fly-by. Does NOT change the path length, use "
                          "'Fly Approach' for that.";
                case Key::FlyApproach:
                    return lang == Language::De
                        ? "Anflug-/Abflugstrecke: wie weit vor (und nach) dem nächsten Punkt die "
                          "Bahn beginnt bzw. endet. Unabhängig von 'Fly Dist' (das ist nur der "
                          "seitliche Abstand) - länger heißt mehr hörbare Annäherung vor dem "
                          "eigentlichen Vorbeiflug, besonders bei hoher Fluggeschwindigkeit sinnvoll."
                        : "Approach/departure stretch: how far before (and after) the "
                          "closest point the path begins/ends. Independent of 'Fly Dist' "
                          "(which is only the lateral distance) - longer means more "
                          "audible approach before the actual fly-by, especially useful "
                          "at high flight speed.";
                case Key::FlySpeed:
                    return lang == Language::De
                        ? "Fluggeschwindigkeit in m/s, live veränderbar und automatisierbar - die "
                          "Bahn integriert den jeweils aktuellen Wert, ein Automationsverlauf "
                          "beschleunigt die Quelle also wirklich. Ueber 343 m/s wird der Flug "
                          "überschallschnell."
                        : "Flight speed in m/s, changeable and automatable live - the "
                          "path integrates the current value at each moment, an "
                          "automation curve really does accelerate the source. Above "
                          "343 m/s the flight becomes supersonic.";
                case Key::FlyLoop:
                    return lang == Language::De
                        ? "Vorbeiflug in Dauerschleife: am Ende der Strecke fängt derselbe Flug "
                          "von vorn an, statt am Endpunkt stehen zu bleiben. Der Rücksprung an "
                          "den Anfang läuft über denselben Weg wie ein frisch ausgelöster "
                          "Flug - die Bahn bekommt eine passende Vorgeschichte und wird "
                          "überblendet, statt als Positionssprung zu knacken. Mit Startvariante "
                          "'Knall-Start' beginnt allerdings JEDER Durchgang schlagartig, das ist "
                          "dort genau der Zweck. Der Stopp-Knopf beendet die Schleife sofort."
                        : "Fly-by on repeat: at the end of the path the same flight starts over "
                          "instead of stopping at the end point. The jump back to the start "
                          "takes the same route as a freshly triggered flight - the trajectory "
                          "gets a matching prehistory and is crossfaded instead of clicking as a "
                          "position jump. With start variant 'Knall-Start' (abrupt) EVERY pass begins "
                          "instantly, which is exactly the point there. The stop button ends the "
                          "loop right away.";
                case Key::Fly:
                    return lang == Language::De
                        ? "Vorbeiflug starten bzw. laufenden Flug abbrechen. Die Bahnart und "
                          "die Startvariante gelten ab dem nächsten Start, das Tempo wirkt "
                          "sofort."
                        : "Start the fly-by or abort a running flight. The path type and "
                          "start variant take effect from the next start, the speed takes "
                          "effect immediately.";

                // --- SwarmPanel ---
                case Key::CloneTotal:
                    return lang == Language::De
                        ? "Gesamtzahl der Klone. Ein Klon ist eine zweite Quelle mit voller "
                          "Löserphysik (eigene Laufzeit, eigener Doppler, eigener Überschall), deren "
                          "Route um einen kleinen Betrag von der echten abweicht - zusammen ergibt das "
                          "ein Schrotmuster statt eines Einzelobjekts. Jeder Klon kostet genau ein "
                          "Pfadpaar, die Löserlast wächst also linear mit dieser Zahl, siehe "
                          "CPU-Balken unten am Fensterrand. 0 = aus, kostet dann auch nichts."
                        : "Total number of clones. A clone is a second source with full solver "
                          "physics (its own delay, own doppler, own sonic boom) whose path "
                          "deviates from the real one by a small amount - together they "
                          "produce a shotgun pattern instead of a single object. Each clone "
                          "costs exactly one path pair, so solver load grows linearly with "
                          "this number, see the CPU bar at the bottom of the window. 0 = off, costs nothing then "
                          "either.";
                case Key::CloneSpread:
                    return lang == Language::De
                        ? "Wie weit die Klon-Routen von der echten abweichen, in Metern."
                        : "How far the clone paths deviate from the real one, in metres.";
                case Key::CloneRealLevel:
                    return lang == Language::De
                        ? "Gain der Klone in dB. Jeder ist eine vollwertige Quelle mit eigener "
                          "Laufzeit - ohne Absenkung summieren sich acht Stück bis an den Limiter, "
                          "und dann klingt der Schwarm nicht breiter, sondern zusammengefahren. "
                          "0dB = unverändert. Faustregel: je mehr Klone, desto weiter herunter."
                        : "Gain of the clones in dB. Each is a full source with its own delay - "
                          "without attenuation eight of them add up to the limiter, and then the "
                          "swarm does not sound wider but squashed. 0dB = unchanged. Rule of "
                          "thumb: the more clones, the further down.";
                case Key::CloneShow:
                    return lang == Language::De
                        ? "Zeigt im Feld, wo die echten Klone sitzen: kleine, blasse Punkte um "
                          "die Quelle herum. Daran ist zu sehen, wie weit sie streuen und dass "
                          "jeder für sich wackelt. Reine Anzeige, kostet keine Rechenzeit im Ton."
                        : "Shows where the real clones sit in the field: small, faint dots around "
                          "the source. This makes their spread visible, and that each one wobbles "
                          "on its own. Display only, it costs no audio processing time.";

                // --- PluginEditor ---
                case Key::SourceButton:
                    return lang == Language::De
                        ? "Klangquelle umschalten: Motor-Generator, geladenes Sample oder "
                          "Audio-Eingang (live, sofern der Host/das Format einen bereitstellt)."
                        : "Switch sound source: engine generator, loaded sample, or audio "
                          "input (live, provided the host/format offers one).";
                case Key::FieldDrag:
                    return lang == Language::De
                        ? "Ziehen an M - dem Marker der Schallquelle - verschiebt sie. Ziehen am Kopf verschiebt "
                          "den Hörer, Ziehen an der Nase dreht ihn."
                        : "Drag M - the sound source marker - to move it. Drag the head to move the "
                          "listener, drag the nose to turn it.";
                case Key::ViewToggle:
                    return lang == Language::De
                        ? "Zwischen Draufsicht und perspektivischem Blick in die Tiefe "
                          "umschalten. Die perspektivische Ansicht zeigt die Höhe z, die in "
                          "der Draufsicht gar nicht vorkommt - und in ihr lässt sich die "
                          "Quellhöhe auch mit der Maus ziehen (waagerecht = Seite, "
                          "senkrecht = Höhe, die Tiefe bleibt)."
                        : "Switch between top-down view and perspective view into the "
                          "depth. The perspective view shows height z, which does not "
                          "appear at all in the top-down view - and in it the source "
                          "height can also be dragged with the mouse (horizontal = side, "
                          "vertical = height, depth stays fixed).";
                case Key::SpeedUnitToggle:
                    return lang == Language::De
                        ? "Tempo-Einheit umschalten (km/h, m/s, Mach). Gilt für die "
                          "Anzeige im Feld, die Statuszeile UND die Werte an den "
                          "Tempo-Reglern Fly Speed, Max Speed und Slew Vmax."
                        : "Switch the speed unit (km/h, m/s, Mach). Applies to the "
                          "display in the field, the status line AND the values on the "
                          "speed controls Fly Speed, Max Speed and Slew Vmax.";
                case Key::EngineReset:
                    return lang == Language::De
                        ? "Audiomotor neu anlassen: kompletter prepareToPlay()-"
                          "Durchlauf wie bei einem Wechsel der Audio-Puffergröße "
                          "(Klangquelle, Ausbreitungswege und beide Positions-"
                          "glätter neu aufgesetzt), falls nach einer CPU-Spitze "
                          "kein Ton mehr kommt. Hält processBlock() kurz an, "
                          "kein Datenrennen mit dem Audiothread."
                        : "Restart the audio engine: complete prepareToPlay() pass as "
                          "with an audio buffer size change (sound source, propagation "
                          "paths and both position smoothers rebuilt), in case no sound "
                          "comes through after a CPU spike. Briefly pauses "
                          "processBlock(), no data race with the audio thread.";
                case Key::TooltipsToggle:
                    return lang == Language::De
                        ? "Hilfehinweise beim Ueberfahren der Regler ein-/ausblenden."
                        : "Show/hide help hints when hovering over the controls.";
                case Key::ScopeToggle:
                    return lang == Language::De
                        ? "Oszilloskop ein-/ausblenden."
                        : "Show/hide the oscilloscope.";
                case Key::ScopeFreeze:
                    return lang == Language::De
                        ? "Scope-Bild anhalten und auf die komplette bisherige Historie umschalten "
                          "(@dpa-Feedback: \"frei herumsuchen\") - darin waagerecht scrollen/ziehen "
                          "zum Verschieben, senkrecht/Pinch weiter zum Zoomen. Der Ringpuffer läuft "
                          "im Hintergrund weiter, erst ein erneuter Klick holt wieder Live-Daten."
                        : "Halt the scope image and switch to the complete history so far "
                          "(@dpa-Feedback: \"frei herumsuchen\") - scroll/drag "
                          "horizontally in it to move, vertically/pinch to keep zooming. "
                          "The ring buffer keeps running in the background, only another "
                          "click fetches live data again.";
                case Key::ScopeSync:
                    return lang == Language::De
                        ? "Sync: richtet einen steigenden Nulldurchgang von L in der Mitte "
                          "des Scopes aus - der Trigger-Moment steht dann immer zentriert."
                        : "Sync: aligns a rising zero crossing of L in the middle of the "
                          "scope - the trigger moment then always stays centred.";
                case Key::ScopeEventTrigger:
                    return lang == Language::De
                        ? "Knall-Trigger: löst nicht an einem Nulldurchgang aus, sondern "
                          "am Pegelanstieg - ein schneller Hüllkurvenfolger gegen einen "
                          "langsamen. Ein Knall hat keine Periode, an der man ausrichten "
                          "könnte, sondern einen Einsatz. Der landet zentriert im Bild, "
                          "danach steht es für die eingestellte Haltezeit und schaltet "
                          "sich von selbst wieder scharf. Freeze hat Vorrang."
                        : "Bang trigger: fires on a level rise instead of a zero crossing "
                          "- a fast envelope follower against a slow one. A bang has no "
                          "period to align to, only an onset. It lands centred in the "
                          "picture, which then holds for the set hold time and re-arms "
                          "itself. Freeze takes priority.";
                case Key::ScopeHold:
                    return lang == Language::De
                        ? "Haltezeit des Knall-Triggers: so lange bleibt das Bild nach "
                          "einem Einsatz stehen, bevor es wieder scharf wird. Zwischen "
                          "zwei Einsätzen bleibt das letzte Bild ebenfalls stehen - "
                          "unten rechts steht, ob gerade gehalten oder gewartet wird."
                        : "Hold time of the bang trigger: how long the picture stays "
                          "after an onset before it re-arms. Between two onsets the last "
                          "picture also stays - the corner shows whether it is holding "
                          "or waiting.";
                case Key::ScopeZoomIn:
                    return lang == Language::De
                        ? "Reinzoomen (kürzere Zeitbasis). Wirkt wie Mausrad hoch "
                          "oder Pinch-Auseinanderziehen direkt auf dem Scope."
                        : "Zoom in (shorter time base). Works like scrolling the mouse "
                          "wheel up or pinching apart directly on the scope.";
                case Key::ScopeZoomOutPrefix:
                    return lang == Language::De
                        ? "Rauszoomen (längere Zeitbasis, bis zu "
                        : "Zoom out (longer time base, up to ";
                case Key::ScopeZoomOutSuffix:
                    return lang == Language::De
                        ? "s). Wirkt wie Mausrad runter oder Pinch-Zusammenziehen "
                          "direkt auf dem Scope."
                        : "s). Works like scrolling the mouse wheel down or pinching "
                          "together directly on the scope.";
                case Key::ScopeSave:
                    return lang == Language::De
                        ? "Sichtbaren Scope-Ausschnitt als WAV (32bit float, stereo) in "
                          "~/Downloads ablegen (@dpa-Feedback: \"für Dich, debuggen\") - "
                          "Zeitstempel im Dateinamen, abspielbar mit der richtigen "
                          "Samplerate/Tonhöhe."
                        : "Save the visible scope section as WAV (32bit float, stereo) "
                          "to ~/Downloads (@dpa-Feedback: \"für Dich, debuggen\") - "
                          "timestamp in the file name, playable with the correct "
                          "sample rate/pitch.";
                case Key::ScopePlay:
                    return lang == Language::De
                        ? "Spielt das sichtbare Scope-Bild einmal von vorn bis hinten ab, "
                          "danach still. Bleibt der Schalter an, startet ein Klick im Scope "
                          "die Wiedergabe ab der geklickten Stelle bis zum rechten Rand. "
                          "Solange er an ist, ersetzt die Wiedergabe den Ausgang."
                        : "Plays the visible scope picture once from start to end, then "
                          "goes quiet. While the switch stays on, clicking in the scope "
                          "starts playback from that point to the right edge. While it's "
                          "on, playback replaces the plugin output.";
                case Key::LanguageToggle:
                    return lang == Language::De
                        ? "Sprache der Hilfehinweise umschalten (Deutsch/Englisch)."
                        : "Toggle the language of the help hints (German/English).";
            }

            return "";
        }
    }

    // Aktuelle Sprache der Hilfehinweise.
    inline Language currentLanguage() { return detail::currentLanguageRef(); }

    inline void setLanguage (Language lang) { detail::currentLanguageRef() = lang; }

    inline void toggleLanguage()
    {
        setLanguage (currentLanguage() == Language::De ? Language::En : Language::De);
    }

    // Liefert den Hilfetext zu `key` in der aktuell gewaehlten Sprache. Baut
    // erst hier einen juce::String - der Aufrufer setzt das Ergebnis direkt
    // per Component::setTooltip().
    inline juce::String text (Key key)
    {
        // Ueber Text::utf8(), nicht ueber juce::String(const char*): die Texte
        // hier stehen als UTF-8 im Quelltext und enthalten Umlaute. Siehe
        // Util/Utf8.h - der direkte Weg macht aus "hört" ein "hÃ¶rt".
        return Text::utf8 (detail::rawText (key, currentLanguage()));
    }
}
