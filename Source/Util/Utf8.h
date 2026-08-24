#pragma once

#include <juce_core/juce_core.h>

// Deutsche Beschriftungen mit Umlauten sicher an JUCE uebergeben.
//
// Die Quelldateien dieses Projekts sind UTF-8. juce::String nimmt bei einem
// nackten `const char*` aber CharPointer_ASCII an, also ein Zeichen je Byte -
// aus den zwei UTF-8-Bytes von "ü" (C3 BC) werden dabei die zwei Zeichen "Ã¼".
// Genau das stand als "DÃ¼senantrieb" in der Betriebsart-Auswahl.
//
// utf8() sagt JUCE, was die Bytes wirklich sind. Jede sichtbare Zeichenkette
// mit einem Zeichen jenseits von ASCII muss hier durch, egal ob Umlaut,
// Gradzeichen oder Anfuehrungsstrich.
namespace Text
{
    inline juce::String utf8 (const char* bytes)
    {
        return juce::String (juce::CharPointer_UTF8 (bytes));
    }
}
