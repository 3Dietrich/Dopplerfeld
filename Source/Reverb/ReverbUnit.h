#pragma once

// Gemeinsame Schnittstelle aller Hallbauarten.
//
// JUCE-frei wie die Physics-Schicht, und aus demselben Grund: der Hall laesst
// sich damit offline im reverb_probe messen und anhoeren, ohne dass ein Plugin
// gebaut werden muss.
//
// Mono rein, Stereo raus. Was an einem Abgriffpunkt im Feld ankommt, ist ein
// einzelner Punkt und damit mono; die Breite entsteht erst im Hall selbst,
// durch verschieden lange Wege nach links und rechts. Geregelt wird sie danach
// im TapBus, nicht hier.
//
// Die Stellwerte sind bewusst physikalisch benannt statt als 0..1-Regler: eine
// Landschaftsgeometrie kann einen Abstand in Metern und eine Abklingzeit in
// Sekunden ausrechnen, einen abstrakten "Size"-Regler nicht.
class ReverbUnit
{
public:
    virtual ~ReverbUnit() = default;

    // capacityMetres ist der groesste Raum, den die Puffer nach diesem Aufruf
    // tragen koennen. Er bemisst den Speicher und wird ausserhalb des
    // Audiothreads nachgezogen, wenn der Regler darueber hinausgeht (siehe
    // reverbparts::capacityFor).
    virtual void prepare (double sampleRate, int maxBlock, double capacityMetres) = 0;
    virtual void reset() = 0;

    // Schreibt nach outL/outR, addiert nicht. Der Aufrufer mischt selbst zu,
    // weil nur er Pegel und Breite kennt.
    //
    // Mono ist der Normalfall: was an einem Abgriffpunkt im Feld ankommt, ist
    // ein Punkt. Stereo braucht nur die Kette (siehe TapState::chainTo im
    // DopplerEngine), wo eine Bauart in die naechste geht - dort waere eine
    // Mono-Summe ein Verlust: die beiden Seiten einer Hallbauart sind
    // absichtlich unkorreliert, und ihre Summe loescht sich stellenweise aus.
    //
    // Jede Bauart setzt NUR die Stereofassung um; die Mono-Fassung ist ihr
    // Sonderfall mit inL == inR und liefert deshalb weiterhin Zeichen fuer
    // Zeichen dasselbe wie vorher.
    void process (const float* in, float* outL, float* outR, int numSamples)
    {
        processStereo (in, in, outL, outR, numSamples);
    }

    virtual void processStereo (const float* inL, const float* inR,
                                float* outL, float* outR, int numSamples) = 0;

    // Kantenlaenge des gedachten Raums in Metern. Bestimmt die Laufzeiten im
    // Netz, also Echodichte und Faerbung.
    virtual void setRoomSize (double metres) = 0;

    // Zeit bis -60 dB, in Sekunden.
    virtual void setDecaySeconds (double seconds) = 0;

    // Hoehenverlust je Umlauf. 0 = keiner, 1 = fast alles oberhalb einiger
    // hundert Hertz ist nach wenigen Umlaeufen weg. Dieselbe Bedeutung wie die
    // Flaechendaempfung in DopplerEngine, damit ein Tal spaeter beide aus
    // derselben Zahl speisen kann.
    virtual void setDamping (double amount01) = 0;

    // Grober Preis eines Durchlaufs als Vielfaches der billigsten Bauart. Nur
    // fuer Anzeige und Voreinstellung gedacht; was es wirklich kostet, misst
    // der reverb_probe auf der Maschine, auf der es laufen soll.
    virtual double relativeCost() const = 0;

    virtual const char* name() const = 0;
};
