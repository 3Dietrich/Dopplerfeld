#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// Oszilloskop fuer den Ausgang (@dpa-Feedback, "Scope einbauen ... gross
// genug zum analysieren ... zoombar"). Zeitbereichs-Darstellung von L/R.
// Hat wie LevelMeter keinen eigenen Timer - der Aufrufer (Editor-Timer)
// pusht periodisch ein Rohfenster aus dem Ringpuffer (siehe
// ScopeRingBuffer), die Komponente entscheidet daraus selbst, was
// angezeigt wird (Freeze/Sync/Zoom leben deshalb hier, nicht im Editor).
//
// Zoom laeuft in Samples, nicht in Sekunden - die Komponente kennt die
// Samplerate nicht (die haengt an dem, was gerade geladen ist, und aendert
// sich mit dem Host/Projekt). Der Editor rechnet die Samplerate-abhaengige
// Obergrenze um (siehe setMaxDisplaySampleCount()) und reicht ausserdem
// einen reinen Anzeige-Hinweis fuer die Zeit-Beschriftung durch
// (setSampleRateHint()) - beides bewusst getrennt vom DSP-Pfad.
class ScopeComponent : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    // Untere Zoom-Grenze in Samples, samplerate-unabhaengig - darunter waeren
    // nur noch ein paar Punkte zu sehen, das bringt nichts mehr.
    static constexpr int minDisplaySamples = 128;

    ScopeComponent();

    // Neues Rohfenster von genau captureWindowSampleCount() Samples
    // (chronologisch, aeltestes zuerst). Bei gesetztem Freeze wird das
    // Fenster ignoriert - die zuletzt angezeigten Kurven bleiben stehen, wie
    // am echten Geraet.
    void feed (const float* rawLeft, const float* rawRight);

    void setFrozen (bool shouldFreeze) { frozen = shouldFreeze; }
    bool isFrozen() const { return frozen; }

    // Sync: sucht im Rohfenster einen steigenden Nulldurchgang von L nahe
    // der Fenstermitte und richtet die Anzeige daran aus - der Trigger-
    // Moment landet dadurch in der Mitte des Scopes (@dpa-Vorgabe). Ohne
    // Sync wird einfach die juengste Haelfte des Rohfensters gezeigt.
    void setSyncEnabled (bool shouldSync) { syncEnabled = shouldSync; }
    bool isSyncEnabled() const { return syncEnabled; }

    // Obere Zoom-Grenze in Samples (Editor: DopplerfeldProcessor::
    // scopeMaxDisplaySeconds * Samplerate). Klemmt den aktuellen Zoom mit,
    // falls der gerade darueber liegt (z.B. nach einem Samplerate-Wechsel
    // auf einen kleineren Wert).
    void setMaxDisplaySampleCount (int maxSamples);

    // Reiner Anzeige-Wert fuer die Zeit-Beschriftung in paint() - siehe
    // Klassenkommentar.
    void setSampleRateHint (double sr) { sampleRateHint = sr; }

    int displaySampleCount() const { return displaySamples; }
    int captureWindowSampleCount() const { return displaySamples * 2; }

    void paint (juce::Graphics& g) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;

private:
    // Sucht im Bereich [captureWindowSampleCount()/4 .. *3/4) den
    // steigenden Nulldurchgang von rawLeft, der der exakten Fenstermitte am
    // naechsten liegt. Liefert -1, wenn keiner gefunden wurde (z.B. Stille
    // oder reiner Gleichanteil) - der Aufrufer faellt dann auf die
    // ungesynchte Anzeige zurueck, damit das Bild nicht leer bleibt.
    int findTriggerIndex (const float* rawLeft) const;

    // Setzt eine neue Zoomstufe (Samples), klemmt auf [minDisplaySamples,
    // maxDisplaySamples] und passt die Anzeigepuffer an.
    void setDisplaySampleCount (int newCount);

    bool frozen      = false;
    bool syncEnabled = false;

    int displaySamples    = 4096;          // Default bis der Editor die Samplerate kennt
    int maxDisplaySamples = 1 << 20;        // vorlaeufig grosszuegig, s. setMaxDisplaySampleCount()
    double sampleRateHint = 48000.0;

    std::vector<float> shownLeft, shownRight;

    // Ob gerade eine Sync-Ausrichtung gelungen ist (fuer die Trigger-Linie
    // in paint() - bei fehlgeschlagener Suche wuerde sonst eine Trigger-
    // Linie ueber einem gar nicht ausgerichteten Bild stehen).
    bool lastFrameWasSynced = false;

    static constexpr float amplitudeRange = 1.2f;   // etwas ueber Vollausschlag, Clipping bleibt sichtbar

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeComponent)
};
