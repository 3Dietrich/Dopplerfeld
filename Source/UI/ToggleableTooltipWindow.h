#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>

// TooltipWindow mit globalem An/Aus-Schalter (@dpa-Feedback: Hilfehinweise
// müssen in den Einstellungen abschaltbar sein). getTipFor() ist genau für
// so etwas vorgesehen (virtuell, JUCE ruft sie pro Hover auf) - bei
// enabled=false bleibt der Text leer, TooltipWindow zeigt dann nichts.
class ToggleableTooltipWindow : public juce::TooltipWindow
{
public:
    using juce::TooltipWindow::TooltipWindow;

    bool enabled = true;

    // Zweite Stufe: Hinweise auf den grossen ANZEIGEN - Feld und Scope - lassen
    // sich einzeln abschalten (@dpa 20260830). An einem Regler steht der
    // Hinweis neben etwas, das man ohnehin nicht beim Ablesen braucht; auf
    // einer Anzeige deckt er genau das zu, worauf man schaut, und er kommt
    // nach jeder Mausbewegung erneut.
    bool enabledOnDisplays = true;

    // Sagt, ob eine Komponente zu einer solchen Anzeige gehoert. Wird vom
    // Editor gesetzt, damit dieser Header die Anzeigeklassen nicht kennen muss.
    std::function<bool (juce::Component&)> isDisplay;

    juce::String getTipFor (juce::Component& c) override
    {
        if (! enabled)
            return {};

        if (! enabledOnDisplays && isDisplay != nullptr && isDisplay (c))
            return {};

        return juce::TooltipWindow::getTipFor (c);
    }
};
