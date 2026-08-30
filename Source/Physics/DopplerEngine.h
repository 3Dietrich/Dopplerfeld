#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Listener.h"
#include "Medium.h"
#include "PropagationPath.h"
#include "SourceSignalBuffer.h"
#include "SourceTrajectory.h"
#include "Vec3.h"
#include <array>

#include "Reverb/TapBus.h"
#include "Sources/SoundSource.h"
#include "Util/Crossfader.h"
#include "Util/FieldSnapshot.h"

#include <atomic>
#include <cstdint>
#include <vector>

// Klammert Quelle, geteilte Puffer und Pfadliste (Plan 3.6).
//
// Das ist die Grenzschicht zur JUCE-Welt: juce::AudioBuffer taucht hier auf,
// darunter (PropagationPath, Listener, PathTransform, Solver, Puffer) bleibt
// alles JUCE-frei und damit offline prüfbar.
//
// paths ist bewusst ein Vector und keine zwei Member. Phase 1 hat genau zwei
// Einträge (linkes/rechtes Ohr, beide mit Identity-Transform); Bodenreflexion
// und Wände aus Phase 2 sind zusätzliche Einträge mit anderem PathTransform,
// sonst ändert sich nichts.
//
// Geometriesprünge (Positionssprung, Feldgrößenwechsel) laufen über einen
// DualPathCrossfader<PathSet>: zwei komplette Sätze aus Trajektorie und
// Pfaden, die BEIDE denselben SourceSignalBuffer lesen. Genau dafür sind
// Trajektorie und Signal getrennte Klassen (Plan 3.2/3.7) - der Fade kostet
// kurzzeitig doppelte Löserlast, aber keinen zweiten Signalpuffer.
class DopplerEngine
{
public:
    // Rasterrate der Trajektorie (Plan 2.12). Auch die Untergrenze der
    // Scan-Schrittweite im Löser hängt daran.
    static constexpr double trajectoryRateHz = 1000.0;

    void prepare (double sampleRate, int maxBlock, double maxFieldMetres);
    void reset();

    // Zeigertausch, kein Besitz - die Lebensdauer verwaltet der Aufrufer.
    // Der Quellwechsel-Crossfade sitzt nicht hier, sondern im
    // SoundSourceHolder (Plan 3.7), der selbst eine SoundSource ist.
    void setSource (SoundSource* src) { source = src; }
    SoundSource* getSource() const { return source; }

    // Löst bei tatsächlicher Änderung einen Geometrie-Crossfade aus (60 ms,
    // FadeReason::FieldSize). Begründung für die Umsetzung ohne zweite
    // DopplerEngine steht in der .cpp bei applyArmedFieldChange().
    void setFieldMetres (double metres);
    double getFieldMetres() const { return fieldMetres; }

    // Bereits GEGLÄTTETE Quellposition (Plan 3.8): der MotionSmoother sitzt
    // im Processor davor und tickt auf trajectoryRateHz, hier kommt nur noch
    // sein Ergebnis an. Setzt nur die Zielposition, ohne die Bahn zu
    // beschreiben - dafür ist pushSourceTick() da.
    void setSourceTarget (Vec3 posMetres) { sourceTarget = posMetres; }
    Vec3 getSourceTarget() const { return sourceTarget; }

    // Zeit des nächsten noch zu schreibenden Bahnpunktes. Der Processor tickt
    // seinen Glätter, solange diese Zeit im laufenden Teilblock liegt, und
    // reicht jedes Ergebnis mit pushSourceTick() herein. Damit gibt es genau
    // EINEN Zähler für das Raster - der Glätter kann nicht gegen die Bahn
    // wegdriften.
    double nextTrajectoryTime() const { return (double) nextTrajIndex / trajectoryRateHz; }

    // Ein einzelner Bahnpunkt, genau auf nextTrajectoryTime().
    //
    // Der Glätter im Processor tickt auf derselben Rate, auf der die Bahn
    // gespeichert wird. Seine Ergebnisse gehören deshalb unverändert ins
    // Raster - jedes Umrechnen dazwischen (etwa: Position am Teilblockende
    // merken und über die Teilblockspanne linear verteilen) macht aus einer
    // gleichförmigen Bewegung eine zackige: die Zahl der Glätter-Ticks je
    // Teilblock schwankt (bei 128 Samples und 48 kHz zwischen zwei und drei),
    // die Teilblockspanne ist aber immer gleich lang. Gemessen wurden dabei
    // 75 bis 250 m/s auf einer Bahn, die exakt 200 m/s laufen sollte.
    void pushSourceTick (Vec3 posMetres);

    // Anfang eines Teilblocks: fälliger Geometriewechsel und angemeldete
    // Feldgröße werden übernommen, BEVOR die ersten Bahnpunkte des Teilblocks
    // geschrieben werden. Idempotent - process() ruft dasselbe noch einmal,
    // damit ein Aufrufer, der nur process() kennt, nichts verpasst.
    void beginChunk();

    void setListener (const ListenerState& l) { listener = l; }
    const ListenerState& getListener() const { return listener; }

    // Unstetiger Sprung. Der zweite Geometriesatz wird an der neuen Stelle
    // mit konstanter Vorgeschichte befüllt (klingt also ohne Anlaufzeit) und
    // gegen den alten überblendet; die Dauer kommt aus computeFadeSamples mit
    // FadeReason::SourcePosition und der Sprungweite.
    void jumpSourceTo (Vec3 posMetres);

    // Wie jumpSourceTo(), aber der neue Geometriesatz bekommt eine
    // gleichförmig BEWEGTE Vorgeschichte statt einer ruhenden. Damit fängt ein
    // Vorbeiflug an, als sei die Quelle schon immer geflogen: kein Positions-
    // und kein Geschwindigkeitssprung, den der Löser als Ereignis auffassen
    // müsste.
    //
    // Wie weit die Gerade zurückreicht, rechnet die Engine selbst aus - sie
    // kennt als Einzige die Pufferlänge und damit die längste Laufzeit, die
    // noch abgedeckt ist (siehe SourceTrajectory::fillLinear).
    void startLinearMotion (Vec3 posMetres, Vec3 velocity);

    // Schnitt: die Quelle steht ab sofort an der neuen Stelle, ohne dass ein
    // zweiter Geometriesatz mitlaeuft und ohne dass irgendetwas geblendet
    // wird. Gedacht fuer Positionsaenderungen, die KEINE Bewegung sind - ein
    // geladener Zustand, der Rundenwechsel einer Wiedergabe. Der Aufrufer
    // muss den Ausgang um den Schnitt herum leise machen, sonst knackt es;
    // Begruendung und Abgrenzung zu jumpSourceTo() stehen in der .cpp.
    // preVelocity legt die Vorgeschichte fest, mit der der neue Ort aufgesetzt
    // wird: ruhend (Default) oder gleichfoermig bewegt. Letzteres braucht der
    // Vorbeiflug - eine Quelle, die schon immer geflogen ist, klingt vom
    // ersten Sample an mit voller Fahrt, statt erst anzulaufen.
    void cutTo (Vec3 posMetres, Vec3 preVelocity = Vec3{});

    // Loescht das gespeicherte Quellsignal. Gedacht fuer den Schnitt auf einen
    // GELADENEN Zustand: dort gehoert die Vorgeschichte im Puffer zu einem
    // anderen Preset, mit eigenem Pegel und eigenem Klang, und wuerde nach dem
    // Schnitt eine Laufzeit lang durch die neue Verstaerkungskette laufen.
    // Danach ist es still, bis der Schall die neue Strecke einmal
    // zurueckgelegt hat - dieselbe Stille wie bei einem frisch geladenen
    // Preset.
    void clearSignalHistory() { signal.clearHistory(); }

    void setBoomLimitDb (double dB);
    void setAirAbsorptionAmount (double amount01);

    // Entfernungsabhängigkeit der Amplitude, siehe
    // PropagationPath::setDistanceCurve(). 0 = physikalisch korrektes 1/R.
    void setDistanceCurve (double curve);

    // Druckwellen-/N-Wellen-Schicht, siehe PropagationPath::setNWave().
    // edge01 ist die Schaerfe der Stossfronten, 0,5 = Mitte.
    // Sperrzeit nach einem Knall, in Sekunden - siehe
    // PropagationPath::setBoomHoldSeconds.
    void setBoomHold (double seconds);

    void setNWave (bool shouldBeEnabled, double sizeMetres, double gainLinear,
                   double edge01, double pressure);


    // Pegel der zusaetzlichen Hoerwege, siehe
    // PropagationPath::setExtraPathGain().
    void setExtraPathGain (double gainLinear);

    // Absenkung des uebrigen Schalls waehrend einer Stossfront, siehe
    // PropagationPath::setShockDuck().
    void setShockDuck (double amount01, double rangeMetres);

    void setJumpBoom (double amount01);

    // Laenge des Startknalls in Metern, siehe PropagationPath::setJumpSize().
    void setJumpSize (double metres);

    // Legt die Sprungmarke auf JETZT, mit der Hoehe des
    // Geschwindigkeitssprungs in m/s (siehe PropagationPath::setJumpMarker).
    //
    // Gerufen wird sie vom Aufrufer, der die Sprunghoehe kennt - beim
    // Knall-Start ist das der volle Flugwert, weil die Quelle dort aus dem
    // Stand auf Fahrt geht. jumpSourceTo() selbst kann das nicht wissen: ihm
    // wird nur die neue Position uebergeben, nicht das Tempo danach.
    void markSourceJump (double speedStepMps);

    // Mindestdauer des Ausklangs an der Kaustik, siehe

    // Anteil des gewoehnlichen Pegel-Pannings, 0..1 (siehe
    // PropagationPath::setPanning). Wird pro Block an die Pfade gereicht, weil
    // er von der Kopfausrichtung abhaengt und nicht im Pfad gespeichert bleiben
    // darf.
    void   setPanoramaAmount (double amount01) { panoramaAmount01 = amount01; }
    double panoramaAmount() const { return panoramaAmount01; }

    // Reflektierende Flächen. Index 0 ist der Direktschall (immer an, keine
    // Spiegelung), Index 1 der Boden, danach die Wände.
    //
    // Die Spiegelpfade existieren immer (sie werden in prepare() mit angelegt),
    // gerechnet werden sie nur im eingeschalteten Zustand - ein Umschalten
    // allokiert also nichts und kostet ausgeschaltet auch keine Löserzeit. Beim
    // Wiedereinschalten liegt ihr letzter Löserzeitpunkt in der Vergangenheit;
    // PropagationPath sät sich daraufhin von selbst neu (Lückenerkennung in
    // process()), und die Zweige rampen über den Anti-Klick-Envelope ein statt
    // zu knacken.
    static constexpr int maxWalls    = 2;
    static constexpr int surfaceCount = 2 + maxWalls;   // direkt, Boden, Wände

    void setGroundReflectionEnabled (bool shouldBeEnabled);
    bool isGroundReflectionEnabled() const { return surfaces[1].enabled; }

    // Höhendämpfung der Reflexion, 0..1. Wirkt nur auf die Spiegelpfade.
    void setGroundDampingAmount (double amount01);

    // Eine Wand als unendliche Ebene: Fußpunkt, Richtung der Wandlinie in der
    // Draufsicht und Neigung um genau diese Linie (siehe
    // wallMirrorTransform()). Anders als der Boden darf sie sich bewegen -
    // deshalb wird ihre Abbildung nicht einmalig gesetzt, sondern vor jedem
    // Block aus diesen Werten gebildet. Der Aufrufer glättet sie, damit ein
    // gezogener Regler den Spiegelempfänger nicht springen lässt.
    // gainLinear: reiner Amplitudenfaktor der Reflexion, unabhaengig von der
    // Hoehendaempfung (damping01) - landet in surfaces[index].transform.gain
    // und wird von composeTransforms() bei Mehrfachreflexion automatisch mit
    // verkettet (Plan siehe recipeTransform()).
    void setWall (int index, bool enabled, Vec3 anchorMetres,
                  double azimuthRad, double tiltRad, double damping01, double gainLinear);

    // Pegel der Bodenreflexion, linear. Eigener Regler, weil ein Tiefpass, der
    // bis 100 Hz zumacht, der Reflexion fast die ganze Energie nimmt - ohne
    // Nachregeln waere sie dann weg statt dumpf.
    void setGroundGain (double gainLinear);

    // Eigener Wackler je echtem Klon (@dpa 20260820: "die (echten) clone haben
    // doch hoffentlich auch ihre eigenen Jitterkanaele?"). Der Versatz kommt
    // fertig aus dem Processor, wo er auf der Bewegungsrate tickt - hier wird er
    // nur zum festen Klon-Versatz addiert.
    void setCloneJitterOffset (int index, Vec3 offsetMetres)
    {
        if (index >= 0 && index < maxRealClones)
            cloneJitterOffset[(size_t) index] = offsetMetres;
    }

    // Zwei Propeller an Fluegeln (@dpa 20260823: "(2) Propeller an Fluegeln
    // (die in n meter auseinander, immer flach in der Richtung des fluges
    // sind..)").
    //
    // Sie sind zwei zusaetzliche Schallwege mit eigenem Versatz - dieselbe
    // Maschinerie wie bei den Klonen, aber mit einem Versatz, der NICHT im
    // Raum feststeht: er sitzt quer zur Flugrichtung und waagerecht, dreht
    // sich also mit der Bahn mit. Genau das meint "flach in der Richtung des
    // Fluges": die Fluegel stehen quer zum Flug, nicht quer zur Weltachse.
    //
    // gainLinear ist ihr Pegel; sie kommen zum Rumpfschall hinzu, statt ihn zu
    // ersetzen. Wer nur die Propeller hoeren will, dreht den Rumpf ueber die
    // Motor-Pegel herunter.
    void setPropellers (bool enabled, double gainLinear);

    // Der fertige Versatz je Propeller, in Metern, gegenueber der Quelle.
    // Kommt aus dem Processor, wo die Flugrichtung ohnehin bekannt ist - die
    // Engine kennt nur Bahnpunkte, nicht deren Ableitung.
    void setPropellerOffset (int index, Vec3 offsetMetres)
    {
        if (index >= 0 && index < propellerCount)
            propellerOffset[(size_t) index] = offsetMetres;
    }

    // Mehrfachreflexionen: genau EINE zusätzliche Generation, also Wege der
    // Form Quelle -> Fläche X -> Fläche Y -> Ohr mit X != Y. Mehr nicht, und
    // ausdrücklich keine Rekursion.
    //
    // Warum das nicht aufschwingen kann (das war der Grund, aus dem der Punkt
    // ursprünglich zurückgestellt wurde): das hier ist die
    // Spiegelquellen-Methode, kein Rückkopplungsnetz. Jeder Weg ist ein
    // eigener PropagationPath, der den geteilten Quellsignalpuffer LIEST und
    // additiv auf den Ausgang schreibt. Kein Pfad schreibt je in den Puffer
    // zurück, kein Ausgang ist irgendwo Eingang - es gibt schlicht keine
    // Schleife, die eine Verstärkung aufsammeln könnte. Der Ausgang ist eine
    // endliche Summe endlich vieler beschränkter Terme (jeder einzelne durch
    // 1/(R_min·eps) begrenzt, wie jeder Pfad im Projekt).
    //
    // bounceGain ist trotzdem da und liegt unter 1: die Dämpfung an der Fläche
    // ist ein Tiefpass mit Gleichstromverstärkung 1, nimmt also nur Höhen und
    // keinen Pegel. Ohne einen ausdrücklichen Faktor je Generation wäre eine
    // zweifach reflektierte Welle nur durch den längeren Weg leiser, was für
    // eine reale Fläche zu wenig ist.
    void setSecondOrderEnabled (bool shouldBeEnabled);
    void setBounceGain (double gain01);

    // Zusaetzlicher, unabhaengiger Boost-Faktor obendrauf - anders als
    // bounceGain (s.o.) ausdruecklich ohne Deckelung unter 1: die
    // Generationsgarantie bleibt bei bounceGain, dieser Faktor ist reiner
    // Pegel-Boost (@dpa: Bounce Gain "braucht einen Gain Regler").
    void setBounceGainBoost (double gainLinear);

    // Klone mit voller Löserphysik ("Schrot"-Muster). Ein Klon ist eine zweite
    // Quelle, deren Route um einen kleinen festen Betrag von der echten
    // abweicht.
    //
    // Genau deshalb braucht er weder eine eigene Trajektorie noch einen
    // eigenen Signalpuffer: eine um s verschobene QUELLE ist dasselbe wie ein
    // um -s verschobener EMPFÄNGER, und Empfänger verschieben ist genau das,
    // was PathTransform ohnehin tut. Ein Klon kostet damit exakt ein Pfadpaar -
    // nicht mehr, aber auch nicht weniger, und das ist der Grund für den
    // Regler: die Löserlast wächst linear mit der Klonzahl.
    //
    // Klone laufen nur über den Direktschall. Sie zusätzlich über alle Flächen
    // zu spiegeln wäre dieselbe Rechnung noch einmal mal vier und stünde in
    // keinem Verhältnis zu dem, was man davon hört.
    static constexpr int maxRealClones = 20;

    // gainLinear ist der lineare Faktor aus Params::cloneRealLevel (dB, im
    // Processor via Decibels::decibelsToGain umgerechnet) - KEIN 0..1-Pegel,
    // sondern ein echter Gain, der ueber 1 hinaus darf.
    // zAmount: Anteil der Hoehe an der Streuung, derselbe Regler wie beim
    // Wackler der Quelle (Params::srcJitterZAmount, @dpa 20260826: "Z-Anteil
    // auch bei Feld/Streuung ... vielleicht gegenseitig ferngesteuert/gleich
    // geschaltet?"). 1 = die Klone streuen in der Hoehe genauso weit wie in
    // der Ebene, 0 = sie liegen alle flach auf der Hoehe der Quelle.
    void setRealClones (int count, double spreadMetres, double zAmount, double gainLinear);
    int  realCloneCount() const { return realClones; }

    // ------------------------------------------------------ Abgriffpunkte
    //
    // Ein Abgriffpunkt ist ein Empfangspunkt im Feld, der nicht zu einem Ohr
    // gehoert. Was dort ankommt, laeuft durch einen Hall (TapBus) und wird
    // dem Ausgang als zusaetzliche Signalquelle zugemischt.
    //
    // Der Preis ist genau EIN Loeser je Punkt, nicht zwei: der Punkt hat kein
    // zweites Ohr. Zum Vergleich kosten Direktschall und Boden zusammen schon
    // vier, weil jeder von beiden fuer beide Ohren laeuft. Acht Abgriffpunkte
    // liegen damit in der Groessenordnung dessen, was die zweite
    // Reflexionsordnung ohnehin verbraucht.
    //
    // Der Hall selbst laeuft NICHT noch einmal durch die Physik. Er wird
    // direkt in den Ausgang gemischt, mit einem Vorlauf fuer den Rueckweg zum
    // Hoerer (siehe TapBus::setPredelayMetres). Das ist der Unterschied
    // zwischen bezahlbar und nicht bezahlbar: eine echte Rueckausbreitung
    // braeuchte je Punkt einen zweiten Signalpuffer und ein zweites Pfadpaar.
    static constexpr int maxTaps = 8;

    void setTap (int index, bool enabled, Vec3 posMetres);

    // Hallwerte eines Punktes. Getrennt vom Ort, weil der Ort die Physik
    // betrifft und der Rest nur den Hall dahinter.
    // Ziel der Kette setzen, -1 oder ein spaeterer Index (siehe
    // TapState::chainTo). Alles andere wird verworfen.
    void setTapChain (int index, int target);

    void setTapReverb (int index, int type, double roomMetres, double decaySeconds,
                       double damping01, double phase01, double earlyAmount, double gainLinear,
                       double width, bool predelayEnabled, int echoCount, int seed);

    // Groesster Raum, den ein Punkt verlangt hat und seine Puffer nicht
    // tragen (0 = alles passt). Der Audiothread meldet ihn beim Setzen,
    // gelesen wird er vom Nachrichtenthread.
    double tapRoomShortfall() const;

    // Zu klein bemessene Punkte neu anlegen. Allokiert - nur vom
    // Nachrichtenthread und nur bei angehaltenem Audiothread aufrufen.
    void growTapRoomCapacity();

    // Bemessung der Hallpuffer vor einem prepare() festlegen, damit ein
    // geladener Zustand mit grossen Raeumen sofort richtig anfaengt statt
    // sich ueber growTapRoomCapacity() hochzuarbeiten. Setzt hart: nur so
    // gibt ein Zustand mit kleinen Raeumen den Speicher wieder her.
    void setTapRoomCapacity (double metres) { tapRoomCapacity = reverbparts::capacityFor (metres); }

    bool isTapEnabled (int index) const
    {
        return ! tapsBypassed
               && index >= 0 && index < maxTaps && taps[(size_t) index].enabled;
    }

    // Alle Abgriffpunkte auf einmal stillegen. Ein echter Bypass, kein
    // Pegelregler: die Wege werden uebersprungen und kosten dann auch keine
    // Loeserzeit. Beim Wiedereinschalten saeen sich ihre Loeser von selbst neu
    // und die Zweige rampen ueber den Anti-Klick-Envelope ein.
    void setTapsBypassed (bool shouldBeBypassed) { tapsBypassed = shouldBeBypassed; }
    bool areTapsBypassed() const { return tapsBypassed; }

    // Pegel des Direktschalls, linear. Wirkt NUR auf die Wege ohne Spiegelung
    // (order() == 0), also auf den Direktschall selbst samt Klonen und
    // Propellern - nicht auf Boden, Waende oder Abgriffpunkte.
    //
    // Damit laesst sich hoeren, was der Raum allein macht: dreht man ihn zu,
    // bleiben Reflexionen und Haelle stehen (@dpa 20260829: "wenn alle reverbs
    // aus sind: dieses Signal. Wenn man das runter dreht, dann bleiben nur noch
    // die Reverbs (und Waende..)").
    void   setDirectGain (double gainLinear) { directGain = gainLinear; }
    double getDirectGain() const { return directGain; }

    // Ort des Punktes, wie ihn die Pfadschleife braucht.
    Vec3 tapPosition (int index) const
    {
        return (index >= 0 && index < maxTaps) ? taps[(size_t) index].pos : Vec3{};
    }

    // Alles außer dem Direktschall aus - die minimale sichere Konfiguration.
    void disableAllReflections();

    // Der Mono-Strom der Quelle kommt von außen herein (im Plugin aus dem
    // SoundSourceHolder des Processors); die Engine rendert bewusst keine
    // Quelle selbst, damit der geteilte Signalpuffer genau einen Schreiber
    // hat. sourceMono darf nullptr sein, dann wird Stille eingespeist.
    void process (juce::AudioBuffer<float>& stereoOut,
                  const float*              sourceMono,
                  const MediumState&        medium);

    // Anzeigedaten für den Message-Thread (Plan 3.12). Der Audiothread legt
    // den Snapshot am Blockende in einen von zwei Puffern und tauscht einen
    // atomaren Index; hier wird aus dem veröffentlichten Puffer kopiert.
    // Kein Lock, keine Allokation, keine Rückwirkung auf den Audiothread.
    void fillSnapshot (FieldSnapshot& dest) const;

    // Pfadanteil derselben Daten, lock-frei und einzeln abfragbar, und zwar
    // vom hörbaren (aktiven) Satz - während eines Fades zeigt die Anzeige
    // also die Geometrie, die überwiegend zu hören ist.
    int numPaths() const { return (int) geometry.active().paths.size(); }
    const PropagationPath& getPath (int index) const { return geometry.active().paths[(size_t) index]; }

    bool isCrossfading() const { return geometry.isFading(); }

    // Summe der Löser-Auswertungen über ALLE Pfade beider Geometriesätze -
    // maschinenunabhängiges Lastmaß für Regressionstests (load_check). Die
    // Wanduhr allein taugt dafür nicht: sie schwankt auf einem beschäftigten
    // Rechner um Faktor zwei.
    std::uint64_t solverEvaluations() const;

    // Messung: wie oft ein Hoerweg ueber den Rand des Signalpuffers hinaus
    // gelesen hat und dort eine harte Null bekam - getrennt nach altem und
    // neuem Rand. Siehe SourceSignalBuffer::missesOld/missesNew.
    std::uint64_t signalMissesOld() const { return signal.missesOld.load(); }
    std::uint64_t signalMissesNew() const { return signal.missesNew.load(); }

    // Siehe PropagationPath::loudestSampleDTau(): das dTau, mit dem der
    // lauteste Beitrag ueberhaupt kam, ueber alle Pfade.
    double loudestSampleDTau() const
    {
        double best = 0.0, level = 0.0;

        for (const auto* s : { &geometry.active(), &geometry.pending() })
            for (const auto& p : s->paths)
                if (p.loudestSampleLevel() > level)
                {
                    level = p.loudestSampleLevel();
                    best  = p.loudestSampleDTau();
                }

        return best;
    }

    // Sekunden seit prepare()/reset(), gemeinsame Zeitbasis von Signalpuffer,
    // Trajektorie und Löser.
    double currentTime() const { return (double) sampleClock / sr; }

    // Ende eines Blocks von numSamples, ab jetzt gerechnet. Der Aufrufer darf
    // das NICHT selbst als currentTime() + numSamples/sr zusammensetzen: die
    // beiden Rechenwege unterscheiden sich in der letzten Stelle, und davon
    // hängt ab, ob ein Rasterpunkt noch in diesen Block fällt. Fiel er
    // heraus, sprang die Bahn dort auf Stillstand (gemessen: einzelne
    // Ausreißer auf 207,4 m/s im 200-m/s-Flug, weil die Anzeige über einen
    // eingefrorenen Punkt hinweg interpoliert).
    double blockEndTime (int numSamples) const { return (double) (sampleClock + numSamples) / sr; }

private:
    // Ein kompletter Geometriesatz: eigene Trajektorie, eigene Pfade und
    // damit eigener Löser-, Filter- und Envelope-Zustand. Nicht kopiert wird
    // der Signalpuffer - der liegt in der Engine und wird nur gelesen.
    //
    // Die Ohrgeometrie gehört mit in den Satz: der ausblendende Satz friert
    // seine ein (prevListener == listener, also Ohrgeschwindigkeit 0). Sonst
    // würde ein Feldgrößenwechsel, der ja auch den Hörer verschiebt, den
    // Sprung über die Ohrgeschwindigkeit in den alten Satz zurückholen.
    // Eine reflektierende Fläche. Index 0 (Direktschall) trägt keine
    // Spiegelung und ist immer aktiv; alle anderen sind es nur, wenn der
    // Benutzer sie einschaltet.
    struct Surface
    {
        PathTransform transform;      // Identität beim Direktschall
        bool          enabled = true;
        double        damping = 0.0;  // Höhenverlust an der Fläche, 0..1

        // Eckfrequenz bei voller Dämpfung. Modellkonstante je Flächenart, kein
        // Regler: der Regler ist die Stärke.
        double        dampFcHz = 800.0;

        // Nur bei Wänden gesetzt (setWall()) - Normale der Ebene, fürs
        // Seitenerkennen (wallSideGain()). Beim Boden/Direktschall bleibt sie
        // {0,0,0} und wird nicht benutzt.
        Vec3 normal;
    };

    // Ein Ausbreitungsweg als Rezept: über welche Flächen er läuft, in der
    // Reihenfolge, in der der Schall sie trifft.
    //
    //   first < 0            - Direktschall
    //   first >= 0, second<0 - eine Reflexion
    //   beide >= 0           - zwei Reflexionen (first zuerst getroffen)
    //
    // Die Abbildung des EMPFÄNGERS läuft dabei genau andersherum:
    // |L - σ_second(σ_first(M))| = |σ_first(σ_second(L)) - M|, weil jede
    // Spiegelung eine Isometrie ist. Deshalb ist die äußere Abbildung σ_first.
    struct PathRecipe
    {
        int ear    = 0;
        int first  = -1;
        int second = -1;

        // >= 0: dieser Weg gehört einem Klon, nicht der echten Quelle.
        int clone  = -1;

        // 0 oder 1: dieser Weg gehoert einem der beiden Propeller an den
        // Fluegeln, nicht dem Rumpf (siehe setPropellers). -1 sonst.
        int prop   = -1;

        // >= 0: dieser Weg endet nicht an einem Ohr, sondern an einem
        // Abgriffpunkt (siehe setTap). Er traegt dann nichts direkt zum
        // Ausgang bei - sein Signal geht in den zugehoerigen TapBus.
        int tap    = -1;

        int order() const { return (first < 0 ? 0 : (second < 0 ? 1 : 2)); }
    };

    // Fester Versatz eines Klons in Metern. Deterministisch aus dem Index
    // gebildet, nicht gewürfelt: derselbe Regelweg muss zweimal dasselbe
    // ergeben, sonst klingt jedes Laden anders und kein Vergleich zweier
    // Durchläufe ist möglich.
    static Vec3 cloneOffset (int index, double spreadMetres, double zAmount);

    struct PathSet
    {
        // Die Länge des Rezeptvektors bestimmt die Pfadanzahl.
        void prepare (double sampleRate, int maxBlockSize, size_t pathCount,
                      double trajRateHz, double maxSeconds);
        void reset (Vec3 pos, double time, const ListenerState& l);

        // Ein Trajektorien-Stützpunkt; merkt sich die Position, damit ein
        // eingefrorener Satz sie ohne Zutun der Engine weiterschreiben kann.
        void push (Vec3 pos, double t);

        // Renderer-Vertrag des DualPathCrossfader. Liest über die Zeiger, die
        // die Engine vor jedem Block setzt.
        void renderInto (juce::AudioBuffer<float>& dest, int numSamples);

        SourceTrajectory             trajectory;
        std::vector<PropagationPath> paths;

        Vec3 lastPos { 0.0, 0.0, 0.0 };

        ListenerState listener;
        ListenerState prevListener;

        // Blockkontext, von der Engine vor jedem process() gesetzt.
        const SourceSignalBuffer*      signal  = nullptr;
        const MediumState*             medium  = nullptr;
        const std::vector<PathRecipe>* recipes = nullptr;
        const DopplerEngine*           engine  = nullptr;
        double                         blockStartTime = 0.0;
        double                         sr             = 0.0;
    };

    // Ist dieser Weg gerade zu rechnen, und wie sieht er aus? Beides hängt am
    // Zustand der beteiligten Flächen und wird deshalb hier und nicht im
    // PathSet beantwortet (die Flächen gehören der Engine, nicht dem Satz).
    bool          recipeEnabled (const PathRecipe& r) const;
    PathTransform recipeTransform (const PathRecipe& r) const;
    double        recipeDamping (const PathRecipe& r) const;
    double        recipeDampFcHz (const PathRecipe& r) const;

    // Wie stark eine einfache Wandreflexion (order()==1) gerade physikalisch
    // ueberhaupt Sinn ergibt: 1, wenn Quelle und Hoerer auf derselben Seite
    // der Wandebene stehen (die reale Wand wirft den Schall in denselben
    // Raum zurueck), weich gegen 0, wenn sie auf verschiedenen Seiten stehen
    // (die "Reflexion" waere ein Durchschein durch die feste Wand, das gibt
    // es hier nicht). wallIndex ist 0/1 (nicht der Surface-Index).
    // Stetiges 0..1-Mass dafuer, ob Quelle und Empfaenger auf derselben Seite
    // der Wandebene stehen. Der Empfaenger ist meistens der Hoerer, bei den
    // Wegen zu einem Abgriffpunkt aber dieser Punkt.
    double wallSideGain (int wallIndex, Vec3 receiverPos) const;

    // Sicherheitsnetz: füllt die Bahn mit der zuletzt geschriebenen Position
    // auf, falls der Aufrufer für diesen Block weniger Punkte geliefert hat
    // als der Löser braucht. Im Normalbetrieb passiert hier nichts.
    void fillTrajectoryUpTo (double untilTime);

    // Schreibt den Anzeige-Snapshot in den gerade nicht veröffentlichten
    // Puffer und tauscht ihn ein (Audiothread, am Blockende).
    void publishSnapshot (const MediumState& medium);

    // Gemeinsamer Kern von Positionssprung und Feldgrößenwechsel.
    void startGeometrySwitch (Vec3 newPos, Vec3 preVelocity, int fadeSamples);
    void configurePendingSet (Vec3 newPos, Vec3 preVelocity);

    // Setzt einen beliebigen Geometriesatz an newPos neu auf. Der
    // Ueberblendweg braucht nur pending(), der Schnitt (cutTo) beide.
    void configureSet (PathSet& s, Vec3 newPos, Vec3 preVelocity);
    void applyArmedFieldChange();

    int fadeSamplesFor (FadeReason reason, double positionDeltaMetres) const;

    // Zustand eines Abgriffpunkts, soweit er die Physik betrifft.
    struct TapState
    {
        bool enabled = false;
        Vec3 pos;

        // Nur fuer die Feldanzeige gemerkt: der Hall selbst haelt seine
        // Raumgroesse im TapBus, dort aber schon auf die Kapazitaet geklemmt.
        double roomMetres = 30.0;

        // Ob der Rueckweg zum Hoerer als Laufzeit abgebildet wird. Aus
        // klingt der Hall, als saesse er am Ohr - manchmal gewollt, meist
        // nicht.
        bool predelay = true;

        // Ziel der Kette (siehe Params::TapPart::chain): -1 = keines, sonst
        // der Index eines SPAETEREN Punktes. Wer ein Ziel hat, geht nicht mehr
        // auf die Ohren, sondern in dessen Eingang; wer Ziel ist, hoert nicht
        // mehr das Feld, sondern nur noch seinen Vorgaenger.
        int chainTo = -1;
    };

    // Wo der Hall eines Punktes im Stereobild sitzt, aus seinem Ort gegenueber
    // dem Hoerer: die Richtung zum Punkt auf die Rechts-Achse des Kopfes
    // projiziert. Ein Punkt genau vor dem Hoerer landet damit in der Mitte,
    // einer seitlich davon aussen - dieselbe Regel wie beim Ohr-Panorama, und
    // sie haengt am selben Regler (panoramaAmount).
    double tapPanorama (int index) const;

    TapState taps[maxTaps];
    TapBus   tapBus[maxTaps];

    // Arbeitsflaechen der Kette (siehe TapState::chainTo). chainStereo nimmt
    // den Hall einer Stufe auf, chainInput haelt je Ziel dessen Summe - das
    // ist der Eingang der naechsten Stufe. Mono, weil eine Hallbauart einen
    // Punkt hoert, keinen Stereopegel; die Breite entsteht ohnehin in der
    // letzten Stufe neu.
    //
    // Je Ziel ein eigener Abschnitt, nicht eine gemeinsame Flaeche: es koennen
    // mehrere Ketten gleichzeitig laufen (1 in 3 und 2 in 4), und die zweite
    // wuerde der ersten sonst ihr Signal ueberschreiben, bevor deren Ziel an
    // der Reihe ist.
    //
    // Bemessen in prepare(), im Renderpfad wird daran nichts allokiert.
    std::vector<float> chainStereoL, chainStereoR, chainInput;
    bool chainHasInput[maxTaps] {};

    // Renderziel der Pfade: zwei Ohrkanaele plus je einer je Abgriffpunkt.
    //
    // Die Abgriffpunkte als zusaetzliche KANAELE zu fuehren statt als eigene
    // Puffer ist der Grund, warum sie beim Geometrie-Crossfade von selbst
    // mitgefadet werden - der Crossfader mischt schlicht alle Kanaele, die er
    // vorfindet. Ein eigener Puffer neben dem Crossfader wuerde bei jedem
    // Feldgroessenwechsel springen.
    juce::AudioBuffer<float> renderBuffer;

    double directGain   = 1.0;
    bool   tapsBypassed = false;

    DualPathCrossfader<PathSet> geometry;

    SourceSignalBuffer      signal;     // geteilt, genau ein Schreiber
    std::vector<PathRecipe> recipes;    // parallel zu PathSet::paths

    Surface surfaces[surfaceCount];

    bool   secondOrderOn      = false;
    double bounceGain         = 0.6;
    double bounceGainBoost    = 1.0;

    int    realClones  = 0;
    double cloneSpread = 3.0;

    // Anteil der Hoehe an der Klon-Streuung, siehe setRealClones(). Startwert
    // wie der Regler selbst (Params.cpp: Default 1).
    double cloneZAmount = 1.0;

    // Gain der Klone (linear), siehe Params::cloneRealLevel - 0dB = 1.0.
    double cloneRealLevel = 1.0;

    // Wackler je Klon, siehe setCloneJitterOffset().
    std::array<Vec3, (size_t) maxRealClones> cloneJitterOffset {};

    // Propellerpaar, siehe setPropellers(). Zwei Stueck - "(2) Propeller an
    // Fluegeln", nicht beliebig viele.
    static constexpr int propellerCount = 2;

    bool   propellersOn   = false;
    double propellerGain  = 1.0;
    std::array<Vec3, (size_t) propellerCount> propellerOffset {};

    SoundSource* source = nullptr;

    ListenerState listener;

    Vec3 sourceTarget { 0.0, 0.0, 0.0 };

    // Warteschlange der Länge eins auf Aufruferseite: der Crossfader merkt
    // sich nur die Dauer, die Zielgeometrie steht hier.
    Vec3 queuedJumpPos { 0.0, 0.0, 0.0 };
    Vec3 queuedJumpVel { 0.0, 0.0, 0.0 };

    double fieldMetres       = 100.0;
    bool   fieldChangeArmed  = false;
    double maxHistorySeconds = 1.0;

    // Pfadeinstellungen, damit ein frisch konfigurierter Satz sie nicht
    // verliert (PropagationPath::reset() lässt sie zwar stehen, aber beide
    // Sätze müssen von Anfang an gleich eingestellt sein).
    double boomLimitDb    = 30.0;
    double airAbsorbAmount = 1.0;
    double distanceCurve   = 0.0;

    double panoramaAmount01 = 0.0;

    bool   nWaveOn    = false;
    double nWaveSizeM = 15.0;

    // Regelbarer Knall-Pegel, linear (siehe Params::nWaveGainDb).
    double nWaveGain  = 1.0;

    // Schaerfe der Stossfronten (siehe Params::nWaveEdge).
    double nWaveEdge  = 0.5;

    // Staerke der Nulllinien-Auslenkung (siehe Params::nWavePressure).
    double nWavePressure = 1.0;

    double extraPathGain = 1.0;

    // Siehe setReverseGain() / setShockDuck().
    double shockDuckAmount = 0.0;
    double shockDuckRange  = 0.0;
    double jumpBoom        = 0.0;
    double jumpSizeM       = 1.5;

    // Eckfrequenz der Bodendämpfung bei voller Stärke. Rund ein Kilohertz ist
    // die Gegend, in der eine streifende Reflexion an Gras/Erde ihre Höhen
    // verliert. Eine Wand ist typischerweise härter und behält mehr davon.
    // Grenzfrequenz bei ganz aufgedrehtem Daempfungsregler (@dpa: "bis zu
    // 100Hz, wirklich richtig dumpf"). Was dazwischen liegt, rechnet
    // PropagationPath::setReflectionDamping aus dem Reglerwert.
    static constexpr double groundDampFcHz = 100.0;
    static constexpr double wallDampFcHz   = 100.0;

    // Lage der Wände, für die Anzeige mitgeführt (die Abbildung selbst steht
    // in surfaces[] und ist daraus nicht mehr ablesbar).
    struct WallGeometry
    {
        Vec3   anchor;
        double azimuthRad = 0.0;
        double tiltRad    = 0.0;
    };

    WallGeometry wallGeometry[maxWalls];

    double sr       = 0.0;
    int    maxBlock = 0;

    // Raumgroesse, auf die die Hallpuffer der Abgriffpunkte bemessen sind.
    // Waechst mit dem, was verlangt wird (growTapRoomCapacity), und bleibt
    // ueber ein prepare() hinweg erhalten - sonst faenge ein Puffergroessen-
    // wechsel des Hosts wieder klein an und muesste sich neu hocharbeiten.
    double tapRoomCapacity = reverbparts::baseCapacityMetres;

    // Sperrzeit nach einem Knall, siehe setBoomHold. Drei Sperren: eine je
    // Ohr und eine fuer die Abgriffpunkte. Je Ohr, weil derselbe Knall beide
    // trifft - eine halbe Millisekunde versetzt, und genau daran hoert man,
    // von wo er kommt. Eine gemeinsame Sperre wuerde das zweite Ohr
    // wegwerfen.
    std::array<PropagationPath::BoomGate, 3> boomGates {};

    // Welche Sperre ein Weg benutzt: sein Ohr, oder die dritte, wenn er an
    // einem Abgriffpunkt endet.
    // const, weil die Render-Schleife die Engine nur lesend kennt: die Sperre
    // selbst ist Zustand des Audiothreads und wird beim Ausloesen fortgezaehlt,
    // nicht hier.
    PropagationPath::BoomGate* boomGateFor (const PathRecipe& r) const
    {
        const size_t index = (size_t) (r.tap >= 0 ? 2 : (r.ear != 0 ? 1 : 0));

        return const_cast<PropagationPath::BoomGate*> (&boomGates[index]);
    }

    // Fortlaufender Sample-Zähler, nie gewrappt - deckungsgleich mit
    // SourceSignalBuffer::writePosition(), damit timeToIndex() und die
    // Zeitachse des Lösers dieselbe Null haben.
    std::int64_t sampleClock = 0;

    // Nächster zu schreibender Trajektorien-Stützpunkt als ganzzahliger
    // Rasterindex, damit die Rasterzeiten nicht wegdriften. Gemeinsam für
    // beide Sätze: sie müssen auf demselben Raster liegen, sonst laufen die
    // beiden Zeitachsen während eines Fades auseinander.
    std::int64_t nextTrajIndex = 0;

    std::vector<float> silence;   // Ersatz, wenn kein Quellsignal anliegt

    // --- Anzeige-Snapshot (Plan 3.12) ---
    //
    // Zwei Puffer plus atomarer Index: der Schreiber füllt immer den, den der
    // Leser gerade nicht liest. Die Generationszählung darüber deckt den
    // einzigen verbleibenden Fall ab, den der Index allein nicht abdeckt -
    // dass der Audiothread während einer laufenden Lesekopie zweimal
    // veröffentlicht und dabei den gelesenen Puffer wieder einholt. Ungerade
    // Generation heißt "Schreiben läuft"; fillSnapshot verwirft die Kopie
    // dann und probiert es erneut, statt den Audiothread warten zu lassen.
    static constexpr double snapshotIntervalSeconds = 1.0 / 60.0;

    FieldSnapshot              snapshotBuffers[2];
    std::atomic<int>           snapshotIndex { 0 };
    std::atomic<unsigned int>  snapshotGeneration { 0 };
    int                        snapshotWriteSlot = 1;
    double                     lastSnapshotTime  = -1.0;
};
