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

    // Bahngeschwindigkeit des Wacklers in m/s, siehe
    // PositionJitter::setSpeed(). Zusammen mit dem Ausschlag sind das die
    // ZWEI Groessen, die die Bewegung beschreiben - die Frequenz ergibt sich
    // aus beiden und ist kein Regler mehr.
    constexpr const char* srcJitterSpeed = "srcJitterSpeed";

    // --- Abgeloest, nur noch zum Umrechnen alter Zustaende ---
    //
    // "srcJitterRateHz" (Hektik, eine Frequenz) und "srcJitterMaxSpeed"
    // (Jit Max, ein Tempo-Deckel) sind nicht mehr Teil der Parameterliste.
    // Die beiden IDs bleiben hier stehen, weil setStateInformation() sie in
    // einem alten Zustand erkennen und in das neue Tempo (srcJitterSpeed)
    // umrechnen muss - sonst klingen bestehende Presets nach dem Laden
    // anders (@dpa 20260825: "ist das mit der Hektik zu kompliziert das
    // 'passende Fenster' zu finden").
    constexpr const char* srcJitterRateLegacy     = "srcJitterRateHz";
    constexpr const char* srcJitterMaxSpeedLegacy = "srcJitterMaxSpeed";

    // Ein/Aus fuer das Wackeln insgesamt - Quelle UND Klone (@dpa 20260820).
    // Ein eigener Schalter statt "Amount auf 0 drehen", weil der eingestellte
    // Ausschlag beim Abschalten erhalten bleiben soll: aus, hoeren, wieder an,
    // ohne den Regler neu zu suchen.
    constexpr const char* srcJitterOn = "srcJitterOn";

    // Anteil der Hoehe am Wackeln, siehe PositionJitter::setZFactor().
    constexpr const char* srcJitterZAmount = "srcJitterZAmount";




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
    // Eine eigene Beschleunigungsgrenze gibt es nicht mehr (@dpa 20260825:
    // "ich verstehe ja bis heute nicht warum es zwei regler sind. Ich habe
    // die besten Ergebnisse, wenn ich sie gleich einstelle"). Sie folgt aus
    // slewVmax, siehe SlewLimiter::accelTimeSeconds.
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

    // Vorbeiflug in Dauerschleife (@dpa 20260821: "neben vorbeiflug bitte ein
    // toggle Loop"). Am Ende der Strecke wird der Flug nicht gehalten, sondern
    // von vorn gestartet - ueber denselben Weg wie ein frisch ausgeloester
    // Flug, damit der Ruecksprung an den Anfang als Geometriewechsel
    // ueberblendet wird statt als Positionssprung zu knacken.
    constexpr const char* flyLoop     = "flyLoop";

    // --- Physik ---
    constexpr const char* boomLimitDb     = "boomLimitDb";
    constexpr const char* airAbsorbAmount = "airAbsorbAmount";

    // Ausbreitungsmedium (Source/Physics/Medium.h, MediumState): Temperatur
    // bestimmt c(T) und damit direkt die Mach-Schwelle - der Unterschied
    // zwischen einer Peitsche auf Meereshöhe und einem Jet in grosser Höhe
    // (dort ist es kalt, c ist niedriger). Höhe wirkt unabhängig davon über
    // die Luftdichte auf den Ausgangspegel (siehe PluginProcessor::
    // applyParameters, "--- Ausgang ---") - beide Regler sind bewusst
    // getrennt, siehe Tooltips.
    constexpr const char* airTempC    = "airTempC";
    constexpr const char* airAltitude = "airAltitude";

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

    // Schaerfe der beiden Stossfronten (@dpa 20260827: "mir sind die
    // Ueberschallecken meist zu zahm, zu tiefgepasst, zu weich ... ich will
    // den echten knall"). Getrennt von nWaveSize, denn Groesse und Schaerfe
    // sind zwei Dinge: die Groesse macht die Welle laenger und tiefer, die
    // Schaerfe entscheidet, ob ihr Einsatz eine Kante ist oder ein Wusch.
    // 0,5 = Mitte, siehe PropagationPath::setNWave().
    constexpr const char* nWaveEdge = "nWaveEdge";

    // Staerke der DRUCKWELLE - der langsamen Auslenkung der Nulllinie, auf
    // der der uebrige Sound reitet (@dpa-Skizze "Druckwelle - 1", 20260827:
    // "die Druckwelle ist mir fast zu wenig! sie soll einen Regler kriegen
    // fuer lauter ... Aber das hat nichts mit der Fahne zu tun").
    //
    // Getrennt von nWaveGainDb, weil es zwei verschiedene Dinge sind: der
    // Pegel regelt den ganzen Knall, dieser hier nur den Anteil zwischen den
    // beiden Stossfronten. 0 laesst allein die Fronten stehen (den
    // Doppelknall), 1 ist die vollstaendige N-Welle, darueber wird die
    // Auslenkung betont.
    constexpr const char* nWavePressure = "nWavePressure";

    // Pegel des ZEITVERKEHRT gehoerten Anteils in dB (siehe
    // PropagationPath::setReverseGain). Bei Ueberschall liefert der Loeser
    // mehrere Hoerwege; einer davon laeuft rueckwaerts. Real geht der neben dem
    // vorwaerts laufenden weitgehend unter, im Modell steht er gleich laut
    // daneben (@dpa 20260821: "die Lautstaerke des Rueckwaertssounds muss
    // leiser"). 0 dB = unveraendert.
    // Wellenform der vier Motor-Teiltoene (@dpa 20260823: "mach die 4 Osc
    // umschaltbar auf Sines"). Aus = PolyBLEP-Saegezahn wie bisher, an =
    // reiner Sinus. Gilt fuer alle vier gemeinsam - sie sind ein Klang, keine
    // vier Einzelstimmen.
    // Wellenform JE Teilton (@dpa 20260824: "der sinus soll (zumindest bei
    // Hubschrauber und Propeller) für jeden osc setzbar sein"). Aus =
    // PolyBLEP-Sägezahn, an = reiner Sinus.
    constexpr const char* harmSine[4] = { "harmSine1", "harmSine2", "harmSine3", "harmSine4" };

    // Sammelschalter der vier Teiltoene (@dpa 20260825). Ein Mute neben den
    // Level-Reglern, kein Pegel - die vier Level behalten ihre Werte.
    // Siehe EngineGenerator::setHarmonicsOn().
    constexpr const char* oscOn = "oscOn";

    // Gesamtpegel der Betriebsart in dB. Gilt für alles außer "Frei" - dort
    // machen die vier Teilton-Pegel den Pegel, und daran darf sich nichts
    // ändern, sonst klängen alte Snapshots anders.
    constexpr const char* engineLevelDb = "engineLevelDb";

    // Stärke der Druckstöße aus der Raketendüse und des Blattknallens am
    // Rotor - beides eigene Größen der jeweiligen Betriebsart.
    constexpr const char* rocketShock = "rocketShock";
    constexpr const char* rotorSlap   = "rotorSlap";

    // Klangformung der beiden Rausch-Betriebsarten (@dpa 20260824:
    // "Duesenantrieb hat einfach nur weises Rauschen? Das braucht einen
    // Klangveraenderungsknob und/oder eine Auswahl an vorgefertigten
    // (multiband?) Filtern (am besten beides)"). Es ist beides geworden:
    //
    //   ...Voice - fertige Dreiband-Formung, je Betriebsart eine eigene Liste.
    //              Duese und Rakete klingen verschieden, also haben sie
    //              verschiedene Vorlagen und nicht eine gemeinsame.
    //   ...Tone  - stufenlos darueber, 0 dunkel bis 1 hell, 0,5 = die Vorlage
    //              unveraendert. Kippt Baenderpegel UND Eckfrequenzen, sonst
    //              waere der Regler kaum zu hoeren.
    constexpr const char* jetVoice    = "jetVoice";
    constexpr const char* jetTone     = "jetTone";
    constexpr const char* rocketVoice = "rocketVoice";
    constexpr const char* rocketTone  = "rocketTone";

    // Form der Druckstoesse aus der Raketenduese (@dpa 20260824: "Die
    // Druckstoesse sind Ueberschall, also donnernde N-Waves"). Seitdem sind es
    // echte N-Wellen und keine Rauschstoesse mehr, und dafuer brauchen sie
    // dieselben zwei Groessen wie jede andere Stosswelle:
    //
    //   Size - Ausdehnung der Stosszelle in Metern. Daraus wird die Dauer der
    //          Welle (T = 2*Size/c), genau wie bei Params::nWaveSize. Klein =
    //          Peitschenknall, gross = Donnern.
    //   Rate - mittlere Folge der Stoesse in Hz. Einzelne Schlaege unten,
    //          zusammenhaengendes Grollen oben.
    constexpr const char* rocketShockSize = "rocketShockSize";

    // Wie stark die Entfernung die Klangfarbe der Rakete nach unten schiebt,
    // in Oktaven je Verdopplung des Abstands (@dpa 20260825: "Energie von
    // weit Weit weg"). Siehe EngineGenerator::setRocketFarColour().
    constexpr const char* rocketFarColour = "rocketFarColour";
    constexpr const char* rocketShockRate = "rocketShockRate";

    // OHNE WIRKUNG - siehe Params.cpp. Ersetzt durch extraPathGainDb.
    constexpr const char* reverseGainDb = "reverseGainDb";

    // Pegel der ZUSAETZLICHEN Hoerwege in dB - der "Fahne" nach dem
    // Vorbeiflug (@dpa 20260827: "ich will die Fahne weg haben!").
    //
    // Bei Ueberschall trifft Schall von mehreren Emissionszeitpunkten
    // gleichzeitig ein. Einer davon ist der juengste - der Weg, ueber den man
    // die Quelle dort hoert, wo sie gerade ist. Die anderen tragen aeltere
    // Emissionen nach und bilden zusammen den Nachlauf.
    //
    // Warum ein eigener Regler neben reverseGainDb: der dortige greift nur
    // ueber dTau = 1, also nur bei zeitverkehrt gelesenen Wegen. Gemessen im
    // Kreisflug-Szenario kommt der lauteste Beitrag der Fahne aber bei
    // dTau = -0,271 - er laeuft VORWAERTS und wurde von jener Blende nie
    // erfasst, auch am Anschlag nicht (-60 dB eingestellt, 2,3 dB gemessen).
    constexpr const char* extraPathGainDb = "extraPathGainDb";

    // Absenkung des uebrigen Schalls, waehrend eine Stossfront ueber den
    // Hoerweg laeuft (@dpa: "keine Noise vom Motor" waehrend der N-Welle).
    // Gemeint ist die ganze Welle, auch die Strecke zwischen Bug- und
    // Heckstoss: "es ist immer was zu hoeren zwischen den zwei knallen.. das
    // soll weg". Deshalb voll aufgedreht als Voreinstellung.
    // OHNE WIRKUNG - siehe Params.cpp, die Tiefe steht fest auf 1.
    constexpr const char* shockDuckAmount = "shockDuckAmount";

    // Entfernung, ab der die Absenkung nachlaesst (@dpa 20260824: "wir muessen
    // also bestimmen ab welcher entfernung die N-Wave noch 'echt' ist").
    // Siehe PropagationPath::setShockDuck().
    constexpr const char* shockDuckRange = "shockDuckRange";

    // Bewegungssprung hoerbar machen (@dpa 20260823: "der Vorbeiflug
    // 'Knall-Start' muesste ja mindestens subsonic zu hoeren sein ... Bisher
    // ist noch nicht zu hoeren!"). Der Sprung selbst bleibt lautlos - er wird
    // geschnitten, nicht geflogen (siehe CutState in PluginProcessor.h) -,
    // jumpBoom setzt die Druckwelle darauf, die dabei entsteht.
    constexpr const char* jumpBoom = "jumpBoom";

    // Laenge des Startknalls in Metern, siehe PropagationPath::setJumpSize().
    // Getrennt von nWaveSize: der Ueberschallknall bildet einen KOERPER ab,
    // der Startknall eine BESCHLEUNIGUNG - zwei verschiedene Dinge, die
    // vorher an derselben Zahl hingen.
    constexpr const char* jumpBoomSize = "jumpBoomSize";

    // Mindestdauer des Ausklangs, wenn ein Hoerweg an der Kaustik
    // verschwindet, in Millisekunden. Rechnerisch folgt sie aus der Physik,
    // faellt bei schnellen Vorbeifluegen aber immer auf die Untergrenze von
    // 1 ms - und ein voll ausgesteuerter Zweig, der in einer Millisekunde weg
    // ist, reisst hoerbar ab (@dpa zum rueckwaerts laufenden Anteil: "nur dass
    // es ploetzlich aufhoert"). Default 1 ms = bisheriges Verhalten.
    // OHNE WIRKUNG - siehe Params.cpp. Der Wert bleibt nur registriert, damit
    // gespeicherte Presets ihn behalten.
    constexpr const char* shadowTailMs = "shadowTailMs";

    // "Schrot"-Muster: Klone der Quelle. cloneTotal ist die Gesamtzahl, und alle
    // davon bekommen volle Loeserphysik - eine billige Nachbildung gibt es
    // nicht mehr (@dpa: "nur echte Klones, alles andere weg, keine 'billigen',
    // die bringen nichts").
    constexpr const char* cloneTotal  = "cloneTotal";
    constexpr const char* cloneSpread = "cloneSpread";

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

    // Betriebsart des Motors (@dpa 20260824: "in 'Motor' mehrere umschaltbar
    // machen"): Duesenantrieb/Raketenantrieb/Hubschrauber/Propeller/Frei.
    // Ueberschreibt KEINEN vorhandenen Regler - der Generator gewichtet nur,
    // wie stark Teiltoene/Rauschband beitragen, und schaltet je Betriebsart
    // einen eigenen Zusatzklang dazu (siehe EngineGenerator). "Frei" und
    // "Propeller" verhalten sich wie das bisherige Verhalten, "Propeller" ist
    // vorerst nur Platzhalter fuer die Geometrie (Source/Physics, nicht Teil
    // dieses Generators).
    constexpr const char* engineKind = "engineKind";

    // Nur in Betriebsart "Hubschrauber" wirksam: Rotordrehzahl (Hz, eigenes
    // Tempo, unabhaengig von der Motor-RPM) und Blattzahl. Multipliziert
    // ergeben sie die Blattschlagfrequenz - das charakteristische Wummern
    // (@dpa: "Motor, und Rotoren mit Geschwindigkeit extra").
    constexpr const char* heliRotorHz    = "heliRotorHz";
    constexpr const char* heliBladeCount = "heliBladeCount";

    // Echter Rotor-Doppler statt nachgebauter Modulation (@dpa 20260824:
    // "Knattern soll Umschaltbar auf Doppler() sein"). Siehe
    // EngineGenerator::setRotorDoppler(). heliRotorRadius ist die Blattlaenge
    // und bestimmt, wie tief die Laufzeit je Umlauf schwankt.
    constexpr const char* heliDoppler     = "heliDoppler";

    // Rotordrehzahl ins Frequenzraster des Motors rasten, siehe
    // EngineGenerator::setRotorQuantise().
    constexpr const char* heliQuantise    = "heliQuantise";
    constexpr const char* heliRotorRadius = "heliRotorRadius";

    // Nur in Betriebsart "Propeller" wirksam: die beiden Propeller sitzen an
    // den Fluegeln, also propSpan Meter auseinander und quer zur Flugrichtung
    // (@dpa 20260823: "(2) Propeller an Fluegeln (die in n meter auseinander,
    // immer flach in der Richtung des fluges sind..)"). Sie sind zwei eigene
    // Schallwege in der Geometrie, kein Klang im Generator - deshalb liegen
    // sie hier und nicht bei den Motor-Reglern.
    //
    // propLevelDb ist ihr Pegel. Sie kommen zum Rumpfschall hinzu, statt ihn
    // zu ersetzen.
    constexpr const char* propSpan    = "propSpan";
    constexpr const char* propLevelDb = "propLevelDb";

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
