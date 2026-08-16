#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Slider mit wertabhaengig gerundeter Anzeige (@dpa-Feedback): der
// gespeicherte/automatisierte Wert bleibt in voller Praezision, nur der
// Text im Textfeld rundet je nach Groessenordnung unterschiedlich fein -
// sonst stehen bei kleinen Werten (z.B. Track 0..1) sinnlose Nachkommaketten,
// bei grossen Werten (z.B. RPM) dagegen unnoetig viele Stellen.
//
//   Bereich        Nachkommastellen
//   |Wert| < 1      3
//   |Wert| < 10     2
//   |Wert| < 100    1
//   sonst           0
//
// juce::Slider bietet dafuer keinen std::function-Hook, deshalb die
// Unterklasse: getTextFromValue() ist genau dafuer vorgesehen (virtuell,
// JUCE ruft sie fuers Textfeld). getValueFromText() bleibt Standard (JUCE
// parst die eingetippte Zahl unveraendert).
class RoundedSlider : public juce::Slider
{
public:
    juce::String getTextFromValue (double value) override
    {
        const double a = std::abs (value);
        int decimals = 0;
        if (a < 1.0)        decimals = 3;
        else if (a < 10.0)  decimals = 2;
        else if (a < 100.0) decimals = 1;

        return juce::String (value, decimals) + getTextValueSuffix();
    }
};
