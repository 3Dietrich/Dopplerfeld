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

    setupKnob (distanceCurveKnob, apvts, Params::distanceCurve, "Distance Curve",
               "Wie stark die Entfernung auf die Lautstaerke wirkt. Mitte (0) = "
               "physikalisch korrektes 1/R, wie bisher. Nach rechts: faellt schneller "
               "ab (schaerfer abgegrenzt). Nach links: faellt flacher ab (traegt "
               "weiter, verschwimmt mehr).");

    setupKnob (srcZKnob,        apvts, Params::srcZ,            "Source Z",
               "Hoehe der Quelle ueber dem Boden in Metern. 0 = auf dem Boden (Auto, "
               "Motorrad), groessere Werte fuer Ueberflug o.ae. x/y stellt man mit der "
               "Maus im Feld ein, die Hoehe nur hier.");
    setupKnob (lisZKnob,        apvts, Params::lisZ,            "Listener Z",
               "Ohrhoehe des Hoerers ueber dem Boden in Metern (Standard 1.75 = stehend). "
               "Erst ein Hoehenunterschied zwischen Quelle und Hoerer macht die "
               "Bodenreflexion hoerbar.");
    setupKnob (srcJitterAmountKnob, apvts, Params::srcJitterAmount, "Jitter",
               "Auslenkung einer langsamen, staendigen Mikrobewegung der Quelle M in "
               "Metern - 0 = aus (Default). Wirkt immer additiv, auch waehrend normaler "
               "Bewegung; im Stillstand ist es der 'echte Chorus', bei Bewegung geht es im "
               "normalen Doppler unter.");
    setupKnob (srcJitterRateKnob,   apvts, Params::srcJitterRateHz, "Hektik",
               "Wie schnell/unruhig sich die Jitter-Bewegung aendert (Hz). Kleine Werte = "
               "langsames Driften, grosse Werte = nervoeses Zittern.");

    setupKnob (groundDampKnob,  apvts, Params::groundDampAmount, "Ground Damp",
               "Wie stark der Boden bei der Reflexion die Hoehen schluckt. 0 = ideal "
               "harte Flaeche (Reflexion klingt wie der Direktschall), 1 = weicher Boden "
               "(Gras/Erde). Wirkt nur auf den gespiegelten Pfad, nicht auf den "
               "Direktschall - und nur bei eingeschalteter Bodenreflexion.");

    groundReflectionButton.setTooltip (
        "Zweiter Ausbreitungsweg pro Ohr ueber den Boden (Spiegelquelle an der Ebene "
        "z=0), mit eigener Laufzeit, eigenem Doppler und eigener Daempfung. "
        "Achtung: bei Source Z = 0 liegt die Spiegelquelle exakt auf der echten "
        "Quelle, die Reflexion ist dann nur eine gedaempfte Verdopplung ohne eigene "
        "Laufzeit - hoerbar getrennt wird sie erst, wenn die Quelle ueber dem Boden "
        "liegt. Kostet die doppelte Loeserlast, deshalb standardmaessig aus.");
    addAndMakeVisible (groundReflectionButton);
    groundReflectionAttachment = std::make_unique<ButtonAttachment> (apvts, Params::groundReflectionOn, groundReflectionButton);

    setupKnob (nWaveSizeKnob, apvts, Params::nWaveSize, "N-Wave Size",
               "Groesse/Masse des Koerpers in Metern - sie bestimmt die Dauer der "
               "Druckwelle. Groesser = tiefer und laenger (Verkehrsflugzeug), kleiner = "
               "kuerzer und knackiger (Geschoss). Wirkt nur bei eingeschalteter N-Welle.");

    nWaveButton.setTooltip (
        "Echte N-Wellen-Druckwelle beim Ueberschallknall: steiler Anstieg, Nulldurchgang, "
        "steiler Abfall. Ausgeloest pro Hoerweg in dem Moment, in dem die Mach-Front ihn "
        "ueberstreicht (M_r durchquert 1). Kommt ADDITIV oben auf den normalen Klang, die "
        "bestehende Amplitudenformel bleibt unveraendert. Nicht zu verwechseln mit 'Boom "
        "Limit' (reine Amplitudendeckelung, keine Pulsform) und nicht mit dem Limiter am "
        "Ausgang. Standardmaessig aus.");
    addAndMakeVisible (nWaveButton);
    nWaveAttachment = std::make_unique<ButtonAttachment> (apvts, Params::nWaveOn, nWaveButton);

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
    toggleRow.removeFromLeft (8);
    groundReflectionButton.setBounds (toggleRow.removeFromLeft (140));
    toggleRow.removeFromLeft (8);
    nWaveButton.setBounds (toggleRow.removeFromLeft (100));
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

    // Zweite Reihe: die Geometrie-Achse z und die daran hängende
    // Bodendämpfung.
    area.removeFromTop (6);

    auto geoRow = area.removeFromTop (knobH);
    for (auto* k : { &srcZKnob, &lisZKnob, &groundDampKnob, &nWaveSizeKnob, &distanceCurveKnob })
    {
        layoutKnob (*k, geoRow.removeFromLeft (knobW));
        geoRow.removeFromLeft (4);
    }

    // Dritte Reihe: M-Jitter, eigenstaendig statt in die schon volle
    // Hoehen-Reihe gequetscht.
    area.removeFromTop (6);

    auto jitterRow = area.removeFromTop (knobH);
    for (auto* k : { &srcJitterAmountKnob, &srcJitterRateKnob })
    {
        layoutKnob (*k, jitterRow.removeFromLeft (knobW));
        jitterRow.removeFromLeft (4);
    }
}
