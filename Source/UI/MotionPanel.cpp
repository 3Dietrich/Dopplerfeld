#include "MotionPanel.h"

void MotionPanel::setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
                              const juce::String& paramID, const juce::String& labelText,
                              const juce::String& tooltip)
{
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

        slider.textFromValueFunction = [unit, speedOfSoundMps, suffix] (double raw)
        {
            // Stellenzahl nach der gemeinsamen Regel, gebildet aus dem WERT, der
            // dasteht - nicht aus dem gespeicherten m/s-Wert. Sonst haette
            // dieselbe Einstellung je nach Einheit verschieden viele Stellen.
            const double shown = FieldComponent::convertSpeed (raw, speedOfSoundMps, unit);

            return RoundedSlider::roundedText (shown) + suffix;
        };

        slider.valueFromTextFunction = [unit, speedOfSoundMps] (const juce::String& text)
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
    setupKnob (smootherTauKnob, apvts, Params::smootherTau, "Tau",
               "Zeitkonstante der Bewegungsglaettung: so lange braucht die geglaettete "
               "Position, um einer Zielaenderung zu folgen. Kleiner = direkter/schneller "
               "(schon normale Mausbewegungen ueber wenige Meter koennen dann hohe "
               "Geschwindigkeiten und starken Doppler erzeugen), groesser = traeger.");
    setupKnob (slewVmaxKnob,    apvts, Params::slewVmax,    "Slew Vmax",
               "Maximale Geschwindigkeit in m/s. Wirkt in zwei Faellen: als gewaehltes "
               "Glaettungsverfahren 'Slew Limiter' selbst - UND, unabhaengig davon, immer "
               "als Ueberschwinger-Waechter waehrend Catmull-Rom-Clip-Wiedergabe (dort "
               "begrenzt er nur Ausreisser an scharfen Bahn-Umkehrpunkten, ohne normale "
               "Bewegung abzurunden).");
    setupKnob (slewAmaxKnob,    apvts, Params::slewAmax,    "Slew Amax",
               "Maximale Beschleunigung in m/s^2 - dieselbe Doppelrolle wie Slew Vmax "
               "(gewaehlter Smoother UND Catmull-Rom-Ueberschwinger-Waechter). Bei einer "
               "energiereichen Aufnahme (viele schnelle Richtungswechsel) muss dieser Wert "
               "deutlich ueber der natuerlichen Beschleunigung der Aufnahme liegen, sonst "
               "bremst der Waechter durchgehend statt nur an Ausreissern.");
    setupKnob (playSpeedKnob,   apvts, Params::playSpeed,   "Play Speed",
               "Wiedergabegeschwindigkeit einer Aufnahme (0.25-4x). Skaliert die Bewegung "
               "und damit den Doppler - schnelle Wiedergabe kann Ueberschall erzeugen.");
    setupKnob (globalMaxSpeedKnob, apvts, Params::globalMaxSpeed, "Max Speed",
               "Gemeinsamer Tempo-Deckel fuer ALLE Bewegung - Maus/Automation-Glaettung "
               "UND Vorbeiflug zusammen, unabhaengig vom gewaehlten Smoother. Anders als "
               "'Slew Vmax' (nur bei Slew Limiter, begrenzt dessen eigene Dynamik) wirkt "
               "das hier immer, als letzte Sicherung. Default sehr hoch = wirkungslos, "
               "bis bewusst heruntergestellt.");

    smootherTypeLabel.setText ("Smoother", juce::dontSendNotification);
    smootherTypeLabel.setJustificationType (juce::Justification::centredLeft);
    smootherTypeLabel.setTooltip ("Glaettungsverfahren fuer die Quell-/Hoererbewegung - "
                                  "bestimmt, wie aus ruckartigen Mausbewegungen eine "
                                  "'bewegte Maschine' statt einer 'digitalen Maus' wird.");
    addAndMakeVisible (smootherTypeLabel);
    populateChoices (smootherTypeCombo, apvts, Params::smootherType);
    smootherTypeCombo.setTooltip (smootherTypeLabel.getTooltip());
    addAndMakeVisible (smootherTypeCombo);
    smootherTypeAttachment = std::make_unique<ComboBoxAttachment> (apvts, Params::smootherType, smootherTypeCombo);
    smootherTypeCombo.onChange = [this] { updateSlewControlsVisibility(); };

    playInterpLabel.setText ("Play Interp", juce::dontSendNotification);
    playInterpLabel.setJustificationType (juce::Justification::centredLeft);
    playInterpLabel.setTooltip ("Interpolation der Wiedergabe zwischen aufgezeichneten "
                                "Punkten: Linear (einfach) oder Catmull-Rom (weich, "
                                "ohne Tonhoehensprung an den Stuetzstellen).");
    addAndMakeVisible (playInterpLabel);
    populateChoices (playInterpCombo, apvts, Params::playInterp);
    playInterpCombo.setTooltip (playInterpLabel.getTooltip());
    addAndMakeVisible (playInterpCombo);
    playInterpAttachment = std::make_unique<ComboBoxAttachment> (apvts, Params::playInterp, playInterpCombo);
    playInterpCombo.onChange = [this] { updateSlewControlsVisibility(); };

    playLoopButton.setTooltip ("Wiedergabe am Ende des Clips von vorn beginnen statt zu stoppen.");
    addAndMakeVisible (playLoopButton);
    playLoopAttachment = std::make_unique<ButtonAttachment> (apvts, Params::playLoop, playLoopButton);

    coastButton.setTooltip ("Nach dem Loslassen von Quelle/Hoerer im Feld noch kurz mit Schwung "
                            "weiterlaufen und abbremsen, statt abrupt zu stoppen.");
    coastButton.onClick = [this] { if (onCoastToggled != nullptr) onCoastToggled (coastButton.getToggleState()); };
    addAndMakeVisible (coastButton);

    recordButton.setTooltip ("Aufnahme der (geglaetteten) Quellbewegung starten/stoppen.");
    addAndMakeVisible (recordButton);
    recordButton.onClick = [this] { if (onRecordClicked != nullptr) onRecordClicked(); };

    playButton.setTooltip ("Aufgezeichnete Bewegung abspielen bzw. stoppen.");
    addAndMakeVisible (playButton);
    playButton.onClick = [this] { if (onPlayClicked != nullptr) onPlayClicked(); };

    // --- Vorbeiflug-Generatoren ---

    flyKindLabel.setText ("Vorbeiflug-Bahn", juce::dontSendNotification);
    flyKindLabel.setJustificationType (juce::Justification::centredLeft);
    flyKindLabel.setTooltip ("Bahnart des Generators. 'Durch den Bildschirm' fliegt in die "
                             "Tiefe an einem seitlich versetzten Hoerer vorbei, 'Waagerecht "
                             "querend' von links nach rechts in n Metern Abstand.");
    addAndMakeVisible (flyKindLabel);
    populateChoices (flyKindCombo, apvts, Params::flyKind);
    addAndMakeVisible (flyKindCombo);
    flyKindAttachment = std::make_unique<ComboBoxAttachment> (apvts, Params::flyKind, flyKindCombo);

    flyStartLabel.setText ("Startvariante", juce::dontSendNotification);
    flyStartLabel.setJustificationType (juce::Justification::centredLeft);
    flyStartLabel.setTooltip (
        "'Kontinuierlich' belegt die Vorgeschichte mit genau derselben Geraden vor - der "
        "Loeser sieht eine Quelle, die schon immer geflogen ist, es gibt keinen Sprung. "
        "'Knall-Start' laesst die Quelle schlagartig in voller Fahrt erscheinen: bewusst "
        "unphysikalisch, dafuer ein reproduzierbarer Testfall fuer den Ueberschallknall.");
    addAndMakeVisible (flyStartLabel);
    populateChoices (flyStartCombo, apvts, Params::flyStart);
    addAndMakeVisible (flyStartCombo);
    flyStartAttachment = std::make_unique<ComboBoxAttachment> (apvts, Params::flyStart, flyStartCombo);

    setupKnob (flyDistanceKnob, apvts, Params::flyDistance, "Fly Dist",
               "Abstand, in dem die Bahn am Hoerer vorbeilaeuft - senkrecht zur "
               "Flugrichtung. Kleiner Abstand = kraeftigerer Doppler-Umschlag beim "
               "Vorbeiflug. Aendert NICHT die Bahnlaenge, dafuer 'Fly Approach'.");
    setupKnob (flyApproachKnob, apvts, Params::flyApproach, "Fly Approach",
               "Anflug-/Abflugstrecke: wie weit vor (und nach) dem naechsten Punkt die "
               "Bahn beginnt bzw. endet. Unabhaengig von 'Fly Dist' (das ist nur der "
               "seitliche Abstand) - laenger heisst mehr hoerbare Annaeherung vor dem "
               "eigentlichen Vorbeiflug, besonders bei hoher Fluggeschwindigkeit sinnvoll.");
    setupKnob (flySpeedKnob, apvts, Params::flySpeed, "Fly Speed",
               "Fluggeschwindigkeit in m/s, live veraenderbar und automatisierbar - die "
               "Bahn integriert den jeweils aktuellen Wert, ein Automationsverlauf "
               "beschleunigt die Quelle also wirklich. Ueber 343 m/s wird der Flug "
               "ueberschallschnell.");

    flyButton.setTooltip ("Vorbeiflug starten bzw. laufenden Flug abbrechen. Die Bahnart und "
                          "die Startvariante gelten ab dem naechsten Start, das Tempo wirkt "
                          "sofort.");
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
    coastButton.setBounds (loopRow.removeFromLeft (120));
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
