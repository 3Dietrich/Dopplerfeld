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
    // Die drei Regler, die ein Tempo einstellen. Der gespeicherte Wert bleibt in
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
    addAndMakeVisible (flyStartLabel);
    populateChoices (flyStartCombo, apvts, Params::flyStart);
    addAndMakeVisible (flyStartCombo);
    flyStartAttachment = std::make_unique<ComboBoxAttachment> (apvts, Params::flyStart, flyStartCombo);

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
                      &globalMaxSpeedKnob, &flyDistanceKnob, &flyApproachKnob, &flySpeedKnob })
    {
        const auto tooltip = Tooltips::text (k->tooltipKey);
        k->slider.setTooltip (tooltip);
        k->label.setTooltip (tooltip);
    }

    smootherTypeLabel.setTooltip (Tooltips::text (Tooltips::Key::SmootherType));
    smootherTypeCombo.setTooltip (Tooltips::text (Tooltips::Key::SmootherType));
    playInterpLabel.setTooltip (Tooltips::text (Tooltips::Key::PlayInterp));
    playInterpCombo.setTooltip (Tooltips::text (Tooltips::Key::PlayInterp));
    playLoopButton.setTooltip (Tooltips::text (Tooltips::Key::PlayLoop));
    coastButton.setTooltip (Tooltips::text (Tooltips::Key::Coast));
    mouseFrameButton.setTooltip (Tooltips::text (Tooltips::Key::MouseFrame));
    recordButton.setTooltip (Tooltips::text (Tooltips::Key::Record));
    playButton.setTooltip (Tooltips::text (Tooltips::Key::Play));
    flyKindLabel.setTooltip (Tooltips::text (Tooltips::Key::FlyKind));
    flyStartLabel.setTooltip (Tooltips::text (Tooltips::Key::FlyStart));
    flyButton.setTooltip (Tooltips::text (Tooltips::Key::Fly));
}

void MotionPanel::resized()
{
    constexpr int knobW = 84;
    constexpr int knobH = 82;
    auto area = getLocalBounds().reduced (8);

    // Record/Play oben - die Bewegungsaufnahme ist der Einstiegspunkt in
    // diese Gruppe.
    auto transportRow = area.removeFromTop (28);
    recordButton.setBounds (transportRow.removeFromLeft (100));
    transportRow.removeFromLeft (8);
    playButton.setBounds (transportRow.removeFromLeft (100));
    area.removeFromTop (6);

    // Zwei Dropdowns nebeneinander (Smoother-Typ, Interpolation).
    auto comboRow = area.removeFromTop (44);
    auto smootherArea = comboRow.removeFromLeft (180);
    smootherTypeLabel.setBounds (smootherArea.removeFromTop (18));
    smootherTypeCombo.setBounds (smootherArea);
    comboRow.removeFromLeft (12);
    auto interpArea = comboRow.removeFromLeft (180);
    playInterpLabel.setBounds (interpArea.removeFromTop (18));
    playInterpCombo.setBounds (interpArea);
    area.removeFromTop (6);

    auto loopRow = area.removeFromTop (26);
    playLoopButton.setBounds (loopRow.removeFromLeft (100));
    loopRow.removeFromLeft (12);
    coastButton.setBounds (loopRow.removeFromLeft (100));
    loopRow.removeFromLeft (12);
    mouseFrameButton.setBounds (loopRow.removeFromLeft (120));
    area.removeFromTop (6);

    auto knobRow = area.removeFromTop (knobH);
    for (auto* k : { &smootherTauKnob, &slewVmaxKnob, &slewAmaxKnob, &playSpeedKnob, &globalMaxSpeedKnob })
    {
        layoutKnob (*k, knobRow.removeFromLeft (knobW));
        knobRow.removeFromLeft (4);
    }

    // Vorbeiflug-Generatoren als eigener Block darunter.
    area.removeFromTop (10);

    auto flyRow = area.removeFromTop (28);
    flyButton.setBounds (flyRow.removeFromLeft (140));
    area.removeFromTop (6);

    auto flyComboRow = area.removeFromTop (44);
    auto kindArea = flyComboRow.removeFromLeft (180);
    flyKindLabel.setBounds (kindArea.removeFromTop (18));
    flyKindCombo.setBounds (kindArea);
    flyComboRow.removeFromLeft (12);
    auto startArea = flyComboRow.removeFromLeft (180);
    flyStartLabel.setBounds (startArea.removeFromTop (18));
    flyStartCombo.setBounds (startArea);
    area.removeFromTop (6);

    auto flyKnobRow = area.removeFromTop (knobH);
    for (auto* k : { &flyDistanceKnob, &flyApproachKnob, &flySpeedKnob })
    {
        layoutKnob (*k, flyKnobRow.removeFromLeft (knobW));
        flyKnobRow.removeFromLeft (4);
    }
}
