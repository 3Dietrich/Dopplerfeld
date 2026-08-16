#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// TooltipWindow mit globalem An/Aus-Schalter (@dpa-Feedback: Hilfehinweise
// müssen in den Einstellungen abschaltbar sein). getTipFor() ist genau für
// so etwas vorgesehen (virtuell, JUCE ruft sie pro Hover auf) - bei
// enabled=false bleibt der Text leer, TooltipWindow zeigt dann nichts.
class ToggleableTooltipWindow : public juce::TooltipWindow
{
public:
    using juce::TooltipWindow::TooltipWindow;

    bool enabled = true;

    juce::String getTipFor (juce::Component& c) override
    {
        return enabled ? juce::TooltipWindow::getTipFor (c) : juce::String();
    }
};
