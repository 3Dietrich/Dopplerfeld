#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

// Oszilloskop fuer den Ausgang (@dpa-Feedback, "Scope einbauen ... gross
// genug zum analysieren"). Zeitbereichs-Darstellung von L/R, gross genug
// gezeichnet, um darin einzelne Perioden und den ueberschall-typischen
// "Knall" auseinanderzuhalten. Hat wie LevelMeter keinen eigenen Timer - der
// Aufrufer (Editor-Timer) pusht periodisch ein Rohfenster aus dem
// Ringpuffer (siehe ScopeRingBuffer), die Komponente entscheidet daraus
// selbst, was angezeigt wird (Freeze/Sync leben deshalb hier, nicht im
// Editor).
class ScopeComponent : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    // Wie viele Samples am Ende angezeigt werden (die "Zeitbasis" des
    // Scopes). 4096 Samples sind bei 44.1-48 kHz rund 85-93 ms - genug, um
    // bei den hohen, doppler-verschobenen Frequenzen mehrere Perioden UND
    // ein kurzes Chaos-Ereignis (Ueberschall-Knall) gleichzeitig zu sehen.
    static constexpr int displaySamples = 4096;

    // Das Rohfenster, das feed() erwartet: doppelt so lang wie
    // displaySamples, damit bei aktivem Sync ein Trigger irgendwo in der
    // mittleren Haelfte gefunden werden kann UND danach noch genug
    // Samples uebrig sind, um displaySamples/2 nach dem Trigger zu fuellen
    // (das Trigger-Sample landet exakt in der Mitte der Anzeige).
    static constexpr int captureWindowSamples = displaySamples * 2;

    ScopeComponent();

    // Neues Rohfenster von genau captureWindowSamples Samples (chronolo-
    // gisch, aeltestes zuerst). Bei gesetztem Freeze wird das Fenster
    // ignoriert - die zuletzt angezeigten Kurven bleiben stehen, wie am
    // echten Geraet.
    void feed (const float* rawLeft, const float* rawRight);

    void setFrozen (bool shouldFreeze) { frozen = shouldFreeze; }
    bool isFrozen() const { return frozen; }

    // Sync: sucht im Rohfenster einen steigenden Nulldurchgang von L nahe
    // der Fenstermitte und richtet die Anzeige daran aus - der Trigger-
    // Moment landet dadurch in der Mitte des Scopes (@dpa-Vorgabe). Ohne
    // Sync wird einfach die juengste Haelfte des Rohfensters gezeigt.
    void setSyncEnabled (bool shouldSync) { syncEnabled = shouldSync; }
    bool isSyncEnabled() const { return syncEnabled; }

    void paint (juce::Graphics& g) override;

private:
    // Sucht im Bereich [captureWindowSamples/4 .. captureWindowSamples*3/4)
    // den steigenden Nulldurchgang von rawLeft, der der exakten Fenstermitte
    // am naechsten liegt. Liefert -1, wenn keiner gefunden wurde (z.B.
    // Stille oder reiner Gleichanteil) - der Aufrufer faellt dann auf die
    // ungesynchte Anzeige zurueck, damit das Bild nicht leer bleibt.
    static int findTriggerIndex (const float* rawLeft);

    bool frozen       = false;
    bool syncEnabled  = false;

    std::array<float, displaySamples> shownLeft {};
    std::array<float, displaySamples> shownRight {};

    // Ob gerade eine Sync-Ausrichtung gelungen ist (fuer die Trigger-Linie
    // in paint() - bei fehlgeschlagener Suche wuerde sonst eine Trigger-
    // Linie ueber einem gar nicht ausgerichteten Bild stehen).
    bool lastFrameWasSynced = false;

    static constexpr float amplitudeRange = 1.2f;   // etwas ueber Vollausschlag, Clipping bleibt sichtbar

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeComponent)
};
