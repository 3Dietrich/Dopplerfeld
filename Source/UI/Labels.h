#pragma once

#include "Tooltips.h"
#include "../Util/Utf8.h"

#include <juce_core/juce_core.h>

#include <cstring>

// Beschriftungen der Bedienelemente in beiden Sprachen (@dpa 20260824:
// "bitte auch alle deutschen Labels in EN mode auf englisch (achte dabei auch
// auf Breitenunterschiede)").
//
// Aufgebaut wie eine Ersetzung, nicht wie ein Schluesselverzeichnis: im Code
// steht weiterhin der DEUTSCHE Text, und diese Tabelle liefert im
// EN-Betrieb seine Entsprechung. Das hat einen praktischen Grund - die
// Aufrufstellen bleiben lesbar ("Knattern" statt Key::RotorSlapLabel), und
// eine fehlende Uebersetzung faellt nicht aus, sondern zeigt eben den
// deutschen Text. Bei rund neunzig Beschriftungen ist das die robustere
// Richtung.
//
// Die Sprache kommt aus Tooltips::currentLanguage(), es gibt also nur EINEN
// Umschalter fuer Hinweise und Beschriftungen.
//
// **Breite:** englische Beschriftungen sind bewusst kurz gehalten. Die
// Reglerzellen sind rund 100 px breit und die Schrift wird nicht kleiner -
// was laenger ist als "Ground Reflection", passt nicht mehr. Deshalb
// "Blade Len" statt "Blade Length" und "Duck Range" statt
// "Duck Range (metres)".
namespace Labels
{
    struct Entry
    {
        const char* de;
        const char* en;
    };

    // Nur Eintraege, bei denen sich etwas aendert. Alles andere (RPM, Gain,
    // Tau, Pitch, Loop, Doppler ...) heisst in beiden Sprachen gleich und
    // steht deshalb gar nicht erst hier.
    inline const Entry* table (int& count)
    {
        static const Entry entries[] =
        {
            // --- Statuszeile ---
            { "Aufnahme",                   "Recording"     },
            { "Wiedergabe",                 "Playback"      },
            { "Zweig-Abriss",               "Branch cut"    },
            { "Pegel dabei",                "level"         },
            { "laut",                       "loud"          },

            // --- Zustandsleiste ---
            { "Sichern",                    "Save"          },
            { "Neu...",                     "New..."        },
            { "Ordner...",                  "Folder..."     },
            { "Abbrechen",                  "Cancel"        },
            { "kein Zustand gewählt",       "no state"      },
            { "Ordner ist leer",            "folder empty"  },
            { "gesichert",                  "saved"         },
            { "konnte nicht schreiben",     "write failed"  },
            { "kein lesbarer Zustand",      "not a state"   },
            { "gerade geladen, noch nicht bereit", "just loaded, not ready" },
            { "Zustand sichern",            "Save state"    },
            { "Name für den neuen Zustand:", "Name for the new state:" },
            { "Ordner mit Zuständen",       "Folder with states" },

            // --- Motor ---
            { "Blätter",        "Blades"      },
            { "Blattlänge",     "Blade Len"   },
            { "Klangfarbe",     "Timbre"      },
            { "Knattern",       "Slap"        },
            { "Druckstoß",      "Shock"       },
            { "Stoßfolge",      "Shock Rate"  },
            { "Stoßlänge",      "Shock Size"  },
            { "Fern-Farbe",     "Far Colour"  },
            { "Spannweite",     "Span"        },
            { "Prop Pegel",     "Prop Level"  },
            { "Pegel",          "Level"       },
            { "Betriebsart",    "Engine Kind" },
            { "Vorlage",        "Voicing"     },
            { "Strahlklang",    "Jet Timbre"  },

            // Auswahllisten. Die Parameter selbst behalten ihre deutschen
            // Eintraege - sie stehen in der Automationsliste des Hosts und
            // duerfen sich nicht mit der Anzeigesprache aendern, sonst waere
            // eine gespeicherte Automation nicht mehr wiederzuerkennen.
            // Uebersetzt wird nur, was die Auswahlfelder ZEIGEN (siehe
            // populateChoices in den Panels).
            { "Düsenantrieb",   "Jet Engine"    },
            { "Raketenantrieb", "Rocket Engine" },
            { "Hubschrauber",   "Helicopter"    },
            { "Frei",           "Free"          },
            { "Nachbrenner",    "Afterburner"   },
            { "Ferne",          "Distant"       },
            { "Breit",          "Wide"          },
            { "Vollschub",      "Full Thrust"   },
            { "Feststoff",      "Solid Fuel"    },
            { "Zündung",        "Ignition"      },
            { "Durch den Bildschirm", "Through the screen" },
            { "Waagerecht querend",   "Crossing level"     },
            { "Kontinuierlich", "Continuous"    },
            { "Knall-Start",    "Bang Start"    },
            { "Brüllen",        "Roar"        },

            // --- Bewegung ---
            { "Jit Tempo",      "Jit Speed"   },
            { "Z-Anteil",       "Z Share"     },
            { "Startvariante",  "Start Mode"  },
            { "Vorbeiflug-Bahn", "Fly-By Path" },
            { "Vorbeiflug",     "Fly-By"      },
            { "Flug stoppen",   "Stop Fly-By" },
            { "Nachlauf",       "Coast"       },
            { "Maus glatt",     "Smooth Mouse" },
            { "Jitter An",      "Jitter On"   },

            // --- Feld / Physik / Ausgang ---
            { "Meereshöhe",     "Altitude"    },
            { "Luft °C",        "Air °C"      },
            { "Rückwärts",      "Reverse"     },
            { "Fahne",          "Trail"       },
            { "Front-Duck",     "Front Duck"  },
            { "Duck-Reichw.",   "Duck Range"  },
            { "Schatten",       "Shadow"      },
            { "Startknall",     "Start Boom"  },
            { "Knall-Länge",    "Boom Size"   },
            { "Knall-Kante",    "Boom Edge"   },
            { "Druckwelle",     "Pressure"    },
            { "N-Welle",        "N-Wave"      },
            { "Bodenreflexion", "Ground Reflection" },

            // --- Waende / Schwarm ---
            { "Wand ",          "Wall "       },
            { "Winkel ",        "Angle "      },
            { "Neigung ",       "Tilt "       },
            { "Mehrfachreflexion", "Multi Reflection" },
            { "Klone",          "Clones"      },
            { "Streuung",       "Spread"      },

            // --- Ueberschriften der Klappen ---
            { "Motorsteuerung",           "Engine Control"        },
            { "Motor",                    "Engine"                },
            { "Bewegung",                 "Motion"                },
            { "Feld / Physik / Ausgang",  "Field / Physics / Out" },
            { "Reflexionen / Waende",     "Reflections / Walls"   },
            { "Schwarm / Klone",          "Swarm / Clones"        },

            { "Entfernung",     "Distance"    },

            // --- Kopfzeile, Sample, Scope ---
            { "An",             "On"          },
            { "Hilfehinweise",  "Tooltips"    },
            { "Zeigen",         "Show"        },
            { "Motor bei Griff", "Engine On Grab" },
            { "Quelle: Motor",  "Source: Engine" },
            { "Quelle: Sample", "Source: Sample" },
            { "Quelle: Audio In", "Source: Audio In" },
            { "(kein Sample geladen)", "(no sample loaded)" },
            { "Speichern",      "Save"        },
            { "Knall",          "Bang"        },
            { "Knall: An",      "Bang: On"    },
            { "Freeze: An",     "Freeze: On"  },
            { "Sync: An",       "Sync: On"    },
            { "Play: An",       "Play: On"    },
            { "Scope ausblenden", "Hide Scope" },
            { "Ansicht: Draufsicht",  "View: Top Down"    },
            { "Ansicht: Perspektive", "View: Perspective" },
            { "nicht mehr zeigen", "don't show again" },
            { "öffne states",   "open states" },
            { "Raum",           "Space"       },

            // --- Hall-Panel ---
            { "Höhe (Z)",       "Height (Z)"  },
            { "LR-Breite",      "L/R Width"   },
            { "Weite",          "Extent"      },
            { "Abkling",        "Decay"       },
            { "Energie",        "Energy"      },
            { "Draußen",        "Outdoors"    },

            // --- Bewegung ---
            { "Fahrtwind",      "Slipstream"  },
            { "Hilfe: alles",   "Help: all"   },
            { "Hilfe: Regler",  "Help: knobs" },
            { "Hilfe: aus",     "Help: off"   },
            { "Rausch v",       "Noise v"     },
            { "Luft",           "Air"         },
            { "Boden",          "Ground"      },
            { "Ausgang",        "Output"      },
        };

        count = (int) (sizeof (entries) / sizeof (entries[0]));
        return entries;
    }

    // Anzeigetext zu einem deutschen Quelltext. Im DE-Betrieb (und fuer alles,
    // was nicht in der Tabelle steht) unveraendert.
    inline juce::String text (const char* german)
    {
        if (german == nullptr)
            return {};

        if (Tooltips::currentLanguage() != Tooltips::Language::En)
            return Text::utf8 (german);

        int count = 0;
        const Entry* entries = table (count);

        for (int i = 0; i < count; ++i)
            if (std::strcmp (entries[i].de, german) == 0)
                return Text::utf8 (entries[i].en);

        return Text::utf8 (german);
    }
}
