#include "MotionPanel.h"

void MotionPanel::setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                              const juce::String& paramID, const char* labelText,
                              Tooltips::Key tooltipKey)
{
    knob.tooltipKey = tooltipKey;
    const auto tooltip = Tooltips::text (tooltipKey);

    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knob.slider.setTooltip (tooltip);
    addAndMakeVisible (knob.slider);

    knob.labelSource = labelText;
    knob.label.setText (Labels::text (labelText), juce::dontSendNotification);
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

        // Das Tempo des Wacklers ist genauso ein Tempo wie die anderen drei
        // (@dpa 20260824: "Jit Max ist ja eine 'Speed'angabe, bitte gib ihn
        // via gesetzter 'Tempo Einheit' an").
        { &srcJitterSpeedKnob, false },
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

        // Angezeigt wird die uebersetzte Fassung; der Parameter behaelt seine
        // eigene Liste (siehe Labels.h). combo.addItem() nimmt den Text nur
        // zur Anzeige - verknuepft wird ueber den Index.
        for (auto& choice : choiceParam->choices)
            combo.addItem (Labels::text (choice.toRawUTF8()), itemId++);
    }
    // Kein Parameter oder falscher Typ unter dieser ID gefunden - waere ein
    // Tippfehler in paramID; Dropdown bliebe dann leer und faellt im
    // provisorischen Test-Editor sofort auf (keine stille Fehlfunktion).
}

MotionPanel::MotionPanel (juce::AudioProcessorValueTreeState& apvts)
{
    apvtsForLabels = &apvts;

    setupKnob (smootherTauKnob, apvts, Params::smootherTau, "Tau", Tooltips::Key::SmootherTau);
    setupKnob (slewVmaxKnob,    apvts, Params::slewVmax,    "Slew Vmax", Tooltips::Key::SlewVmax);
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
    setupKnob (srcJitterSpeedKnob,  apvts, Params::srcJitterSpeed,  "Jit Tempo", Tooltips::Key::SrcJitterSpeed);
    setupKnob (srcJitterZKnob,      apvts, Params::srcJitterZAmount, "Z-Anteil", Tooltips::Key::SrcJitterZAmount);


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

    // Drei Reiter, eine Bewegungsquelle je Reiter (@dpa 20260825: "dritten
    // Reiter klingt gut"): Live ist Maus und Automation, dazu gehoert das
    // Glaettungsverfahren und der Nachlauf. Vorher lagen die beiden in der
    // Record/Play-Gruppe beziehungsweise in der gemeinsamen Zeile, also dort,
    // wo sie gar nicht wirken.
    liveTabButton.setClickingTogglesState (true);
    liveTabButton.setRadioGroupId (tabRadioGroupId);
    liveTabButton.setTooltip (Tooltips::text (Tooltips::Key::LiveTab));
    liveTabButton.onClick = [this] { updateTabVisibility(); };
    addAndMakeVisible (liveTabButton);

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

    // Welcher Reiter gerade gilt, war an drei gleich aussehenden Knoepfen
    // nicht abzulesen. Der aktive bekommt den Ton des Bereichs (Theme::Panel),
    // die beiden anderen bleiben leer.
    for (auto* tab : { &liveTabButton, &flyTabButton, &recordTabButton })
    {
        tab->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        tab->setColour (juce::TextButton::buttonOnColourId, Theme::activeTab (Theme::Panel::motion));
        tab->setColour (juce::TextButton::textColourOffId, Theme::muted);
        tab->setColour (juce::TextButton::textColourOnId, Theme::text);
    }

    smootherTypeLabel.setText (Labels::text ("Smoother"), juce::dontSendNotification);
    smootherTypeLabel.setJustificationType (juce::Justification::centredLeft);
    smootherTypeLabel.setTooltip (Tooltips::text (Tooltips::Key::SmootherType));
    addAndMakeVisible (smootherTypeLabel);
    populateChoices (smootherTypeCombo, apvts, Params::smootherType);
    smootherTypeCombo.setTooltip (Tooltips::text (Tooltips::Key::SmootherType));
    addAndMakeVisible (smootherTypeCombo);
    smootherTypeAttachment = std::make_unique<ComboBoxAttachment> (apvts, Params::smootherType, smootherTypeCombo);
    smootherTypeCombo.onChange = [this] { updateSlewControlsVisibility(); };

    playInterpLabel.setText (Labels::text ("Play Interp"), juce::dontSendNotification);
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

    flyKindLabel.setText (Labels::text ("Vorbeiflug-Bahn"), juce::dontSendNotification);
    flyKindLabel.setJustificationType (juce::Justification::centredLeft);
    flyKindLabel.setTooltip (Tooltips::text (Tooltips::Key::FlyKind));
    addAndMakeVisible (flyKindLabel);
    populateChoices (flyKindCombo, apvts, Params::flyKind);
    addAndMakeVisible (flyKindCombo);
    flyKindAttachment = std::make_unique<ComboBoxAttachment> (apvts, Params::flyKind, flyKindCombo);

    flyStartLabel.setText (Labels::text ("Startvariante"), juce::dontSendNotification);
    flyStartLabel.setJustificationType (juce::Justification::centredLeft);
    flyStartLabel.setTooltip (Tooltips::text (Tooltips::Key::FlyStart));
    addAndMakeVisible (flyStartLabel);
    populateChoices (flyStartCombo, apvts, Params::flyStart);
    addAndMakeVisible (flyStartCombo);
    flyStartAttachment = std::make_unique<ComboBoxAttachment> (apvts, Params::flyStart, flyStartCombo);

    setupKnob (flyJumpBoomKnob, apvts, Params::jumpBoom, "Startknall", Tooltips::Key::JumpBoom);
    setupKnob (flyJumpSizeKnob, apvts, Params::jumpBoomSize, "Knall-Länge", Tooltips::Key::JumpBoomSize);


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
    srcJitterSpeedKnob.slider.setEnabled (jitterOn);
    srcJitterSpeedKnob.label.setEnabled (jitterOn);
    srcJitterZKnob.slider.setEnabled (jitterOn);
    srcJitterZKnob.label.setEnabled (jitterOn);

    // Der Tempo-Deckel des Wacklers bremst seine Bahngeschwindigkeit.

}

void MotionPanel::updateTabVisibility()
{
    // Immer nur EINE der beiden Gruppen sichtbar, s. Header-Kommentar bei
    // flyTabButton (@dpa-Feedback: "spart Platz und bringt Uebersicht"). Die
    // jeweils andere bleibt existent (setVisible(false)), keine Regler werden
    // neu angelegt oder ihre Attachments geloest.
    const bool showFly    = flyTabButton.getToggleState();
    const bool showLive   = liveTabButton.getToggleState();
    const bool showRecord = ! showFly && ! showLive;

    for (auto* c : { (juce::Component*) &flyKindLabel, (juce::Component*) &flyKindCombo,
                      (juce::Component*) &flyStartLabel, (juce::Component*) &flyStartCombo,
                      (juce::Component*) &flyJumpBoomKnob.slider,
                      (juce::Component*) &flyJumpBoomKnob.label,
                      (juce::Component*) &flyButton, (juce::Component*) &flyLoopButton,
                      (juce::Component*) &flyJumpSizeKnob.slider,
                      (juce::Component*) &flyJumpSizeKnob.label,
                      (juce::Component*) &flyDistanceKnob.slider, (juce::Component*) &flyDistanceKnob.label,
                      (juce::Component*) &flyApproachKnob.slider, (juce::Component*) &flyApproachKnob.label,
                      (juce::Component*) &flySpeedKnob.slider,    (juce::Component*) &flySpeedKnob.label })
        c->setVisible (showFly);

    for (auto* c : { (juce::Component*) &recordButton, (juce::Component*) &playButton,
                      (juce::Component*) &playInterpLabel, (juce::Component*) &playInterpCombo,
                      (juce::Component*) &playLoopButton,
                      (juce::Component*) &playSpeedKnob.slider,   (juce::Component*) &playSpeedKnob.label })
        c->setVisible (showRecord);

    for (auto* c : { (juce::Component*) &smootherTypeLabel, (juce::Component*) &smootherTypeCombo,
                      (juce::Component*) &coastButton, (juce::Component*) &mouseFrameButton,
                      (juce::Component*) &smootherTauKnob.slider, (juce::Component*) &smootherTauKnob.label,
                      (juce::Component*) &slewVmaxKnob.slider,    (juce::Component*) &slewVmaxKnob.label })
        c->setVisible (showLive);
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
}

void MotionPanel::setPlaying (bool isPlaying)
{
    playButton.setButtonText (isPlaying ? "Stop" : "Play");

    // Gruen wie der "gut"-Zustand des CPU-Balkens im Editor (limegreen) -
    // derselbe Ton steht dort schon fuer "laeuft, alles in Ordnung". Ohne
    // Faerbung sah der Knopf im Debug-Fenster kaum anders aus, ob gerade
    // abgespielt wird oder nicht (@dpa-Auftrag).
    playButton.setColour (juce::TextButton::buttonColourId,
                           isPlaying ? juce::Colours::limegreen.withAlpha (0.4f)
                                     : juce::Colours::transparentBlack);
}

void MotionPanel::setRecording (bool isRecording)
{
    recordButton.setButtonText (isRecording ? "Stop" : "Record");

    // Rot wie die Clip-LED im Pegelmeter (siehe LevelMeter.cpp) - im Projekt
    // schon die Farbe fuer "hier passiert gerade etwas, das Aufmerksamkeit
    // verlangt".
    recordButton.setColour (juce::TextButton::buttonColourId,
                             isRecording ? juce::Colours::red.withAlpha (0.45f)
                                         : juce::Colours::transparentBlack);
}

void MotionPanel::setFlying (bool isFlying)
{
    flyingNow = isFlying;

    flyButton.setButtonText (Labels::text (isFlying ? "Flug stoppen" : "Vorbeiflug"));
}

void MotionPanel::setCoastEnabled (bool shouldCoast)
{
    coastButton.setToggleState (shouldCoast, juce::dontSendNotification);
}


void MotionPanel::relabelChoices()
{
    if (apvtsForLabels == nullptr)
        return;

    auto relabel = [this] (juce::ComboBox& combo, const juce::String& paramID)
    {
        if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (
                apvtsForLabels->getParameter (paramID)))
        {
            int itemId = 1;

            for (auto& choice : choiceParam->choices)
                combo.changeItemText (itemId++, Labels::text (choice.toRawUTF8()));
        }
    };

    relabel (smootherTypeCombo, Params::smootherType);
    relabel (playInterpCombo, Params::playInterp);
    relabel (flyKindCombo, Params::flyKind);
    relabel (flyStartCombo, Params::flyStart);

    // changeItemText allein zeichnet das geschlossene Feld nicht neu.
    repaint();
}

void MotionPanel::refreshTooltips()
{
    relabelChoices();

    smootherTypeLabel.setText (Labels::text ("Smoother"), juce::dontSendNotification);
    playInterpLabel.setText (Labels::text ("Play Interp"), juce::dontSendNotification);
    flyKindLabel.setText (Labels::text ("Vorbeiflug-Bahn"), juce::dontSendNotification);
    flyStartLabel.setText (Labels::text ("Startvariante"), juce::dontSendNotification);


    // Beschriftungen mit dem Sprachumschalter mitnehmen.
    srcJitterOnButton.setButtonText (Labels::text ("Jitter An"));
    flyLoopButton.setButtonText (Labels::text ("Loop"));
    liveTabButton.setButtonText (Labels::text ("Live"));
    flyTabButton.setButtonText (Labels::text ("Vorbeiflug"));

    // Der Flug-Knopf traegt seinen Zustand im Text (siehe setFlying) - beim
    // Sprachwechsel bekommt er den Text zum aktuellen Zustand, nicht
    // stumpf "Vorbeiflug".
    flyButton.setButtonText (Labels::text (flyingNow ? "Flug stoppen" : "Vorbeiflug"));
    playLoopButton.setButtonText (Labels::text ("Loop"));
    coastButton.setButtonText (Labels::text ("Nachlauf"));
    mouseFrameButton.setButtonText (Labels::text ("Maus glatt"));

    for (auto* k : { &smootherTauKnob, &slewVmaxKnob, &playSpeedKnob,
                      &globalMaxSpeedKnob, &srcJitterAmountKnob, &srcJitterSpeedKnob,
                      &srcJitterZKnob,
                      &flyDistanceKnob, &flyApproachKnob, &flySpeedKnob, &flyJumpBoomKnob })
    {
        const auto tooltip = Tooltips::text (k->tooltipKey);
        k->slider.setTooltip (tooltip);
        k->label.setTooltip (tooltip);

        // Der Sprachumschalter nimmt die Beschriftung mit, nicht nur den
        // Hinweis (@dpa 20260824: "bitte auch alle deutschen Labels in EN
        // mode auf englisch").
        k->label.setText (Labels::text (k->labelSource), juce::dontSendNotification);
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
    // Reglermasse wie im Motor-Panel (@dpa 20260825: "mach die Controls bitte
    // so gross wie in Motor, dann ordne nochmal Uebersichtlich und kompakt").
    // Dieselben Masse ueberall heissen: die Panels sehen aus wie ein Geraet
    // und nicht wie zwei - und die eingesparte Breite ist genau das, was die
    // Jitter-Zeile braucht, um samt Schalter in EINE Reihe zu passen.
    constexpr int knobW = 84;
    constexpr int knobH = 67;
    auto area = getLocalBounds().reduced (8);

    // Reiter oben: immer genau eine der drei Bewegungsquellen sichtbar
    // (@dpa-Feedback: "spart Platz und bringt Uebersicht"). Die Breite ist
    // gleichmaessig aufgeteilt, damit die Zeile bei jeder Panelbreite aufgeht.
    auto tabRow = area.removeFromTop (28);

    {
        constexpr int gap = 8;

        const int tabW = (tabRow.getWidth() - 2 * gap) / 3;

        liveTabButton.setBounds (tabRow.removeFromLeft (tabW));
        tabRow.removeFromLeft (gap);
        flyTabButton.setBounds (tabRow.removeFromLeft (tabW));
        tabRow.removeFromLeft (gap);
        recordTabButton.setBounds (tabRow.removeFromLeft (tabW));
    }

    area.removeFromTop (6);

    // Beide Reitergruppen bekommen denselben reservierten Bereich zugewiesen
    // (gleicher Startpunkt) - nur eine ist am Ende sichtbar
    // (updateTabVisibility()), aber die Panel-Hoehe bleibt dadurch unabhaengig
    // vom aktiven Reiter konstant, wie von CollapsiblePanel gefordert (siehe
    // Klassenkommentar dort: der Aufrufer legt die Gesamthoehe fest vor).
    // Bemessen an der groessten der drei Gruppen. Alle drei kommen mit
    // hoechstens drei Zeilen aus: eine Knopfzeile, eine Auswahlzeile und eine
    // Reglerzeile.
    constexpr int tabContentHeight = 28 + 6 + 44 + 6 + knobH;
    auto tabContentArea = area.removeFromTop (tabContentHeight);

    // --- Live-Gruppe: Maus und Automation ---
    //
    // Das Glaettungsverfahren steht hier, weil es die Bewegung formt, die von
    // Maus oder Automation kommt. Es wirkt zusaetzlich auf eine LINEAR
    // abgespielte Aufzeichnung und als Ueberschwinger-Waechter waehrend einer
    // Catmull-Rom-Wiedergabe (siehe updateSlewControlsVisibility und
    // DopplerfeldProcessor::advanceMotion) - das steht im Tooltip des Reiters,
    // damit niemand die Regler im Record/Play-Reiter sucht.
    {
        auto a = tabContentArea;

        // Die beiden Schalter stehen neben der Auswahl, nicht darunter: so
        // kommt die Gruppe mit denselben drei Zeilen aus wie die anderen
        // beiden.
        auto comboRow = a.removeFromTop (28);
        coastButton.setBounds (comboRow.removeFromLeft (110).withSizeKeepingCentre (110, 18));
        comboRow.removeFromLeft (12);
        mouseFrameButton.setBounds (comboRow.removeFromLeft (120).withSizeKeepingCentre (120, 18));
        a.removeFromTop (6);

        auto smootherRow = a.removeFromTop (44);
        auto smootherArea = smootherRow.removeFromLeft (180);
        smootherTypeLabel.setBounds (smootherArea.removeFromTop (18));
        smootherTypeCombo.setBounds (smootherArea);
        a.removeFromTop (6);

        auto knobRow = a.removeFromTop (knobH);
        for (auto* k : { &smootherTauKnob, &slewVmaxKnob })
        {
            layoutKnob (*k, knobRow.removeFromLeft (knobW));
            knobRow.removeFromLeft (4);
        }
    }

    // --- Record/Play-Gruppe ---
    {
        auto a = tabContentArea;

        // Aufnehmen, Abspielen und die Dauerschleife in EINER Zeile: sie
        // gehoeren zusammen, und die gesparte Zeile nimmt das ganze Panel mit
        // (siehe tabContentHeight oben).
        auto transportRow = a.removeFromTop (28);
        recordButton.setBounds (transportRow.removeFromLeft (100));
        transportRow.removeFromLeft (8);
        playButton.setBounds (transportRow.removeFromLeft (100));
        transportRow.removeFromLeft (12);
        playLoopButton.setBounds (transportRow.removeFromLeft (90).withSizeKeepingCentre (90, 18));
        a.removeFromTop (6);

        auto comboRow = a.removeFromTop (44);
        auto interpArea = comboRow.removeFromLeft (180);
        playInterpLabel.setBounds (interpArea.removeFromTop (18));
        playInterpCombo.setBounds (interpArea);
        a.removeFromTop (6);

        auto knobRow = a.removeFromTop (knobH);
        layoutKnob (playSpeedKnob, knobRow.removeFromLeft (knobW));
    }

    // --- Vorbeiflug-Gruppe ---
    {
        auto a = tabContentArea;

        auto flyRow = a.removeFromTop (28);
        flyButton.setBounds (flyRow.removeFromLeft (140));
        flyRow.removeFromLeft (10);
        flyLoopButton.setBounds (flyRow.removeFromLeft (juce::jmin (80, flyRow.getWidth())));

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
        for (auto* k : { &flyDistanceKnob, &flyApproachKnob, &flySpeedKnob,
                          &flyJumpBoomKnob, &flyJumpSizeKnob })
        {
            layoutKnob (*k, flyKnobRow.removeFromLeft (knobW));
            flyKnobRow.removeFromLeft (4);
        }
    }

    // --- Immer sichtbar, unabhaengig vom Reiter: gemeinsamer Tempo-Deckel
    // und der Jitter. Beide gelten fuer JEDE Bewegungsquelle - Maus,
    // Automation, Vorbeiflug und Wiedergabe - und gehoeren deshalb in keinen
    // der drei Reiter ---
    //
    // EINE Zeile, in dieser Reihenfolge:
    //   Tempo-Deckel | Luecke | Jitter An | Jitter | Jit Tempo | Z-Anteil
    //
    // Der Schalter steht VOR den Reglern, die er schaltet, und nicht am Ende
    // der Zeile. Das ist kein Geschmack: hier wird von links weggenommen, und
    // wer am Ende steht, bekommt das, was uebrig bleibt - beim Schalter waren
    // das null Pixel, beim Tempo-Deckel am 24.08. die halbe Breite.
    //
    // Die Rechnung geht auf: 84 + 4 + 6 + 82 + 4 + 3 x (84 + 4) - 4 = 440
    // Pixel bei 446 verfuegbaren. Die Luecke von 6 Pixeln setzt den
    // Tempo-Deckel ab - er gehoert nicht zum Jitter, sondern ueber alle
    // Reiter. Nachgeprueft wird das im load_check-Abschnitt "Bedienelemente".
    area.removeFromTop (6);

    auto sharedRow = area.removeFromTop (knobH);

    layoutKnob (globalMaxSpeedKnob, sharedRow.removeFromLeft (knobW));
    sharedRow.removeFromLeft (4 + 6);

    constexpr int toggleW = 82;

    auto toggleColumn = sharedRow.removeFromLeft (toggleW);
    sharedRow.removeFromLeft (4);

    srcJitterOnButton.setBounds (toggleColumn.withSizeKeepingCentre (toggleW, 18));

    for (auto* k : { &srcJitterAmountKnob, &srcJitterSpeedKnob, &srcJitterZKnob })
    {
        layoutKnob (*k, sharedRow.removeFromLeft (knobW));
        sharedRow.removeFromLeft (4);
    }
}
