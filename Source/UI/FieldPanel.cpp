#include "FieldPanel.h"

void FieldPanel::setupKnob (Knob& knob, juce::AudioProcessorValueTreeState& apvts,
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

void FieldPanel::layoutKnob (Knob& knob, juce::Rectangle<int> cell)
{
    knob.label.setBounds (cell.removeFromTop (18));
    knob.slider.setBounds (cell);
}

FieldPanel::FieldPanel (juce::AudioProcessorValueTreeState& apvts)
{
    setupKnob (fieldMetresKnob, apvts, Params::fieldMetres,     "Field Size",
               "Breite der Feldflaeche in Metern (1-10000) - der Massstab des 700x400px-"
               "Feldes. Aenderung ueberblendet weich (kein Klick), Positionen bleiben "
               "normiert erhalten.");
    setupKnob (boomLimitKnob,   apvts, Params::boomLimitDb,     "Boom Limit",
               "Regularisierung der Amplitude bei Ueberschall (1-M_r nahe 0). Kleinere "
               "Werte = staerkerer, spitzerer Knall; groessere Werte = sanftere Begrenzung.");
    setupKnob (airAbsorbKnob,   apvts, Params::airAbsorbAmount, "Air Absorb",
               "Staerke der distanzabhaengigen Luftdaempfung (Hoehenverlust ueber "
               "Entfernung). 0 = aus, 1 = voll.");
    setupKnob (fadeManualKnob,  apvts, Params::fadeManualMs,    "Fade Manual",
               "Feste Ueberblendzeit (ms) bei unstetigen Aenderungen, wirkt nur wenn "
               "'Fade Auto' ausgeschaltet ist.");
    setupKnob (outputGainKnob,  apvts, Params::outputGain,      "Output Gain",
               "Ausgangslautstaerke (dB). Der Pegel folgt 1/Abstand ohne Referenzdistanz - "
               "bei grossen Feldern ist das leise, hier laesst sich gegensteuern.");

    setupKnob (srcZKnob,        apvts, Params::srcZ,            "Source Z",
               "Hoehe der Quelle ueber dem Boden in Metern. 0 = auf dem Boden (Auto, "
               "Motorrad), groessere Werte fuer Ueberflug o.ae. x/y stellt man mit der "
               "Maus im Feld ein, die Hoehe nur hier.");
    setupKnob (lisZKnob,        apvts, Params::lisZ,            "Listener Z",
               "Ohrhoehe des Hoerers ueber dem Boden in Metern (Standard 1.75 = stehend). "
               "Erst ein Hoehenunterschied zwischen Quelle und Hoerer macht die "
               "Bodenreflexion hoerbar.");

    fadeAutoButton.setTooltip ("Ueberblendzeit bei Sprüngen automatisch aus der jeweiligen "
                               "Aenderung ableiten (Distanz/Klangfrequenz), statt eine feste "
                               "Zeit ('Fade Manual') zu benutzen.");
    addAndMakeVisible (fadeAutoButton);
    fadeAutoAttachment = std::make_unique<ButtonAttachment> (apvts, Params::fadeAuto, fadeAutoButton);

    limiterOnButton.setTooltip ("Sicherheitsbegrenzer am Ausgang (weiche Kniekennlinie) - "
                                "faengt Uberschall-Spitzen ab, ohne sie hart zu clippen.");
    addAndMakeVisible (limiterOnButton);
    limiterOnAttachment = std::make_unique<ButtonAttachment> (apvts, Params::limiterOn, limiterOnButton);

    levelMeter.setTooltip ("Ausgangspegel (nach Gain+Limiter). Weisser Strich = -6dB. "
                           "Rote LED oben = Clipping, haelt 500ms.");
    addAndMakeVisible (levelMeter);
}

void FieldPanel::resized()
{
    constexpr int knobW = 84;
    constexpr int knobH = 82;
    auto area = getLocalBounds().reduced (8);

    auto toggleRow = area.removeFromTop (26);
    fadeAutoButton.setBounds (toggleRow.removeFromLeft (120));
    toggleRow.removeFromLeft (8);
    limiterOnButton.setBounds (toggleRow.removeFromLeft (100));
    area.removeFromTop (6);

    auto knobRow = area.removeFromTop (knobH);
    for (auto* k : { &fieldMetresKnob, &boomLimitKnob, &airAbsorbKnob, &fadeManualKnob, &outputGainKnob })
    {
        layoutKnob (*k, knobRow.removeFromLeft (knobW));
        knobRow.removeFromLeft (4);
    }

    // Levelmeter direkt neben Output Gain, gleiche Höhe wie die Regler
    // (ohne die Beschriftungszeile, die braucht das Meter nicht).
    knobRow.removeFromLeft (4);
    levelMeter.setBounds (knobRow.removeFromLeft (24));

    // Zweite Reihe: die Geometrie-Achse z.
    area.removeFromTop (6);

    auto geoRow = area.removeFromTop (knobH);
    for (auto* k : { &srcZKnob, &lisZKnob })
    {
        layoutKnob (*k, geoRow.removeFromLeft (knobW));
        geoRow.removeFromLeft (4);
    }
}
