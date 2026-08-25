#pragma once

#include "SoundSource.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

// Motorgenerator (Plan 3.10): vier PolyBLEP-Sägezahn-Teiltöne mit RPM-Tracking,
// ein RPM-abhängiges Rauschband und langsamer Jitter auf der Grundfrequenz.
//
// Seit @dpa 20260824 ("jeder Betriebsmode ist ein eigener Generator!") ist das
// nur noch EINE der Betriebsarten, nämlich "Frei". Die anderen vier sind keine
// Gewichtung derselben Bausteine, sondern eigene Klangerzeuger mit eigenem
// Aufbau und eigenen Pegeln:
//
//   Düsenantrieb   - EIN Verdichterton, darüber ein lautes Strahlrauschen.
//   Raketenantrieb - kein Ton, nur Brüllen, dazu Druckstöße aus der Düse:
//                    echte N-Wellen, keine Rauschstöße.
//   Hubschrauber   - Verbrennermotor (die vier Teiltöne) UND ein Rotor, der
//                    aus der Nähe schwirrt und dessen Blätter knallen.
//   Propeller      - ein leiser Ton plus Blattschlag.
//
// Was allen gemeinsam ist: das Fahrtwindrauschen. Je schneller die Quelle
// fliegt, desto lauter (@dpa: "Die Luftwiderstandsgeräusche haben alle,
// vielleicht unterschiedlich, aber je schneller um so lauter"). Die
// Geschwindigkeit kommt von außen, siehe setAirspeed().
//
// Beim Wechsel der Betriebsart wird kurz aus- und wieder eingeblendet (siehe
// kindFade). Zwei Betriebsarten gleichzeitig zu rechnen und ineinander zu
// blenden wäre der aufwendigere Weg für einen Vorgang, den niemand im Takt
// bedient - eine Betriebsart wählt man einmal.
//
// Liest nichts selbst aus einer APVTS - das verdrahtet erst H13 im Processor
// per Listener. Stattdessen setzt der Message-Thread hier über einfache
// Setter je einen atomaren Wert pro Parameter; der Audio-Thread liest sie
// lock-frei. Die Werte hängen nicht zusammen (kein gemeinsam-atomarer
// Snapshot nötig), darum reichen einzelne std::atomic<float>.
// Eine Klangvorlage für die Dreiband-Formung (siehe EngineGenerator::BandVoicing):
// wo die drei Bänder sitzen und wie laut sie sind. Frequenzen in Hz, Pegel linear.
//
// Steht ausserhalb der Klasse, weil die Vorlagentabellen (jetVoiceTable und
// rocketVoiceTable in der .cpp) Dateikonstanten sind und an einen privaten
// Typ innerhalb der Klasse nicht herankaemen.
struct EngineVoicePreset
{
    double lowFc, midFc, midQ, highFc;
    double lowGain, midGain, highGain;
    double narrowFc, narrowQ, narrowGain;   // singender Ton im Rauschen, 0 = keiner
};

class EngineGenerator : public SoundSource
{
public:
    EngineGenerator();

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void renderMono (float* out, int numSamples) override;
    double dominantFrequencyHz() const override;

    // --- Setter: Message-Thread schreibt, Audio-Thread liest (Plan 3.10) ---

    void setRpm (float rpmValue);

    // index 0..3 für die vier Teiltöne. ratio = r_i, detuneCents = d_i (Cent),
    // trackAmount = t_i in [0,1], levelDb = Pegel des Teiltons in dB.
    void setHarmonic (int index, float ratio, float detuneCents, float trackAmount, float levelDb);

    void setNoiseParams (float fcLoHz, float fcHiHz, float gainLoDb, float gainHiDb, float q);
    void setJitter (float amountPercent, float rateHz);
    void setImbalance (float amount);

    // Wellenform JE Teilton (@dpa 20260824: "der sinus soll (zumindest bei
    // Hubschrauber und Propeller) für jeden osc setzbar sein"). false =
    // PolyBLEP-Sägezahn, true = reiner Sinus. index 0..3.
    void setSineMode (int index, bool shouldUseSine);

    // Die vier Teiltoene gemeinsam stummschalten (@dpa 20260825: "neben den
    // Levels einen OSC On/Off switch (schaltet die 4 Oscillatoren aus, zum
    // hoeren was noch alles laeuft"). Ein Mute, KEIN Pegel: die vier
    // Level-Regler behalten ihre Werte, sie sind beim Wiedereinschalten
    // unveraendert da.
    //
    // Wirkt auf "Frei" und auf den Verbrennermotor des Hubschraubers - genau
    // dort werden die Teiltoene gerechnet. Rauschband, Rotor, Fahrtwind und
    // Unwucht laufen weiter, und das ist der ganze Zweck: hoeren, was ohne
    // die Oszillatoren uebrig bleibt.
    void setHarmonicsOn (bool shouldSound);
    // Oktavlage der Unwucht, siehe Params::imbalanceOctave.
    void setImbalanceOctave (float octaves);

    // Betriebsart des Motors (@dpa 20260824: "in 'Motor' mehrere umschaltbar
    // machen"). kindIndex folgt exakt der Reihenfolge aus Params::engineKind
    // (createParameterLayout()): 0=Frei, 1=Duesenantrieb, 2=Raketenantrieb,
    // 3=Hubschrauber, 4=Propeller - siehe kindWeightTable in der .cpp.
    // Ueberschreibt keinen der uebrigen Setter hier, gewichtet nur, wie stark
    // ihre Ergebnisse beitragen (kein Regler wird dem Nutzer weggenommen).
    void setEngineKind (int kindIndex);

    // Nur in Betriebsart "Hubschrauber" hoerbar: Rotordrehzahl (Hz) und
    // Blattzahl, multipliziert ergeben sie die Blattschlag-Frequenz.
    void setHeliRotor (float rotorHz, float bladeCount);

    // Gesamtpegel der Betriebsart in dB. Gilt NICHT für "Frei" - dort machen
    // die vier Teilton-Pegel den Pegel, und daran darf sich nichts ändern,
    // sonst klängen alte Snapshots anders.
    //
    // Die anderen Betriebsarten brauchen ihn, weil ihre Pegel aus der Sache
    // kommen und nicht aus vier Reglern: ein Hubschrauber in drei Metern
    // Abstand ist ohrenbetäubend, und genau das war er vorher nicht (@dpa:
    // "die Lautstärken sind noch irgendwie völlig unrealistisch: Hubschrauber
    // in 3m abstand ist flüsterleise..!").
    void setKindLevelDb (float levelDb);

    // Fahrtwind: Betrag der Quellgeschwindigkeit in m/s. Treibt in jeder
    // Betriebsart das Rauschen mit, das allein vom Fliegen kommt.
    void setAirspeed (float metresPerSecond);

    // Stärke der Druckstöße aus der Raketendüse, 0..1 (@dpa: "aus einem
    // Raketenantrieb kann Überschall druck rauskommen. das mus einerseits
    // einstellbar sein, andererseits müssen diese noisy N-Waves zu hören sein
    // (auch wenn die Rakete noch um subsonic ist)").
    //
    // Es sind echte N-Wellen, siehe nWaveShape() und setRocketShockShape().
    // Ihre Länge und Folge regeln die beiden dortigen Größen.
    //
    // Das sind NICHT die N-Wellen der Ausbreitung (die entstehen in
    // PropagationPath und hängen an M_r, der auf den Hörer bezogenen
    // Mach-Zahl). Das hier sitzt im Quellsignal selbst: der Abgasstrahl einer
    // Rakete ist selbst überschallschnell, seine Stoßzellen knallen, ganz
    // unabhängig davon, wie schnell die Rakete durch die Luft fliegt.
    void setRocketShock (float amount01);

    // Klangformung des Strahlrauschens bzw. des Raketenbruellens (@dpa
    // 20260824: "Das braucht einen Klangveraenderungsknob und/oder eine
    // Auswahl an vorgefertigten (multiband?) Filtern (am besten beides)").
    //
    // voiceIndex zeigt in die jeweilige Vorlagentabelle (jetVoiceTable /
    // rocketVoiceTable in der .cpp, Reihenfolge = Params::jetVoice bzw.
    // Params::rocketVoice). tone01 schiebt stufenlos darueber: 0 dunkel,
    // 0,5 die Vorlage unveraendert, 1 hell.
    void setJetVoice (int voiceIndex, float tone01);
    void setRocketVoice (int voiceIndex, float tone01);

    // Form der Druckstoesse aus der Raketenduese. sizeMetres ist die
    // Ausdehnung einer Stosszelle, daraus wird die Dauer der N-Welle
    // (T = 2*size/c); rateHz die mittlere Folge der Stoesse.
    void setRocketShockShape (float sizeMetres, float rateHz);

    // --- Abstandsnaht der Rakete (@dpa 20260825) ---
    //
    // Der Generator sitzt VOR der Ausbreitung und kennt von sich aus keinen
    // Abstand. Fuer den Pegel braucht er auch keinen: die 1/R-Daempfung der
    // Ausbreitung greift nachweislich (load_check, Abschnitt
    // "Raketen-Abstand": -19,9 dB je Faktor zehn). Zwei Dinge kann sie aber
    // nicht leisten, weil sie erst NACH dem Generator passieren:
    //
    //   - Die KLANGFARBE aus der Ferne. Ein Triebwerk in zwei Kilometern
    //     klingt nicht wie ein leises Triebwerk in zwei Metern, es klingt
    //     tiefer (@dpa: "Energie von weit Weit weg"). Der Abstand schiebt die
    //     drei Baender nach unten, wie stark, sagt der Regler dahinter
    //     (setRocketFarColour).
    //   - Die Tiefe der Absenkung durch die Druckstoesse. Die Stoesse der
    //     Rakete entstehen hier im Generator und nicht als Kegelankunft in der
    //     Ausbreitung - deshalb kam der Regler "Duck-Reichw." bei der Rakete
    //     bisher gar nicht an (@dpa: "ich habe bei Rakete noch keinen
    //     Unterschied zwischen den Druckreichweiten gehoert"). Mit dem
    //     Abstand hier greift dieselbe Formel wie in
    //     PropagationPath::triggerNWave: range / (range + R).
    //
    // Gefuettert vom Processor, genau wie setRotorInPlane/setRotorFlightSpeed.
    void setRocketDistance (float metres);

    // Wie stark die Entfernung die Klangfarbe nach unten schiebt, in Oktaven
    // je Verdopplung des Abstands ueber rocketFarRefMetres hinaus. 0 = gar
    // nicht (Klang wie am Startplatz, egal wie weit weg).
    void setRocketFarColour (float octavesPerDoubling);

    // Absenkung des Bruellens waehrend eines Druckstosses. Dieselben beiden
    // Werte, die auch PropagationPath::setShockDuck bekommt - ein Reglerpaar,
    // zwei Orte, an denen Stossfronten entstehen.
    void setRocketShockDuck (float amount01, float rangeMetres);

    // Stärke des Blattknallens am Rotor, 0..1. Die Blattspitzen laufen
    // schneller als der Rumpf und schlagen bei jedem Umlauf in die eigene
    // Wirbelschleppe - das ist das laute Knattern (@dpa: "beim fliegen
    // überschlagen sie sich ... lautes Knattern").
    void setRotorSlap (float amount01);

    //------------------------------------------------------------------
    // Echter Rotor-Doppler (@dpa 20260824: "das knattern.. ist mir noch nicht
    // echt genug. es muss sich beim Ueberflug veraendern. Also: Knattern soll
    // Umschaltbar auf Doppler() sein - aus: wie derzeit: gefaket - an: die
    // noises werden im 'original' Kreis gedreht und ergeben daher den
    // (hoffentlich echteren) Sound").
    //
    // Aus: das Schwirren atmet mit der Blattfolge, der Schlag kommt je Blatt.
    // Eine nachgebaute Modulation, die von der Blickrichtung nichts weiss und
    // sich beim Ueberflug deshalb auch nicht aendert.
    //
    // An: jedes Blatt ist eine eigene Quelle auf der Kreisbahn. Sein Rauschen
    // laeuft durch eine eigene Verzoegerungsleitung, deren Laenge sich mit
    // seiner Stellung auf dem Kreis aendert - das IST der Doppler, samt der
    // Kammstruktur zwischen den Blaettern. Nichts wird nachgebaut, und weil
    // die Laufzeit an der Geometrie haengt, aendert sich das Knattern beim
    // Ueberflug von selbst.
    void setRotorDoppler (bool shouldUseDoppler);

    // Rotordrehzahl in das Frequenzraster des Motors rasten (@dpa 20260824:
    // ein Schalter "Quant" fuer "in Obertoene quantisieren").
    //
    // Die Unwucht sitzt seit jeher auf f_base/2 mal einer ganzen Oktave, ist
    // also mit dem Motor verkoppelt - und genau deshalb klingt sie wirksam.
    // Der Rotor lief frei dagegen: seine Blattfolge und der Motortakt gingen
    // aneinander vorbei, und was man hoerte, war eine wandernde Schwebung
    // statt eines Klanges.
    //
    // Eingerastet wird auf das naechste GANZZAHLIGE Verhaeltnis zur
    // Grundfrequenz, nach oben wie nach unten (f_base * k oder f_base / k).
    // Ein Hauptrotor dreht viel langsamer als der Motor, der uebliche Fall ist
    // also der Teiler - bei 434 RPM und 3,63 Hz liegt der Rotor auf f_base/2.
    //
    // Der Regler behaelt seinen Wert; gerastet wird, was der Generator daraus
    // macht. Aus bleibt es beim frei laufenden Rotor - die Schwebungen, die
    // dabei entstehen, sind ausdruecklich erwuenscht (@dpa: "die zwei leicht
    // unterschiedlichen Tempi ... fuehren zu interessanten haerte und
    // weichheits verlaeufen").
    void setRotorQuantise (bool shouldQuantise);

    // Blattlaenge in Metern. Sie bestimmt, wie weit die Laufzeit je Umlauf
    // schwankt (2r/c bei flacher Sicht) und damit die Tiefe des Effekts.
    void setRotorRadius (float metres);

    // Wie stark die Sichtlinie zum Hoerer IN der Rotorebene liegt: 1 = von der
    // Seite (die Blaetter kommen abwechselnd auf einen zu und laufen weg,
    // volle Laufzeitschwankung), 0 = senkrecht von unten oder oben (alle
    // Blaetter gleich weit weg, keine Schwankung mehr). Genau daran aendert
    // sich das Knattern beim Ueberflug.
    //
    // Gerechnet wird das im Processor, der als Einziger beide Positionen
    // kennt; der Generator bekommt nur die fertige Zahl.
    void setRotorInPlane (float factor01);

    // Geschwindigkeit der Quelle ENTLANG der Sichtlinie zum Hoerer, in m/s,
    // positiv beim Anflug (@dpa 20260824: "hoehren tut man bei einem
    // Hubschrauberueberflug wirklich 'ganz schoene Spitzen' die an
    // Ueberschall erinnern").
    //
    // Genau daran liegt es: die Blattspitze dreht sich nicht nur, der ganze
    // Rotor fliegt auch. Auf der vorlaufenden Seite addieren sich beide
    // Geschwindigkeiten. Bei 6 m Blatt, 5 Umdrehungen/s und 70 m/s Reisetempo
    // sind das 258 m/s, also Mach 0,75 - und ueber Mach 0,88 loesen sich die
    // Verdichtungsstoesse an der Blattspitze von ihr ab und laufen als eigene
    // Wellen davon. Das ist der Knall, den man hoert, obwohl nichts
    // Ueberschall fliegt.
    //
    // Der Doppler des RUMPFES steckt schon in der Ausbreitung; hier geht es
    // allein darum, wie schnell die Blattspitze durch die Luft faehrt.
    void setRotorFlightSpeed (float metresPerSecond);

private:
    // Ein Sägezahn-Teilton: Reglerwerte atomar, Phase ist reiner Audio-Thread-
    // Zustand (kein Fremdzugriff, daher kein Atomic nötig).
    struct Harmonic
    {
        std::atomic<float> ratio { 1.0f };
        std::atomic<float> detuneCents { 0.0f };
        std::atomic<float> trackAmount { 1.0f };
        std::atomic<float> levelDb { 0.0f };

        double phase = 0.0;   // wrappt per Subtraktion von 1.0, nie fmod/Clamp (Plan-Vorgabe)

        // Wellenform dieses einen Teiltons, siehe setSineMode(). Geblendet
        // statt geschaltet: bei Phase 0,25 steht der Sägezahn auf -0,5 und der
        // Sinus auf +1, ein harter Wechsel wäre ein Sprung im Signal.
        std::atomic<float> sineTarget { 0.0f };
        double sineBlend = 0.0;
    };

    // Standard-PolyBLEP-Korrektur an den Sägezahn-Flanken, damit die
    // Unstetigkeit nicht als Alias-Rauschen ins Spektrum faltet.
    static double polyBlep (double phase, double phaseInc);

    // --- Dreiband-Formung eines Rauschens ---
    //
    // Drei parallele Filter über derselben Rauschquelle - ein Tiefband, ein
    // Mittenband, ein Hochband - mit je eigenem Pegel, dazu ein schmaler
    // vierter Zweig für einen singenden Ton im Rauschen (das Kreischen eines
    // Verdichters; bei den meisten Vorlagen steht sein Pegel auf null).
    //
    // Warum drei Bänder und nicht ein durchstimmbares Filter: ein einzelner
    // Hochpass, wie das Strahlrauschen ihn bisher hatte, kann nur heller oder
    // dunkler werden. Ein Turbofan klingt aber nicht wie ein leiserer
    // Nachbrenner, sondern anders VERTEILT - viel Bypass-Rauschen unten, wenig
    // Schärfe oben. Das lässt sich nur über getrennt regelbare Bänder sagen.
    struct BandVoicing
    {
        juce::dsp::StateVariableTPTFilter<float> low, mid, high, narrow;

        void prepare (const juce::dsp::ProcessSpec& spec);
        void reset();
    };

    // Setzt die Filter einer Formung auf `preset`, verbogen um `tone01`
    // (0 dunkel .. 0,5 neutral .. 1 hell) und mitgezogen vom Gas `u`.
    // Liefert die drei Bandpegel zurück, die process() dann braucht.
    struct VoiceGains { double low, mid, high, narrow; };

    // darkOctaves = wie weit der Klangfarbe-Regler nach UNTEN reicht, in
    // Oktaven. Nach oben ist der Weg fuer beide Betriebsarten gleich.
    // extraOctavesDown = zusaetzliche Verschiebung nach unten, die nicht am
    // Regler haengt, sondern an der Entfernung (siehe setRocketDistance).
    // lowQ = Guete des Tiefbands. Je Betriebsart verschieden, siehe
    // jetLowQ/rocketLowQ.
    VoiceGains applyVoicing (BandVoicing& v, const EngineVoicePreset& preset,
                             double tone01, double u,
                             double darkOctaves = 1.1,
                             double extraOctavesDown = 0.0,
                             double lowQ = 0.6);

    // Reglerweg nach unten je Betriebsart, siehe applyVoicing().
    //
    // Die Duese behaelt die gute Oktave, mit der sie eingehoert wurde. Die
    // Rakete braucht mehr: 3,2 Oktaven bringen die Vorlage "Ferne" von 20 Hz
    // auf gut 2 Hz, und mit dem Gas (uScale, mindestens 1) und der
    // Filteruntergrenze von 4 Hz landet der praktisch erreichbare Tiefpunkt
    // genau dort, wo @dpa ihn haben will.
    static constexpr double jetDarkOctaves    = 1.1;
    static constexpr double rocketDarkOctaves = 3.2;

    // Guete des Tiefbands je Betriebsart.
    //
    // Die Duese behaelt ihre flachen 0,6 - ihr Tiefband ist ein Fundament
    // unter den Mitten, kein eigener Ton, und mit Resonanz saenge es.
    //
    // Die Rakete bekommt 1,1. Ein flacher Tiefpass laesst ALLES unterhalb
    // seiner Eckfrequenz durch, und bei einer Eckfrequenz von 30 Hz ist der
    // groesste Teil davon unhoerbar - die Energie verlief sich nach unten,
    // statt dort zu sitzen, wo der Regler sie hinstellt. Mit Resonanz bekommt
    // das Tiefband einen Schwerpunkt AN seiner Eckfrequenz. Das ist auch das
    // ehrlichere Bild: ein Raketenstrahl hat eine Vorzugsfrequenz, kein
    // Rauschplateau bis null Hertz.
    static constexpr double jetLowQ    = 0.6;
    static constexpr double rocketLowQ = 1.1;

    // Ab hier abwaerts zahlt sich Bandbreiten-Ausgleich nicht mehr in
    // Lautheit aus, sondern nur noch in Auslenkung - siehe place() in der
    // .cpp. 30 Hz ist die Gegend, in der Tiefton aufhoert, ein Ton zu sein,
    // und anfaengt, ein Druck zu sein; darunter wird er zwar noch gespuert,
    // aber der Zugewinn je Dezibel faellt steil ab.
    static constexpr double audibleFloorHz = 30.0;

    // Ein Rauschsample durch die drei Bänder, mit den Pegeln aus applyVoicing().
    static double voiceSample (BandVoicing& v, const VoiceGains& g, double in, double narrowIn);

    // Die klassische N-Welle als reine Form, in [-1, +1]: senkrechter Sprung
    // auf +1, lineare Gerade durch null, senkrechter Rücksprung von -1.
    //
    // Bewusst eine EIGENE Kopie und nicht PropagationPath::nWaveAt(): die dort
    // hängt an der auf den Hörer bezogenen Mach-Zahl und bleibt stumm, solange
    // die Quelle langsamer als der Schall fliegt. Hier ist aber nicht die
    // Rakete überschallschnell, sondern ihr Abgasstrahl - die Stoßzellen
    // knallen auch im Stand (@dpa: "auch wenn die Rakete noch um subsonic ist").
    static double nWaveShape (double t, double duration, double rise);

    static constexpr int numHarmonics = 4;

    // Zeitkonstante der Wellenform-Überblendung je Teilton (siehe Harmonic).
    double sineBlendCoeff = 1.0;
    static constexpr double sineBlendSeconds = 0.02;

    // Sammelschalter der vier Teiltöne (siehe setHarmonicsOn). Nicht hart
    // geschaltet, sondern über denselben Ein-Pol geblendet wie die
    // Wellenform: ein Sprung von voller Amplitude auf null wäre eine
    // senkrechte Kante im Signal, also ein Knacken.
    std::atomic<bool> harmonicsOn { true };
    double            harmonicsGain = 1.0;

    // --- Betriebsart ---
    //
    // engineKind ist der Wunsch von außen, activeKind der gerade gerechnete.
    // Unterscheiden sie sich, fährt kindFade auf null, DANN wird umgeschaltet,
    // dann fährt kindFade wieder hoch. So gibt es beim Wechsel eine kurze
    // Lücke statt eines Sprungs - und zwei komplette Betriebsarten gleichzeitig
    // zu rechnen erspart es auch.
    std::atomic<int> engineKind { 0 };
    int    activeKind = 0;
    double kindFade   = 1.0;
    double kindFadeStep = 1.0;   // je Sample, aus kindFadeSeconds

    static constexpr double kindFadeSeconds = 0.03;

    // Gesamtpegel der Betriebsart, linear. Wirkt auf alles außer "Frei".
    std::atomic<float> kindLevelDb { 0.0f };

    // Fahrtwind, siehe setAirspeed(). Der Bezugswert ist die Geschwindigkeit,
    // bei der das Fahrtwindrauschen seinen vollen Pegel erreicht - darüber
    // wächst es weiter, aber flacher (Wurzel), sonst überdeckte es bei
    // Überschall alles andere.
    std::atomic<float> airspeedMps { 0.0f };
    static constexpr double airspeedRefMps = 120.0;

    // Pegel des Fahrtwindrauschens bei der Bezugsgeschwindigkeit.
    static constexpr double windLevel = 0.35;

    juce::dsp::StateVariableTPTFilter<float> windFilter;
    juce::Random windRandom;

    // --- Düsenantrieb ---
    //
    // Ein Verdichterton (nicht vier: @dpa "braucht es höchstens 1
    // Oscillator") und darüber das Strahlrauschen, das den Klang trägt. Der
    // Ton ist die Blattfolgefrequenz des Fans, also ein Vielfaches der
    // Wellendrehzahl - er sitzt hoch und ist deutlich leiser als der Strahl.
    double jetTonePhase = 0.0;
    BandVoicing jetVoicing;
    juce::Random jetRandom;
    juce::Random jetNarrowRandom;

    // Vorlage und Klangfarbe des Strahlrauschens, siehe setJetVoice().
    std::atomic<int>   jetVoiceIndex { 0 };
    std::atomic<float> jetTone { 0.5f };

    static constexpr double jetToneRatio = 8.0;    // Fanblätter je Umdrehung
    static constexpr double jetToneLevel = 0.10;   // gegen das Strahlrauschen
    static constexpr double jetNoiseLevel = 0.85;

    // --- Raketenantrieb ---
    //
    // Kein Ton, nur Brüllen: tiefes Breitbandrauschen. Dazu die Druckstöße
    // der Stoßzellen im Strahl, siehe setRocketShock() - kurze, harte
    // Rauschstöße in unregelmäßigem Abstand, nicht ein sauberer Puls.
    BandVoicing rocketVoicing;
    juce::Random rocketNoiseRandom;
    juce::Random rocketNarrowRandom;

    std::atomic<int>   rocketVoiceIndex { 0 };
    std::atomic<float> rocketTone { 0.5f };

    // Eine laufende Stoßwelle aus dem Strahl. Mehrere davon gleichzeitig,
    // weil Stoßzellen sich nicht abwarten: bei hoher Folge überlappen sie,
    // und genau diese Überlagerung ist das Knattern einer Rakete. Ein
    // einzelner Platz würde stattdessen jeden noch laufenden Stoß abschneiden.
    struct Shock
    {
        double phase    = 0.0;   // Sekunden seit Auslösung
        double duration = 0.0;   // T der N-Welle, 0 = Platz frei
        double rise     = 0.0;   // Anstiegszeit der beiden Fronten
        double amp      = 0.0;
    };

    // Wieviele Stöße gleichzeitig laufen dürfen. Grosszügig bemessen: bei
    // langen Wellen und hoher Folge überlappen viele, und ein zu kleiner
    // Vorrat hiesse, laufende Stöße mittendrin abzuschneiden - das wäre ein
    // Sprung im Signal, also ein Knacken.
    //
    // Seit @dpa 20260825 ist die Stosslaenge nach unten auf 10 m begrenzt
    // ("die Stosslaenge min. 10m sonst klingt es irgendwie unecht"), und
    // zehn Meter Zelle sind schon 58 ms Wellendauer. Bei hoher Folge liegen
    // dann Dutzende uebereinander; mit 32 Plaetzen fiele die Mehrzahl aus,
    // und das Knattern verlöre genau die Dichte, die es ausmacht.
    static constexpr int maxShocks = 160;

    std::array<Shock, maxShocks> shocks {};

    juce::Random shockRandom;
    double shockCountdown = 0.0;   // Samples bis zum nächsten Stoß

    // Huellwert der Absenkung durch die Stoesse (siehe setRocketShockDuck).
    // Springt beim Einsatz eines Stosses auf dessen Staerke und klingt mit
    // rocketDuckRelease ab.
    double shockDuckEnv = 0.0;
    std::atomic<float> rocketShock { 0.0f };

    // Form der Stöße, siehe setRocketShockShape().
    std::atomic<float> rocketShockSizeM { 0.5f };
    std::atomic<float> rocketShockRateHz { 18.0f };

    static constexpr double rocketNoiseLevel = 1.10;

    // --- Abstandsnaht, siehe setRocketDistance() ---
    std::atomic<float> rocketDistanceM  { 0.0f };
    std::atomic<float> rocketFarColour  { 1.0f };
    std::atomic<float> rocketDuckAmount { 0.0f };
    std::atomic<float> rocketDuckRange  { 300.0f };

    // Ab dieser Entfernung faengt die Verfaerbung an zu zaehlen. Darunter
    // steht man praktisch am Triebwerk, und dort soll die Vorlage klingen wie
    // sie ist. 30 m ist die Groessenordnung, ab der eine Rakete ueberhaupt
    // erst "weit weg" sein kann.
    static constexpr double rocketFarRefMetres = 30.0;

    // Zeitkonstante, mit der das Bruellen nach einem Stoss zurueckkommt.
    // Derselbe Gedanke wie PropagationPath::shockDuckRelease: waehrend der
    // Front kommt nichts durch, danach oeffnet es wieder.
    static constexpr double rocketDuckRelease = 0.015;

    // Pegel einer einzelnen N-Welle. Deutlich über dem Brüllen darunter, und
    // das ist Absicht: ein Druckstoß, der nicht aus dem Rauschen herausragt,
    // ist kein Stoß mehr (@dpa 20260824: "donnernde N-Waves"). Bei voll
    // aufgedrehtem Regler darf das übersteuern - dafür gibt es den sichtbaren
    // Begrenzer, und ein stiller Deckel wäre hier genau das Falsche.
    static constexpr double rocketShockLevel = 4.00;

    // Schallgeschwindigkeit für die Umrechnung Stoßzellen-Größe -> Wellendauer.
    // Fester Wert, kein Wetter: hier geht es um die Verhältnisse IM Strahl,
    // nicht um den Weg durch die Umgebungsluft (das rechnet die Ausbreitung).
    static constexpr double shockSpeedOfSound = 343.0;

    // Anteil der Wellendauer, den die beiden Fronten zum Ansteigen brauchen.
    // Klein heißt steil heißt knallig; ganz senkrecht ginge nicht, das wäre
    // pures Aliasing.
    //
    // Ein halbes Prozent: die Quelle steht hier direkt an der Düse, und dort
    // ist eine Stoßfront noch nicht durch den Weg durch die Luft verbreitert.
    // Bei den kurzen Wellen greift ohnehin die Untergrenze von zwei Samples,
    // bei den langen (Donnern) bleibt sie mit ein paar zehntel Millisekunden
    // trotzdem knackig.
    static constexpr double shockRiseFraction = 0.005;

    // --- Rotor (Hubschrauber und Propeller) ---
    //
    // Der Rotor ist KEIN Klick. Aus der Nähe schwirrt er - ein Rauschen, das
    // mit jedem vorbeilaufenden Blatt an- und abschwillt - und beim Fliegen
    // knallen die Blätter obendrein (@dpa: "das ist also aus entfernung ein
    // sich im Kreis drehene Noise mit 'dm großen Knallen'").
    //
    // Umgesetzt als bandpassgefiltertes Rauschen, dessen Pegel mit der
    // Blattfolge moduliert wird, plus ein kurzer, harter Rauschstoß je Blatt.
    std::atomic<float> heliRotorHz { 5.0f };
    std::atomic<float> heliBladeCount { 4.0f };
    std::atomic<float> rotorSlap { 0.5f };

    double rotorPhase = 0.0;
    double slapEnv    = 0.0;

    juce::dsp::StateVariableTPTFilter<float> rotorFilter;
    juce::Random rotorRandom;
    juce::dsp::StateVariableTPTFilter<float> slapFilter;
    juce::Random slapRandom;

    //------------------------------------------------------------------
    // Rotor-Doppler, siehe setRotorDoppler().
    std::atomic<bool>  rotorDoppler { false };
    std::atomic<bool>  rotorQuantise { false };
    std::atomic<float> rotorRadiusM { 5.0f };
    std::atomic<float> rotorInPlane { 1.0f };
    std::atomic<float> rotorFlightSpeed { 0.0f };

    // Ab dieser Blattspitzen-Machzahl loesen sich die Stoesse von der Spitze
    // ab (Delokalisierung). Darunter bleibt der Ueberdruck an das Blatt
    // gebunden und wird mit ihm herumgetragen, darueber laeuft er als eigene
    // Welle davon - deshalb faengt genau hier das harte Knallen an.
    static constexpr double bladeDelocalisationMach = 0.88;

    // Pegel des abgeloesten Stosses an der Blattspitze. Deutlich ueber dem
    // Schwirren: er ist der Grund, warum ein schnell fliegender Hubschrauber
    // aus einem Schwirren ein Hammern macht.
    static constexpr double bladeShockLevel = 3.0;

    // Dauer der abgeloesten Stosswelle. Kurz genug, dass sie als Schlag und
    // nicht als Ton wahrgenommen wird - aus der Naehe ist ein Blattstoss ein
    // Peitschenknall, kein Brummen.
    static constexpr double bladeShockSeconds = 0.0015;

    // Anteil der Dauer, den die Flanken brauchen. Klein: es soll eine Kante
    // sein. Ganz ohne ginge nicht, das waere reines Aliasing.
    static constexpr double bladeShockRise = 0.06;

    // Obergrenze der Richtwirkung eines Blattes, siehe slapSharpness in der
    // .cpp. 36 dB.
    static constexpr double maxBladeDirectivity = 64.0;

    // Obergrenze der Blattzahl, siehe Params::heliBladeCount (2..8).
    static constexpr int maxRotorBlades = 8;

    // Umdrehungsphase (0..1 je UMDREHUNG), nicht Blattfolge: der Kreis, auf
    // dem die Blaetter sitzen, dreht sich einmal je Umdrehung.
    double rotorRevPhase = 0.0;

    // Je Blatt eine eigene Verzoegerungsleitung und eine eigene Rauschquelle.
    // Eigene Quellen sind Pflicht, keine Sparsamkeit: dieselbe Folge N-mal
    // verzoegert waere ein Kammfilter auf EINEM Rauschen, kein Rotor aus N
    // Blaettern - und genau danach klingt es dann auch (@dpa 20260824:
    // "achte bitte bei ab 2 unterschiedlichen Noises (z.B. Propeller) darauf,
    // dass sie unterschiedlich sind").
    struct Blade
    {
        std::vector<float> ring;
        int                writePos = 0;
        double             slapEnv  = 0.0;
        double             prevPhase = 0.0;
        juce::Random       random;

        // Laufende N-Form des abgeloesten Stosses, in Samples seit seiner
        // Ausloesung. Negativ heisst "gerade keiner".
        double             shockPhase = -1.0;
    };

    std::array<Blade, (size_t) maxRotorBlades> blades;

    // Laenge der Leitungen: die groesste Laufzeitschwankung, die vorkommen
    // kann, ist 2r/c bei flacher Sicht. Bemessen wird nach dem groessten
    // einstellbaren Radius, damit prepare() der einzige Ort mit einer
    // Allokation bleibt.
    static constexpr double maxRotorRadiusM = 40.0;

    static constexpr double rotorSwishLevel = 0.55;
    static constexpr double rotorSlapLevel  = 1.60;
    static constexpr double slapDecayMs     = 9.0;

    // Wie stark das Schwirren mit der Blattfolge atmet: 0 wäre gleichmäßiges
    // Rauschen, 1 zerhackt es ganz. Dazwischen bleibt der Rest, der zwischen
    // den Blättern stehen bleibt.
    static constexpr double rotorSwishDepth = 0.85;

    // --- Propeller ---
    //
    // Ein leiser Ton (@dpa: "höchstens einen (leiseren)") und derselbe
    // Blattschlag wie am Rotor, nur weicher.
    double propTonePhase = 0.0;
    static constexpr double propToneLevel = 0.22;

    // Bezugsdrehzahl der Track-Formel aus Plan 3.10 (f_i-Formel), keine
    // Automation, daher als Konstante statt Parameter.
    static constexpr double rpmRef = 1000.0;

    // Obergrenze des rpm-Parameters aus Params.cpp - dient hier nur der
    // Normierung auf u = RPM/RPM_max in [0,1] für Rauschband und Jitter.
    static constexpr double rpmMaxForNormalisation = 12000.0;

    std::array<Harmonic, numHarmonics> harmonics;

    std::atomic<float> rpm { 1000.0f };

    std::atomic<float> noiseFcLo { 400.0f };
    std::atomic<float> noiseFcHi { 3000.0f };
    std::atomic<float> noiseGainLoDb { -24.0f };
    std::atomic<float> noiseGainHiDb { -6.0f };
    std::atomic<float> noiseQ { 1.2f };

    std::atomic<float> jitterAmountPercent { 1.5f };
    std::atomic<float> jitterRateHz { 8.0f };

    // "Unwucht": periodische Amplitudenkomponente bei f_base/2 für den
    // Zündtakt eines Viertakters (Plan 3.10, optional, Default 0 = aus).
    std::atomic<float> imbalanceAmount { 0.0f };
    std::atomic<float> imbalanceOctave { 0.0f };

    // TPT-Struktur ist explizit für Modulation pro Sample ausgelegt (siehe
    // JUCE-Doku), darum werden Cutoff/Resonanz hier ohne Zögern block- bzw.
    // sample-genau nachgeführt statt vorsichtig geglättet.
    juce::dsp::StateVariableTPTFilter<float> noiseFilter;
    juce::dsp::StateVariableTPTFilter<float> jitterFilter;

    // Getrennte Rauschquellen für Motorband und Jitter, sonst wären beide
    // Modulationen korreliert und der Jitter würde im Rauschband durchhören.
    juce::Random noiseRandom;
    juce::Random jitterRandom;

    double halfPhase = 0.0;   // Unwucht-AM-Phase bei f_base/2, wrappt wie die Sägezähne

    double currentSampleRate = 48000.0;

    // Für dominantFrequencyHz(): f_base der aktuellen RPM (unjittert, siehe
    // Plan: "gibt f_base zurück"), block-genau aktualisiert. Wird vermutlich
    // von einem anderen Thread als dem Audio-Thread gelesen (Fade-Policy),
    // daher atomar statt Plain-Member.
    std::atomic<double> lastDominantFrequency { 1000.0 / 60.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngineGenerator)
};
