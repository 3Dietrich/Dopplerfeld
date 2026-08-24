#include "MotionPanel.h"

void MotionPanel::setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                              const juce::String& paramID, const juce::String& labelText,
                              Tooltips::Key tooltipKey)
{
    knob.tooltipKey = tooltipKey;
    const auto tooltip = Tooltips::text (tooltipKey);

    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knob.slider.setTooltip (tooltip);
    addAndMakeVisible (knob.slider);

    knob.label.setText (labelText, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setTooltip (tooltip);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (apvts, paramID, knob.slider);
}

void MotionPanel::setSpeedUnit (FieldComponent::SpeedUnit unit, double speedOfSoundMps)
{
    // Die Regler, die ein Tempo einstellen. Der gespeicherte Wert bleibt in
    // m/s - umgerechnet wird nur, was im Textfeld steht, und was jemand dort
    // eintippt, wird zurueckgerechnet.
    // Tempo und Beschleunigung teilen sich die Umrechnung: eine Beschleunigung
    // ist ein Tempo pro Sekunde, es kommt nur "/s" an die Einheit. Slew Amax
    // gehoert deshalb mit dazu - er stellt das Geschwindigkeitsverhalten ein
    // und war ohne Einheit genauso wenig lesbar wie die drei anderen.
    struct Target { Knob* knob; bool perSecond; };

    const Target targets[] =
    {
        { &flySpeedKnob,       false },
        { &globalMaxSpeedKnob, false },
        { &slewVmaxKnob,       false },
        { &slewAmaxKnob,       true  },

        // Der eigene Tempo-Deckel des Wacklers ist genauso ein Tempo wie die
        // anderen drei (@dpa 20260824: "Jit Max ist ja eine 'Speed'angabe,
        // bitte gib ihn via gesetzter 'Tempo Einheit' an").
        { &srcJitterMaxSpeedKnob, false },
    };

    for (const auto& target : targets)
    {
        const auto base = unit == FieldComponent::SpeedUnit::KmH  ? juce::String (" km/h")
                        : unit == FieldComponent::SpeedUnit::Mach ? juce::String (" Mach")
                                                                  : juce::String (" m/s");

        // m/s wird zu m/s^2, km/h zu (km/h)/s, Mach zu Mach/s.
        const juce::String suffix = ! target.perSecond ? base
                                  : unit == FieldComponent::SpeedUnit::Ms
                                        ? juce::String::fromUTF8 (" m/s\xc2\xb2")
                                        : (unit == FieldComponent::SpeedUnit::KmH
                                               ? juce::String (" (km/h)/s")
                                               : juce::String (" Mach/s"));

        auto& slider = target.knob->slider;

        slider.displayText = [unit, speedOfSoundMps, suffix] (double raw)
        {
            // Stellenzahl nach der gemeinsamen Regel, gebildet aus dem WERT, der
            // dasteht - nicht aus dem gespeicherten m/s-Wert. Sonst haette
            // dieselbe Einstellung je nach Einheit verschieden viele Stellen.
            const double shown = FieldComponent::convertSpeed (raw, speedOfSoundMps, unit);

            return RoundedSlider::roundedText (shown) + suffix;
        };

        slider.parseText = [unit, speedOfSoundMps] (const juce::String& text)
        {
            const double typed = text.getDoubleValue();

            switch (unit)
            {
                case FieldComponent::SpeedUnit::KmH:  return typed / 3.6;
                case FieldComponent::SpeedUnit::Mach: return typed * std::max (1.0, speedOfSoundMps);
                case FieldComponent::SpeedUnit::Ms:
                default:                              return typed;
            }
        };

        // Ohne das behaelt das Textfeld die alte Beschriftung, bis jemand den
        // Regler anfasst.
        slider.updateText();
    }
}

void MotionPanel::layoutKnob (Knob& knob, juce::Rectangle<int> cell)
{
    knob.label.setBounds (cell.removeFromTop (18));
    knob.slider.setBounds (cell);
}

void MotionPanel::populateChoices (juce::ComboBox& combo, juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID)
{
    if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (paramID)))
    {
        int itemId = 1;
        for (auto& choice : choiceParam->choices)
            combo.addItem (choice, itemId++);
    }
    // Kein Parameter oder falscher Typ unter dieser ID gefunden - waere ein
    // Tippfehler in paramID; Dropdown bliebe dann leer und faellt im
    // provisorischen Test-Editor sofort auf (keine stille Fehlfunktion).
}

MotionPanel::MotionPanel (juce::AudioProcessorValueTreeState& apvts)
{
    setupKnob (smootherTauKnob, apvts, Params::smootherTau, "Tau", Tooltips::Key::SmootherTau);
    setupKnob (slewVmaxKnob,    apvts, Params::slewVmax,    "Slew Vmax", Tooltips::Key::SlewVmax);
    setupKnob (slewAmaxKnob,    apvts, Params::slewAmax,    "Slew Amax", Tooltips::Key::SlewAmax);
    setupKnob (playSpeedKnob,   apvts, Params::playSpeed,   "Play Speed", Tooltips::Key::PlaySpeed);
    setupKnob (globalMaxSpeedKnob, apvts, Params::globalMaxSpeed, "Max Speed", Tooltips::Key::GlobalMaxSpeed);

    // Der Tempo-Deckel ist kein Regler unter vielen: er begrenzt JEDE
    // Bewegung, die Maus wie die Automation wie den Vorbeiflug, und er ist
    // die haeufigste Erklaerung dafuer, dass eine Bewegung nicht so schnell
    // wird wie eingestellt (@dpa 20260824: "unter Bewegung muss der 'Maximal
    // Speed' wichtig erscheinen. derzeit ist es das nicht: als letztes,
    // kleiner, Abgeschnitten, hinzugequetscht").
    //
    // Deshalb steht er jetzt als ERSTES in der gemeinsamen Zeile (siehe
    // resized()), bekommt mehr Breite als die uebrigen und eine fette
    // Beschriftung.
    globalMaxSpeedKnob.label.setFont (juce::Font (juce::FontOptions()
                                                     .withHeight (15.0f)
                                                     .withStyle ("Bold")));

    setupKnob (srcJitterAmountKnob, apvts, Params::srcJitterAmount, "Jitter", Tooltips::Key::SrcJitterAmount);
    setupKnob (srcJitterRateKnob,   apvts, Params::srcJitterRateHz, "Hektik", Tooltips::Key::SrcJitterRate);

    setupKnob (srcJitterMaxSpeedKnob, apvts, Params::srcJitterMaxSpeed, "Jit Max",  Tooltips::Key::SrcJitterMaxSpeed);

    flyLoopButton.setTooltip (Tooltips::text (Tooltips::Key::FlyLoop));
    addAndMakeVisible (flyLoopButton);
    flyLoopAttachment = std::make_unique<ButtonAttachment> (apvts, Params::flyLoop, flyLoopButton);

    srcJitterOnButton.setTooltip (Tooltips::text (Tooltips::Key::SrcJitterOn));
    addAndMakeVisible (srcJitterOnButton);
    srcJitterOnAttachment = std::make_unique<ButtonAttachment> (apvts, Params::srcJitterOn, srcJitterOnButton);
    // Klick UND Presetwechsel loesen onClick aus (ButtonAttachment schaltet
    // per sendNotificationSync um) - deshalb reicht dieser eine Ort, um die
    // Regler in beiden Faellen richtig auszugrauen.
    srcJitterOnButton.onClick = [this] { updateJitterEnabledState(); };

    // Reiter-Umschalter Vorbeiflug/Record-Play (@dpa-Feedback, s. Header).
    // setClickingTogglesState+setRadioGroupId ergibt ein Segmented-Control-
    // Paar: Klick auf den einen hebt den anderen automatisch auf.
    constexpr int tabRadioGroupId = 1;
    flyTabButton.setClickingTogglesState (true);
    flyTabButton.setRadioGroupId (tabRadioGroupId);
    flyTabButton.setTooltip (Tooltips::text (Tooltips::Key::Fly));
    flyTabButton.onClick = [this] { updateTabVisibility(); };
    addAndMakeVisible (flyTabButton);

    recordTabButton.setClickingTogglesState (true);
    recordTabButton.setRadioGroupId (tabRadioGroupId);
    recordTabButton.setTooltip (Tooltips::text (Tooltips::Key::Record));
    recordTabButton.onClick = [this] { updateTabVisibility(); };
    addAndMakeVisible (recordTabButton);

    // Record/Play startet aktiv - Vorbeiflug ist der seltener genutzte
    // Generator-Modus, den man bewusst dazuschaltet.
    recordTabButton.setToggleState (true, juce::dontSendNotification);

    smootherTypeLabel.setText ("Smoother", juce::dontSendNotification);
    smootherTypeLabel.setJustificationType (juce::Justification::centredLeft);
    smootherTypeLabel.setTooltip (Tooltips::text (Tooltips::Key::SmootherType));
    addAndMakeVisible (smootherTypeLabel);
    populateChoices (smootherTypeCombo, apvts, Params::smootherType);
    smootherTypeCombo.setTooltip (Tooltips::text (Tooltips::Key::SmootherType));
    addAndMakeVisible (smootherTypeCombo);
    smootherTypeAttachment = std::make_unique<ComboBoxAttachment> (apvts, Params::smootherType, smootherTypeCombo);
    smootherTypeCombo.onChange = [this] { updateSlewControlsVisibility(); };

    playInterpLabel.setText ("Play Interp", juce::dontSendNotification);
    playInterpLabel.setJustificationType (juce::Justification::centredLeft);
    playInterpLabel.setTooltip (Tooltips::text (Tooltips::Key::PlayInterp));
    addAndMakeVisible (playInterpLabel);
    populateChoices (playInterpCombo, apvts, Params::playInterp);
    playInterpCombo.setTooltip (Tooltips::text (Tooltips::Key::PlayInterp));
    addAndMakeVisible (playInterpCombo);
    playInterpAttachment = std::make_unique<ComboBoxAttachment> (apvts, Params::playInterp, playInterpCombo);
    playInterpCombo.onChange = [this] { updateSlewControlsVisibility(); };

    playLoopButton.setTooltip (Tooltips::text (Tooltips::Key::PlayLoop));
    addAndMakeVisible (playLoopButton);
    playLoopAttachment = std::make_unique<ButtonAttachment> (apvts, Params::playLoop, playLoopButton);

    coastButton.setTooltip (Tooltips::text (Tooltips::Key::Coast));
    coastButton.onClick = [this] { if (onCoastToggled != nullptr) onCoastToggled (coastButton.getToggleState()); };
    addAndMakeVisible (coastButton);

    mouseFrameButton.setTooltip (Tooltips::text (Tooltips::Key::MouseFrame));
    mouseFrameButton.setToggleState (true, juce::dontSendNotification);
    mouseFrameButton.onClick = [this] { if (onMouseFrameToggled != nullptr) onMouseFrameToggled (mouseFrameButton.getToggleState()); };
    addAndMakeVisible (mouseFrameButton);

    recordButton.setTooltip (Tooltips::text (Tooltips::Key::Record));
    addAndMakeVisible (recordButton);
    recordButton.onClick = [this] { if (onRecordClicked != nullptr) onRecordClicked(); };

    playButton.setTooltip (Tooltips::text (Tooltips::Key::Play));
    addAndMakeVisible (playButton);
    playButton.onClick = [this] { if (onPlayClicked != nullptr) onPlayClicked(); };

    // --- Vorbeiflug-Generatoren ---

    flyKindLabel.setText ("Vorbeiflug-Bahn", juce::dontSendNotification);
    flyKindLabel.setJustificationType (juce::Justification::centredLeft);
    flyKindLabel.setTooltip (Tooltips::text (Tooltips::Key::FlyKind));
    addAndMakeVisible (flyKindLabel);
    populateChoices (flyKindCombo, apvts, Params::flyKind);
    addAndMakeVisible (flyKindCombo);
    flyKindAttachment = std::make_unique<ComboBoxAttachment> (apvts, Params::flyKind, flyKindCombo);

    flyStartLabel.setText ("Startvariante", juce::dontSendNotification);
    flyStartLabel.setJustificationType (juce::Justification::centredLeft);
    flyStartLabel.setTooltip (Tooltips::text (Tooltips::Key::FlyStart));
    flyJumpEdgeButton.setTooltip (Tooltips::text (Tooltips::Key::JumpEdge));
    addAndMakeVisible (flyStartLabel);
    populateChoices (flyStartCombo, apvts, Params::flyStart);
    addAndMakeVisible (flyStartCombo);
    flyStartAttachment = std::make_unique<ComboBoxAttachment> (apvts, Params::flyStart, flyStartCombo);

    setupKnob (flyJumpBoomKnob, apvts, Params::jumpBoom, "Sprungknall", Tooltips::Key::JumpBoom);

    flyJumpEdgeButton.setTooltip (Tooltips::text (Tooltips::Key::JumpEdge));
    addAndMakeVisible (flyJumpEdgeButton);
    flyJumpEdgeAttachment = std::make_unique<ButtonAttachment> (apvts, Params::jumpEdge, flyJumpEdgeButton);

    setupKnob (flyDistanceKnob, apvts, Params::flyDistance, "Fly Dist", Tooltips::Key::FlyDistance);
    setupKnob (flyApproachKnob, apvts, Params::flyApproach, "Fly Approach", Tooltips::Key::FlyApproach);
    setupKnob (flySpeedKnob, apvts, Params::flySpeed, "Fly Speed", Tooltips::Key::FlySpeed);

    flyButton.setTooltip (Tooltips::text (Tooltips::Key::Fly));
    addAndMakeVisible (flyButton);
    flyButton.onClick = [this] { if (onFlyClicked != nullptr) onFlyClicked(); };

    // Anfangszustand passend zum tatsaechlich geladenen Smoother setzen - sonst
    // stuenden Slew Vmax/Amax nach dem Oeffnen des Editors aktiv, obwohl der
    // geladene Zustand z.B. "Critically Damped Spring" waehlt.
    updateSlewControlsVisibility();

    // Ausgrauzustand und Reiter-Sichtbarkeit von Hand einmal anstossen - beide
    // Attachments/Toggles haben ihren Startwert schon vor der Zuweisung von
    // onClick durchgereicht (s. Kommentar bei srcJitterOnButton.onClick oben).
    updateJitterEnabledState();
    updateTabVisibility();
}

void MotionPanel::updateJitterEnabledState()
{
    // Aus heisst nur "steht still", nicht "Wert weg" - die Regler bleiben auf
    // ihrem Stand, damit beim Wiedereinschalten sofort der alte Ausschlag
    // greift statt bei null neu anzufangen (siehe Tooltips::Key::SrcJitterOn).
    const bool jitterOn = srcJitterOnButton.getToggleState();

    srcJitterAmountKnob.slider.setEnabled (jitterOn);
    srcJitterAmountKnob.label.setEnabled (jitterOn);
    srcJitterRateKnob.slider.setEnabled (jitterOn);
    srcJitterRateKnob.label.setEnabled (jitterOn);

    // Der Tempo-Deckel des Wacklers bremst seine Bahngeschwindigkeit.
    srcJitterMaxSpeedKnob.slider.setEnabled (jitterOn);
    srcJitterMaxSpeedKnob.label.setEnabled (jitterOn);
}

void MotionPanel::updateTabVisibility()
{
    // Immer nur EINE der beiden Gruppen sichtbar, s. Header-Kommentar bei
    // flyTabButton (@dpa-Feedback: "spart Platz und bringt Uebersicht"). Die
    // jeweils andere bleibt existent (setVisible(false)), keine Regler werden
    // neu angelegt oder ihre Attachments geloest.
    const bool showFly = flyTabButton.getToggleState();

    for (auto* c : { (juce::Component*) &flyKindLabel, (juce::Component*) &flyKindCombo,
                      (juce::Component*) &flyStartLabel, (juce::Component*) &flyStartCombo,
                      (juce::Component*) &flyJumpEdgeButton,
                      (juce::Component*) &flyJumpBoomKnob.slider,
                      (juce::Component*) &flyJumpBoomKnob.label,
                      (juce::Component*) &flyButton,
                      (juce::Component*) &flyDistanceKnob.slider, (juce::Component*) &flyDistanceKnob.label,
                      (juce::Component*) &flyApproachKnob.slider, (juce::Component*) &flyApproachKnob.label,
                      (juce::Component*) &flySpeedKnob.slider,    (juce::Component*) &flySpeedKnob.label })
        c->setVisible (showFly);

    for (auto* c : { (juce::Component*) &recordButton, (juce::Component*) &playButton,
                      (juce::Component*) &smootherTypeLabel, (juce::Component*) &smootherTypeCombo,
                      (juce::Component*) &playInterpLabel,   (juce::Component*) &playInterpCombo,
                      (juce::Component*) &playLoopButton, (juce::Component*) &coastButton, (juce::Component*) &mouseFrameButton,
                      (juce::Component*) &smootherTauKnob.slider, (juce::Component*) &smootherTauKnob.label,
                      (juce::Component*) &slewVmaxKnob.slider,    (juce::Component*) &slewVmaxKnob.label,
                      (juce::Component*) &slewAmaxKnob.slider,    (juce::Component*) &slewAmaxKnob.label,
                      (juce::Component*) &playSpeedKnob.slider,   (juce::Component*) &playSpeedKnob.label })
        c->setVisible (! showFly);
}

void MotionPanel::updateSlewControlsVisibility()
{
    // Die Slew-Regler wirken in zwei unabhaengigen Faellen (siehe
    // DopplerfeldProcessor::advanceMotion): als gewaehltes Glaettungs-
    // verfahren ODER als Ueberschwinger-Waechter waehrend Catmull-Rom-
    // Wiedergabe, ganz unabhaengig davon, welcher Smoother sonst gewaehlt
    // ist. Ausgegraut duerfen sie nur sein, wenn WEDER das eine NOCH das
    // andere zutrifft - alles andere waere ein Regler, der wirkt, obwohl er
    // inaktiv aussieht (@dpa-Repro: "fast Drone" nutzt Critically Damped
    // Spring, Slew Amax stand trotzdem grau UND wirkte).
    const bool isSlewLimiter = smootherTypeCombo.getText() == "Slew Limiter";
    const bool isCatmullClip = playInterpCombo.getText() == "Catmull-Rom";
    const bool active = isSlewLimiter || isCatmullClip;

    slewVmaxKnob.slider.setEnabled (active);
    slewVmaxKnob.label.setEnabled (active);
    slewAmaxKnob.slider.setEnabled (active);
    slewAmaxKnob.label.setEnabled (active);
}

void MotionPanel::setPlaying (bool isPlaying)
{
    playButton.setButtonText (isPlaying ? "Stop" : "Play");
}

void MotionPanel::setFlying (bool isFlying)
{
    flyButton.setButtonText (isFlying ? "Flug stoppen" : "Vorbeiflug");
}

void MotionPanel::setCoastEnabled (bool shouldCoast)
{
    coastButton.setToggleState (shouldCoast, juce::dontSendNotification);
}

void MotionPanel::refreshTooltips()
{
    for (auto* k : { &smootherTauKnob, &slewVmaxKnob, &slewAmaxKnob, &playSpeedKnob,
                      &globalMaxSpeedKnob, &srcJitterAmountKnob, &srcJitterRateKnob,
                      &srcJitterMaxSpeedKnob,
                      &flyDistanceKnob, &flyApproachKnob, &flySpeedKnob, &flyJumpBoomKnob })
    {
        const auto tooltip = Tooltips::text (k->tooltipKey);
        k->slider.setTooltip (tooltip);
        k->label.setTooltip (tooltip);
    }

    flyLoopButton.setTooltip (Tooltips::text (Tooltips::Key::FlyLoop));
    srcJitterOnButton.setTooltip (Tooltips::text (Tooltips::Key::SrcJitterOn));
    smootherTypeLabel.setTooltip (Tooltips::text (Tooltips::Key::SmootherType));
    smootherTypeCombo.setTooltip (Tooltips::text (Tooltips::Key::SmootherType));
    playInterpLabel.setTooltip (Tooltips::text (Tooltips::Key::PlayInterp));
    playInterpCombo.setTooltip (Tooltips::text (Tooltips::Key::PlayInterp));
    playLoopButton.setTooltip (Tooltips::text (Tooltips::Key::PlayLoop));
    coastButton.setTooltip (Tooltips::text (Tooltips::Key::Coast));
    mouseFrameButton.setTooltip (Tooltips::text (Tooltips::Key::MouseFrame));
    recordButton.setTooltip (Tooltips::text (Tooltips::Key::Record));
    playButton.setTooltip (Tooltips::text (Tooltips::Key::Play));
    recordTabButton.setTooltip (Tooltips::text (Tooltips::Key::Record));
    flyKindLabel.setTooltip (Tooltips::text (Tooltips::Key::FlyKind));
    flyStartLabel.setTooltip (Tooltips::text (Tooltips::Key::FlyStart));
    flyButton.setTooltip (Tooltips::text (Tooltips::Key::Fly));
    flyTabButton.setTooltip (Tooltips::text (Tooltips::Key::Fly));

    // Der Tooltip des Speed/Hektik-Reglers haengt an der Betriebsart, nicht
    // nur an der Sprache - die Schleife oben hat gerade den Wackel-Text
    // gesetzt, hier steht wieder der richtige.
    updateJitterEnabledState();
}

void MotionPanel::resized()
{
    // Groesser als die 84x82, die die uebrigen Panels nutzen (@dpa-Feedback:
    // Regler hier "zu klein gequetscht") - der Reiter-Umschalter unten (immer
    // nur eine der beiden Gruppen sichtbar, s. updateTabVisibility()) schafft
    // dafuer erst den Platz, ohne dass die Panel-Gesamthoehe waechst (s.
    // PluginEditor::motionContentHeight-Rechnung im Aufgabenbericht).
    // Nur das DREHRAD auf zwei Drittel (@dpa 20260823, Berichtigung: "NUR die
    // Knobs! Label und Value sollen so bleiben wie zuvor"). Beschriftung
    // (18 px) und Wertefeld (18 px) bleiben unveraendert, die Zellenhoehe
    // schrumpft genau um das Drittel, das dem Drehrad selbst gehoert: aus
    // 100 - 18 - 18 = 64 px Rad werden 43, also 43 + 36 = 79 px Zelle. Die
    // Zellenbreite bleibt ebenfalls, sonst wuerde das Wertefeld beschnitten -
    // JUCE zeichnet das Rad mit dem kleineren der beiden Masse, die Hoehe
    // allein macht es also klein.
    constexpr int knobW = 100;
    constexpr int knobH = 79;
    auto area = getLocalBounds().reduced (8);

    // Reiter oben: Vorbeiflug ODER Record/Play, nie beide gleichzeitig
    // (@dpa-Feedback: "spart Platz und bringt Uebersicht").
    auto tabRow = area.removeFromTop (28);
    flyTabButton.setBounds (tabRow.removeFromLeft (150));
    tabRow.removeFromLeft (8);
    recordTabButton.setBounds (tabRow.removeFromLeft (150));
    area.removeFromTop (6);

    // Beide Reitergruppen bekommen denselben reservierten Bereich zugewiesen
    // (gleicher Startpunkt) - nur eine ist am Ende sichtbar
    // (updateTabVisibility()), aber die Panel-Hoehe bleibt dadurch unabhaengig
    // vom aktiven Reiter konstant, wie von CollapsiblePanel gefordert (siehe
    // Klassenkommentar dort: der Aufrufer legt die Gesamthoehe fest vor).
    // Bemessen an der groesseren der beiden Gruppen (Record/Play).
    constexpr int tabContentHeight = 28 + 6 + 44 + 6 + 26 + 6 + knobH;
    auto tabContentArea = area.removeFromTop (tabContentHeight);

    // --- Record/Play-Gruppe ---
    {
        auto a = tabContentArea;

        auto transportRow = a.removeFromTop (28);
        recordButton.setBounds (transportRow.removeFromLeft (100));
        transportRow.removeFromLeft (8);
        playButton.setBounds (transportRow.removeFromLeft (100));
        a.removeFromTop (6);

        auto comboRow = a.removeFromTop (44);
        auto smootherArea = comboRow.removeFromLeft (180);
        smootherTypeLabel.setBounds (smootherArea.removeFromTop (18));
        smootherTypeCombo.setBounds (smootherArea);
        comboRow.removeFromLeft (12);
        auto interpArea = comboRow.removeFromLeft (180);
        playInterpLabel.setBounds (interpArea.removeFromTop (18));
        playInterpCombo.setBounds (interpArea);
        a.removeFromTop (6);

        auto loopRow = a.removeFromTop (26);
        playLoopButton.setBounds (loopRow.removeFromLeft (100));
        loopRow.removeFromLeft (12);
        coastButton.setBounds (loopRow.removeFromLeft (100));
        loopRow.removeFromLeft (12);
        mouseFrameButton.setBounds (loopRow.removeFromLeft (120));
        a.removeFromTop (6);

        auto knobRow = a.removeFromTop (knobH);
        for (auto* k : { &smootherTauKnob, &slewVmaxKnob, &slewAmaxKnob, &playSpeedKnob })
        {
            layoutKnob (*k, knobRow.removeFromLeft (knobW));
            knobRow.removeFromLeft (4);
        }
    }

    // --- Vorbeiflug-Gruppe ---
    {
        auto a = tabContentArea;

        auto flyRow = a.removeFromTop (28);
        flyButton.setBounds (flyRow.removeFromLeft (140));
        flyRow.removeFromLeft (10);
        flyLoopButton.setBounds (flyRow.removeFromLeft (juce::jmin (80, flyRow.getWidth())));
        flyRow.removeFromLeft (10);

        // Die Sprungkante gehoert zur Startvariante darunter und passt in
        // diese Zeile: die Reglerzeile ist mit vier Knoepfen voll, dort bliebe
        // fuer die Beschriftung kein Platz.
        flyJumpEdgeButton.setBounds (flyRow.removeFromLeft (juce::jmin (110, flyRow.getWidth())));

        a.removeFromTop (6);

        auto flyComboRow = a.removeFromTop (44);
        auto kindArea = flyComboRow.removeFromLeft (180);
        flyKindLabel.setBounds (kindArea.removeFromTop (18));
        flyKindCombo.setBounds (kindArea);
        flyComboRow.removeFromLeft (12);
        auto startArea = flyComboRow.removeFromLeft (180);
        flyStartLabel.setBounds (startArea.removeFromTop (18));
        flyStartCombo.setBounds (startArea);
        a.removeFromTop (6);

        auto flyKnobRow = a.removeFromTop (knobH);
        for (auto* k : { &flyDistanceKnob, &flyApproachKnob, &flySpeedKnob, &flyJumpBoomKnob })
        {
            layoutKnob (*k, flyKnobRow.removeFromLeft (knobW));
            flyKnobRow.removeFromLeft (4);
        }
    }

    // --- Immer sichtbar, unabhaengig vom Reiter: Jitter + gemeinsamer
    // Tempo-Deckel (globalMaxSpeed gilt fuer Maus/Automation UND Vorbeiflug,
    // Jitter ist ebenfalls reiterunabhaengig) ---
    area.removeFromTop (6);

    auto sharedRow = area.removeFromTop (knobH);

    // Der Tempo-Deckel zuerst, breiter als die uebrigen und mit einer Luecke
    // dahinter abgesetzt. Vorher stand er am ENDE der Zeile - und weil hier
    // von links weggenommen wird, war er der erste, dem bei knapper
    // Panelbreite die Haelfte fehlte (@dpa 20260824: "als letztes, kleiner,
    // Abgeschnitten, hinzugequetscht"). Ganz vorn kann ihm das nicht mehr
    // passieren, und die Luecke zeigt, dass er nicht zum Jitter gehoert,
    // sondern ueber beiden Reitern steht.
    constexpr int maxSpeedW = 128;

    layoutKnob (globalMaxSpeedKnob, sharedRow.removeFromLeft (maxSpeedW));
    sharedRow.removeFromLeft (20);

    for (auto* k : { &srcJitterAmountKnob, &srcJitterRateKnob, &srcJitterMaxSpeedKnob })
    {
        layoutKnob (*k, sharedRow.removeFromLeft (knobW));
        sharedRow.removeFromLeft (4);
    }

    const int toggleW = juce::jmin (120, sharedRow.getWidth());
    srcJitterOnButton.setBounds (sharedRow.removeFromTop (18).withWidth (toggleW));
}
