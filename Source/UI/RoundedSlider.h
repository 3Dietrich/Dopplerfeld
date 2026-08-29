#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
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
// Ein Regler, der ohnehin nur ganze Zahlen annimmt (Rasterweite 1 oder
// groesser), zeigt gar keine Nachkommastellen - "24,00 Echos" behauptet eine
// Feinheit, die es an diesem Regler nicht gibt. Das ist kein Sonderfall
// einzelner Panels, sondern gehoert in dieselbe Regel: sonst faengt jedes
// Panel an, seine ganzzahligen Regler mit setNumDecimalPlacesToDisplay(0)
// selbst zu behandeln, und die zentrale Regel gilt nicht mehr fuer alle.
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
    //
    // interval ist die Rasterweite des Reglers (juce::Slider::getInterval()).
    // Ohne Angabe gilt die reine Groessenordnungsregel - das ist der Fall fuer
    // Anzeigen ohne eigenen Regler dahinter.
    static int decimalsFor (double value, double interval = 0.0)
    {
        if (isIntegerStep (interval))
            return 0;

        const double a = std::abs (value);

        if (a < 1.0)   return 3;
        if (a < 10.0)  return 2;
        if (a < 100.0) return 1;

        return 0;
    }

    // Raster von genau einer ganzen Zahl (1, 2, ...) heisst: der Regler kennt
    // nur ganze Werte.
    static bool isIntegerStep (double interval)
    {
        return interval >= 1.0 && interval == std::floor (interval);
    }

    // Wert nach der Regel gerundet, ohne Einheit.
    //
    // Formatiert wird ueber printf und NICHT ueber juce::String (double, int):
    // dort bedeutet die Null "kuerzestmoegliche Darstellung", nicht "keine
    // Nachkommastellen". Werte ueber 100 kaemen damit weiterhin voll ausgedruckt
    // heraus (708.301 statt 708) - also genau das, was die Regel verhindern soll.
    static juce::String roundedText (double value, double interval = 0.0)
    {
        return juce::String::formatted ("%.*f", decimalsFor (value, interval), value);
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

        return roundedText (value, getInterval()) + getTextValueSuffix();
    }

    double getValueFromText (const juce::String& text) override
    {
        if (parseText != nullptr)
            return parseText (text);

        return juce::Slider::getValueFromText (text);
    }

    // Pfeiltasten bewegen einen festen Bruchteil des REGLERWEGS, nicht einen
    // festen Wert (@dpa 20260824: "Knobs Auflösung bei up/down arrows sind
    // teilweise viel zu groß").
    //
    // JUCE nimmt fuer die Tastatur das Intervall des Parameters, und wo keines
    // gesetzt ist, ein Hundertstel des Wertebereichs. Bei einem Regler wie Max
    // Speed, der bis 100000 m/s reicht, sind das 1000 m/s je Tastendruck -
    // unbrauchbar, egal wo man gerade steht. Bei einem Regler von 0 bis 1
    // waeren dieselben zwei Prozent dagegen viel zu fein.
    //
    // Der Reglerweg ist das richtige Mass, weil er bereits alles enthaelt, was
    // die Bedienung ausmacht: Bereich UND Kennlinie. Ein Schritt ist damit
    // ueberall gleich gross, wo er sich anfuehlt wie derselbe Schritt - im
    // krummen unteren Ende einer Skew-Kurve genauso wie am linearen oberen.
    //
    // Ein halbes Prozent Weg je Druck: zweihundert Schritte von Anschlag zu
    // Anschlag. Mit Umschalt ein Zehntel davon, fuer das letzte Feintuning.
    //
    // Am Raster eines Reglers endet dieses Mass allerdings. Ein halbes Prozent
    // des Weges sind bei den Echos (2 bis 48 Rueckwuerfe, Raster 1) knapp ein
    // Viertel Rueckwurf; JUCE rastet den Wert danach wieder auf denselben ein,
    // und der Regler ruehrt sich ueberhaupt nicht - genau daran waren die
    // ganzzahligen Regler mit den Pfeiltasten nicht zu bedienen. Bleibt der
    // Wert stehen, geht es deshalb um GENAU EIN Raster weiter.
    bool keyPressed (const juce::KeyPress& key) override
    {
        const bool up   = key.isKeyCode (juce::KeyPress::upKey)   || key.isKeyCode (juce::KeyPress::rightKey);
        const bool down = key.isKeyCode (juce::KeyPress::downKey) || key.isKeyCode (juce::KeyPress::leftKey);

        if (! up && ! down)
            return juce::Slider::keyPressed (key);

        const double step = key.getModifiers().isShiftDown() ? 0.0005 : 0.005;

        const auto   range = getNormalisableRange();
        const double value = getValue();
        const double here  = range.convertTo0to1 (value);
        const double next  = juce::jlimit (0.0, 1.0, here + (up ? step : -step));

        double target = range.convertFrom0to1 (next);

        if (range.interval > 0.0
            && std::abs (range.snapToLegalValue (target) - range.snapToLegalValue (value))
                   < range.interval * 0.5)
        {
            target = value + (up ? range.interval : -range.interval);
        }

        setValue (target, juce::sendNotificationSync);

        return true;
    }

    // Mausrad wird am Regler NICHT verarbeitet (@dpa 20260820): das Fenster ist hoch und
    // wird staendig gescrollt. Geraet dabei ein Regler unter den Mauszeiger, wuerde er
    // sonst das Rad schlucken - das Scrollen braeche ab UND der Reglerwert aenderte sich
    // dabei unbemerkt. Das waere Datenverlust im laufenden Betrieb, also gilt im Normalfall:
    // das Rad geht immer an die Elternkomponente (den scrollenden Viewport) weiter.
    //
    // Ausnahme bewusst nur mit gehaltener Cmd-Taste (unter Windows/Linux von JUCE auf Ctrl
    // gemappt, siehe ModifierKeys::isCommandDown): das kann beim normalen Ueber-die-Seite-
    // Scrollen nie versehentlich passieren. Eine Fokus-Regel ("nur wenn der Regler gerade
    // den Tastaturfokus hat") waere die Alternative gewesen, ist aber leichter zu vergessen
    // (letzter Klick/Tab-Reihenfolge unklar) - die Modifiertaste ueberrascht am wenigsten,
    // weil sie in dem Moment aktiv gehalten werden muss, in dem man den Wert wirklich per
    // Rad aendern will.
    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        if (event.mods.isCommandDown())
        {
            juce::Slider::mouseWheelMove (event, wheel);
            return;
        }

        // Nicht juce::Slider::mouseWheelMove aufrufen (die wuerde bei aktivem
        // Scroll-Rad selbst reagieren) - direkt Component::mouseWheelMove, das reicht
        // das Ereignis unveraendert an die naechste aktivierte Elternkomponente weiter.
        juce::Component::mouseWheelMove (event, wheel);
    }

private:
    // Eigenes, schlankeres/kontrastreicheres Aussehen fuer den Rotary-Ring (@dpa 20260820):
    // "nicht so baby-pummelig-fett auf kleinem Platz, sondern bisschen schlanker, mehr
    // Kontrast - genauso klein". JUCEs Standard-LookAndFeel_V4 zeichnet den Ring dick und
    // mit wenig Kontrast zwischen Hintergrund- und Wert-Bogen. Groesse bleibt unveraendert,
    // nur Strichstaerke und Farben aendern sich - deshalb eigene LookAndFeel statt eigenem
    // paint(): die Layout-Berechnung (Rotary-Flaeche vs. Textfeld-Platz) bleibt so exakt
    // die von JUCE, nur das eigentliche Zeichnen wird ersetzt.
    //
    // Instanz pro Regler (kein static/Singleton): so bleibt die Lebensdauer eindeutig an
    // den Regler gebunden, ohne Fragen zur Initialisierungsreihenfolge ueber mehrere
    // .cpp-Dateien hinweg, die diesen Header einbinden.
    struct SlimRotaryLookAndFeel : public juce::LookAndFeel_V4
    {
        void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle,
                                juce::Slider&) override
        {
            const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (2.0f);
            const auto radius  = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
            const auto centre  = bounds.getCentre();
            const auto angle   = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

            // Strichstaerke fest duenn und an den kleinen Regler-Radius gekoppelt, statt
            // JUCEs Default (LookAndFeel_V4 zeichnet ohne eigenen Wert mit ca. der halben
            // Radius-Breite - dick und weich).
            const float lineW     = juce::jmin (2.2f, radius * 0.18f);
            const float arcRadius = radius - lineW * 0.5f;

            // Hintergrund-Bogen deutlich gedaempft (dunkel, niedrige Deckkraft), statt
            // JUCEs mittelhellem Grau mit wenig Abstand zur Wert-Farbe (JUCE-Default
            // rotarySliderOutlineColourId) - damit der Wert-Bogen klar hervorsticht statt
            // im Ring zu verschwimmen.
            juce::Path backgroundArc;
            backgroundArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                          rotaryStartAngle, rotaryEndAngle, true);
            g.setColour (juce::Colours::white.withAlpha (0.16f));
            g.strokePath (backgroundArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // Wert-Bogen kraeftiges Weiss mit hoher Deckkraft, statt JUCEs blassem
            // Blau/Grau nahe an der Hintergrundfarbe - der Kontrast zum gedaempften
            // Hintergrund-Bogen ist der eigentliche Hebel gegen den "blassen Klumpen".
            if (sliderPosProportional > 0.0f)
            {
                juce::Path valueArc;
                valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                         rotaryStartAngle, angle, true);
                g.setColour (juce::Colours::white.withAlpha (0.95f));
                g.strokePath (valueArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            // Zeiger eine duenne Linie von der Mitte zum Rand statt eines dicken, runden
            // Punkts (thumb) - passt zum schlankeren Gesamtbild, ohne einen zusaetzlichen
            // fetten Klecks in der Mitte.
            juce::Path pointer;
            const float pointerLen = radius * 0.62f;
            pointer.startNewSubPath (centre.x, centre.y);
            pointer.lineTo (centre.x + pointerLen * std::sin (angle), centre.y - pointerLen * std::cos (angle));
            g.setColour (juce::Colours::white.withAlpha (0.95f));
            g.strokePath (pointer, juce::PathStrokeType (lineW * 0.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    };

    SlimRotaryLookAndFeel slimRotaryLookAndFeel;

public:
    RoundedSlider()
    {
        // Eigene LookAndFeel nur fuer diesen einen Regler setzen (siehe Begruendung oben
        // bei SlimRotaryLookAndFeel) - im Destruktor wieder loesen, sonst haelt JUCE einen
        // Zeiger auf ein bereits zerstoertes Objekt.
        setLookAndFeel (&slimRotaryLookAndFeel);
    }

    ~RoundedSlider() override
    {
        setLookAndFeel (nullptr);
    }
};
