#pragma once

#include "Params.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

// Der Hall-Bypass stellt den Direktschall auf 0 dB und beim Ausschalten den
// vorherigen Wert wieder her.
//
// Grund: beim Einstellen eines Halls dreht man den Direktschall herunter, um
// nur den Raum zu hoeren - oft bis -60 dB, was hier als stumm gilt. Der
// Bypass soll den trockenen Vergleich liefern; ohne diese Kopplung liefert er
// in genau dieser Lage Stille, und das sieht nach einem defekten Plugin aus.
//
// Der Rueckweg wird gemerkt, sonst waere der alte Wert nach einmal Bypass
// verloren. Er verfaellt, sobald jemand SELBST am Direktschall dreht: dann
// gilt der neue Wert, und ein spaeteres Ausschalten des Bypasses darf ihn
// nicht wieder ueberschreiben.
//
// Geschrieben wird ausschliesslich im Nachrichtenthread (AsyncUpdater). Ein
// Parameter kann aus dem Audiothread heraus umgestellt werden - Automation
// tut genau das -, und beginChangeGesture/setValueNotifyingHost rufen den
// Host zurueck; das gehoert dort nicht hin.
class BypassDirectLink : private juce::AudioProcessorValueTreeState::Listener,
                         private juce::AsyncUpdater
{
public:
    explicit BypassDirectLink (juce::AudioProcessorValueTreeState& s)
        : state (s)
    {
        state.addParameterListener (Params::reverbBypass, this);
        state.addParameterListener (Params::directGain,   this);

        if (const auto* p = state.getRawParameterValue (Params::reverbBypass))
            bypassed.store (p->load() > 0.5f);
    }

    ~BypassDirectLink() override
    {
        state.removeParameterListener (Params::reverbBypass, this);
        state.removeParameterListener (Params::directGain,   this);

        cancelPendingUpdate();
    }

    // Wert, auf den der Bypass den Direktschall stellt: voller Pegel, also
    // genau das, was ein umgangenes Plugin durchreicht.
    static constexpr float bypassDirectDb = 0.0f;

    // Fuer Tests: dort dreht sich keine Nachrichtenschleife, die ausstehende
    // Aenderung bliebe sonst liegen.
    void flushPendingForTest() { handleUpdateNowIfNeeded(); }

private:
    void parameterChanged (const juce::String& id, float newValue) override
    {
        if (id == Params::directGain)
        {
            // Nur eine fremde Aenderung loescht den Rueckweg. Die eigene
            // wird am zuletzt geschriebenen Wert erkannt und nicht am
            // Schreib-Flag allein: ob der Rueckruf noch waehrend des
            // Schreibens kommt oder erst danach, haengt am Host.
            if (! writing.load() && std::abs (newValue - lastWritten.load()) > 1.0e-4f)
                hasRemembered.store (false);

            return;
        }

        const bool on = newValue > 0.5f;

        if (bypassed.exchange (on) == on)
            return;

        pending.store (on ? 1 : 2);
        triggerAsyncUpdate();
    }

    void handleAsyncUpdate() override
    {
        const int action = pending.exchange (0);

        auto* direct = state.getParameter (Params::directGain);

        if (action == 0 || direct == nullptr)
            return;

        if (action == 1)
        {
            remembered.store (direct->convertFrom0to1 (direct->getValue()));
            hasRemembered.store (true);

            write (*direct, bypassDirectDb);
        }
        else if (hasRemembered.exchange (false))
        {
            write (*direct, remembered.load());
        }
    }

    void write (juce::RangedAudioParameter& p, float valueInDb)
    {
        lastWritten.store (valueInDb);
        writing.store (true);

        p.beginChangeGesture();
        p.setValueNotifyingHost (p.convertTo0to1 (valueInDb));
        p.endChangeGesture();

        writing.store (false);
    }

    juce::AudioProcessorValueTreeState& state;

    std::atomic<bool>  bypassed      { false };
    std::atomic<bool>  hasRemembered { false };
    std::atomic<bool>  writing       { false };
    std::atomic<float> remembered    { 0.0f };
    std::atomic<float> lastWritten   { 0.0f };
    std::atomic<int>   pending       { 0 };   // 1 = eingeschaltet, 2 = ausgeschaltet
};
