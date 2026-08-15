#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Listener.h"
#include "Medium.h"
#include "PropagationPath.h"
#include "SourceSignalBuffer.h"
#include "SourceTrajectory.h"
#include "Vec3.h"

#include "Sources/SoundSource.h"

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
class DopplerEngine
{
public:
    // Rasterrate der Trajektorie (Plan 2.12). Auch die Untergrenze der
    // Scan-Schrittweite im Löser hängt daran.
    static constexpr double trajectoryRateHz = 1000.0;

    void prepare (double sampleRate, int maxBlock, double maxFieldMetres);
    void reset();

    // Zeigertausch, kein Besitz - die Lebensdauer verwaltet der Aufrufer.
    void setSource (SoundSource* src) { source = src; }
    SoundSource* getSource() const { return source; }

    void setFieldMetres (double metres) { fieldMetres = metres; }
    double getFieldMetres() const { return fieldMetres; }

    // Ziel der Quellbewegung. TODO H13: hier gehört der MotionSmoother aus
    // Plan 3.8 dazwischen; in H5 geht das Ziel direkt in die Trajektorie.
    void setSourceTarget (Vec3 posMetres) { sourceTarget = posMetres; }
    Vec3 getSourceTarget() const { return sourceTarget; }

    void setListener (const ListenerState& l) { listener = l; }
    const ListenerState& getListener() const { return listener; }

    // Unstetiger Sprung: die Vorgeschichte der Trajektorie wird konstant
    // überschrieben, damit der neue Ort sofort klingt statt erst nach der
    // Laufzeit. Der Crossfade darüber ist H6, hier passiert nur die
    // Trajektorienseite plus das nötige Zurücksetzen der Pfade.
    void jumpSourceTo (Vec3 posMetres);

    void setBoomLimitDb (double dB);
    void setAirAbsorptionAmount (double amount01);

    // Der Mono-Strom der Quelle kommt von außen herein. TODO H6/H13: hier
    // rendert später der DualPathCrossfader<SoundSourceHolder> über
    // SoundSource::renderMono; in H5 ist der Aufrufer dafür zuständig, damit
    // die Quellen-Instanziierung nicht in dieses Häppchen hineinragt.
    // sourceMono darf nullptr sein, dann wird Stille eingespeist.
    void process (juce::AudioBuffer<float>& stereoOut,
                  const float*              sourceMono,
                  const MediumState&        medium);

    // TODO H11: hier dockt fillSnapshot (FieldSnapshot&) an - dezimierte
    // Trajektorienpunkte, Wellenfront-Emissionszeiten, Ohrgeometrie und pro
    // Pfad Verzögerung/Amplitude/M_r (Plan 3.12). Die Pfad-Getter unten
    // liefern den Pfadanteil davon bereits lock-frei.
    int numPaths() const { return (int) paths.size(); }
    const PropagationPath& getPath (int index) const { return paths[(size_t) index]; }

    // Sekunden seit prepare()/reset(), gemeinsame Zeitbasis von Signalpuffer,
    // Trajektorie und Löser.
    double currentTime() const { return (double) sampleClock / sr; }

private:
    void pushTrajectory (double blockStart, double blockEnd);

    SourceTrajectory             trajectory;
    SourceSignalBuffer           signal;
    std::vector<PropagationPath> paths;    // [0] = links direkt, [1] = rechts direkt
    std::vector<int>             pathEar;  // welcher Pfad auf welchen Kanal

    SoundSource* source = nullptr;

    ListenerState listener;
    ListenerState prevListener;

    Vec3 sourceTarget { 0.0, 0.0, 0.0 };
    Vec3 prevTarget   { 0.0, 0.0, 0.0 };

    double fieldMetres       = 100.0;
    double maxHistorySeconds = 1.0;

    double sr       = 0.0;
    int    maxBlock = 0;

    // Fortlaufender Sample-Zähler, nie gewrappt - deckungsgleich mit
    // SourceSignalBuffer::writePosition(), damit timeToIndex() und die
    // Zeitachse des Lösers dieselbe Null haben.
    std::int64_t sampleClock = 0;

    // Nächster zu schreibender Trajektorien-Stützpunkt als ganzzahliger
    // Rasterindex, damit die Rasterzeiten nicht wegdriften.
    std::int64_t nextTrajIndex = 0;

    std::vector<float> silence;   // Ersatz, wenn kein Quellsignal anliegt
};
