#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Slider mit wertabhaengig gerundeter Anzeige (@dpa 20260819): der
// gespeicherte/automatisierte Wert bleibt in voller Praezision, nur der
// Text im Textfeld rundet je nach Groessenordnung unterschiedlich fein.
//
// @dpa: "Anzeigen mit sauvielen nullen (1.00000000) oder gar darunter
// (0.9999997) ist fuer eine 'Anzeige' Gift". Vier Gruende, alle praktisch:
// man muss zu viel lesen, um die Zahl zu erfassen; der Blick muss ueber
// mehrere Stellen wandern; eine Einheit dahinter springt mit der wechselnden
// Zahlenlaenge mit; und bei jeder Aenderung faengt das Lesen von vorn an.
//
// Die Regel gilt fuer ALLE Regler, nicht nur fuer einzelne.
//
//   Bereich        Nachkommastellen
//   |Wert| < 1      3
//   |Wert| < 10     2
//   |Wert| < 100    1
//   sonst           0
//
// juce::Slider bietet dafuer keinen std::function-Hook, deshalb die
// Unterklasse: getTextFromValue() ist genau dafuer vorgesehen (virtuell,
// JUCE ruft sie fuers Textfeld).
//
// Wer eine eigene Darstellung braucht, setzt weiterhin textFromValueFunction
// bzw. valueFromTextFunction wie bei jedem juce::Slider - die haben Vorrang.
// Das ist der Weg, ueber den die Tempo-Regler ihre Einheit umschalten (siehe
// MotionPanel::setSpeedUnit): eine ueberschriebene virtuelle Methode setzt
// diese Hooks sonst still ausser Kraft, weil JUCE sie nur in der Basisfassung
// abfragt.
class RoundedSlider : public juce::Slider
{
public:
    // Die Rundungsregel als eigene Funktion, damit jede Wertanzeige dieselbe
    // benutzt - auch die, die ihren Text ueber textFromValueFunction selbst
    // bilden (etwa die Tempo-Regler, die zusaetzlich die Einheit umrechnen).
    // Waere sie nur hier unten eingebaut, faenden solche Anzeigen an, eigene
    // Stellenzahlen zu erfinden.
    static int decimalsFor (double value)
    {
        const double a = std::abs (value);

        if (a < 1.0)   return 3;
        if (a < 10.0)  return 2;
        if (a < 100.0) return 1;

        return 0;
    }

    // Wert nach der Regel gerundet, ohne Einheit.
    //
    // Formatiert wird ueber printf und NICHT ueber juce::String (double, int):
    // dort bedeutet die Null "kuerzestmoegliche Darstellung", nicht "keine
    // Nachkommastellen". Werte ueber 100 kaemen damit weiterhin voll ausgedruckt
    // heraus (708.301 statt 708) - also genau das, was die Regel verhindern soll.
    static juce::String roundedText (double value)
    {
        return juce::String::formatted ("%.*f", decimalsFor (value), value);
    }

    // Eigene Darstellung, wenn ein Regler mehr braucht als Wert plus festes
    // Suffix - etwa die Tempo-Regler, die zusaetzlich die Einheit umrechnen.
    //
    // Ausdruecklich NICHT ueber textFromValueFunction: die setzt die
    // SliderAttachment beim Verbinden selbst (juce_ParameterAttachments.cpp),
    // und sie liefert den Parameterwert in voller Praezision. Wer sie
    // vorgehen laesst, schaltet damit die Stellenregel fuer JEDEN Regler ab,
    // der an einem Parameter haengt - also fuer alle.
    std::function<juce::String (double)>       displayText;
    std::function<double (const juce::String&)> parseText;

    juce::String getTextFromValue (double value) override
    {
        if (displayText != nullptr)
            return displayText (value);

        return roundedText (value) + getTextValueSuffix();
    }

    double getValueFromText (const juce::String& text) override
    {
        if (parseText != nullptr)
            return parseText (text);

        return juce::Slider::getValueFromText (text);
    }
};
