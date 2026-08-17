#include "PluginEditor.h"
#include "Params.h"

#include <cmath>

DopplerfeldEditor::DopplerfeldEditor (DopplerfeldProcessor& p)
    : AudioProcessorEditor (&p),
      dopplerfeldProcessor (p),
      enginePanel (p.apvts),
      samplePanel (p.apvts),
      motionPanel (p.apvts),
      fieldPanel  (p.apvts),
      wallPanel   (p.apvts),
      swarmPanel  (p.apvts)
{
    addAndMakeVisible (field);

    // Ziehen im Feld schreibt auf dieselben Parameter, die auch der Host
    // automatisiert - der Weg in die Physik ist für Maus und Automation
    // derselbe (Plan 3.12: der GUI-Thread schreibt nur Ziele).
    field.onSourceDragged = [this] (double normX, double normY)
    {
        setParameter (Params::srcX, normX);
        setParameter (Params::srcY, normY);
    };

    field.onListenerDragged = [this] (double normX, double normY)
    {
        setParameter (Params::lisX, normX);
        setParameter (Params::lisY, normY);
    };

    // Nur in der perspektivischen Ansicht: dort ist Ziehen nach oben "hoeher".
    // Das ist der einzige Weg, die Hoehe mit der Maus zu setzen - die
    // Draufsicht hat dafuer keine Achse.
    field.onSourceHeightDragged = [this] (double metres)
    {
        setParameter (Params::srcZ, metres);
    };

    field.onListenerRotated = [this] (double yawRadians)
    {
        setParameter (Params::lisYaw, juce::radiansToDegrees (yawRadians));
    };

    enginePanelBox.setContent (&enginePanel);
    samplePanelBox.setContent (&samplePanel);
    motionPanelBox.setContent (&motionPanel);
    fieldPanelBox.setContent (&fieldPanel);
    wallPanelBox.setContent (&wallPanel);
    swarmPanelBox.setContent (&swarmPanel);

    // Motor aufgeklappt (die Default-Quelle), der Rest zugeklappt - sonst
    // steht die Spalte beim Öffnen sofort voll.
    samplePanelBox.setExpanded (false);
    motionPanelBox.setExpanded (false);
    fieldPanelBox.setExpanded (true);
    wallPanelBox.setExpanded (false);
    swarmPanelBox.setExpanded (false);

    for (auto* box : { &enginePanelBox, &samplePanelBox, &motionPanelBox, &fieldPanelBox,
                       &wallPanelBox, &swarmPanelBox })
    {
        box->onExpandedChanged = [this] { layoutPanels(); };
        panelHolder.addAndMakeVisible (box);
    }

    panelViewport.setViewedComponent (&panelHolder, false);
    panelViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (panelViewport);

    samplePanel.onFileSelected = [this] (const juce::File& file)
    {
        dopplerfeldProcessor.loadSampleFile (file);
    };

    // Notaus: zurück auf die minimale sichere Konfiguration - nur der
    // Direktpfad pro Ohr, keine Reflexionen, keine Klone.
    //
    // Zwei Wege, absichtlich beide: der Processor schaltet im Audiothread
    // sofort ab (das ist der Knopf für den Fall, dass es gerade klemmt und der
    // Message-Thread nicht durchkommt), und zusätzlich werden die Parameter
    // zurückgesetzt, damit die Schalter zeigen, was passiert ist, der Host es
    // mitbekommt und es im gespeicherten Zustand steht.
    swarmPanel.onPanic = [this]
    {
        dopplerfeldProcessor.panicToMinimal();

        setParameter (Params::groundReflectionOn, 0.0);
        setParameter (Params::wall1On, 0.0);
        setParameter (Params::wall2On, 0.0);
        setParameter (Params::reflect2ndOn, 0.0);
        setParameter (Params::cloneTotal, 0.0);
    };

    motionPanel.onFlyClicked = [this]
    {
        if (dopplerfeldProcessor.isFlyingBy())
            dopplerfeldProcessor.stopFlyBy();
        else
            dopplerfeldProcessor.triggerFlyBy();
    };

    motionPanel.onRecordClicked = [this] { dopplerfeldProcessor.toggleRecording(); };
    motionPanel.onPlayClicked   = [this]
    {
        if (dopplerfeldProcessor.isPlayingMotion())
            dopplerfeldProcessor.stopPlayback();
        else
            dopplerfeldProcessor.triggerPlayback();
    };

    sourceButton.setTooltip ("Klangquelle umschalten: Motor-Generator oder geladenes Sample.");
    sourceButton.onClick = [this]
    {
        dopplerfeldProcessor.selectSampleSource (! dopplerfeldProcessor.isUsingSampleSource());
    };
    addAndMakeVisible (sourceButton);

    field.setTooltip ("Ziehen an M verschiebt die Schallquelle. Ziehen am Kopf verschiebt "
                      "den Hoerer, Ziehen an der Nase dreht ihn.");

    // @dpa-Feedback: Hilfehinweise abschaltbar. Start an, weil neue Regler
    // ohne Erklaerung sonst raten heisst.
    viewButton.setTooltip ("Zwischen Draufsicht und perspektivischem Blick in die Tiefe "
                           "umschalten. Die perspektivische Ansicht zeigt die Hoehe z, die in "
                           "der Draufsicht gar nicht vorkommt - und in ihr laesst sich die "
                           "Quellhoehe auch mit der Maus ziehen (waagerecht = Seite, "
                           "senkrecht = Hoehe, die Tiefe bleibt).");
    viewButton.onClick = [this]
    {
        const bool toPerspective = field.getViewMode() == FieldComponent::ViewMode::TopDown;

        field.setViewMode (toPerspective ? FieldComponent::ViewMode::Perspective
                                         : FieldComponent::ViewMode::TopDown);

        viewButton.setButtonText (toPerspective ? "Ansicht: Perspektive" : "Ansicht: Draufsicht");
    };
    viewButton.setButtonText ("Ansicht: Draufsicht");
    addAndMakeVisible (viewButton);

    speedUnitButton.setTooltip ("Tempo-Einheit fuer die Statuszeile umschalten "
                                "(km/h, m/s, Mach).");
    speedUnitButton.setButtonText ("km/h");
    speedUnitButton.onClick = [this]
    {
        speedUnit = (speedUnit == SpeedUnit::KmH) ? SpeedUnit::Ms
                  : (speedUnit == SpeedUnit::Ms)   ? SpeedUnit::Mach
                                                    : SpeedUnit::KmH;
        speedUnitButton.setButtonText (speedUnit == SpeedUnit::KmH ? "km/h"
                                      : speedUnit == SpeedUnit::Ms  ? "m/s"
                                                                    : "Mach");
    };
    addAndMakeVisible (speedUnitButton);

    engineResetButton.setTooltip ("Audiomotor neu anlassen: kompletter prepareToPlay()-"
                                  "Durchlauf wie bei einem Wechsel der Audio-Puffergroesse "
                                  "(Klangquelle, Ausbreitungswege und beide Positions-"
                                  "glaetter neu aufgesetzt), falls nach einer CPU-Spitze "
                                  "kein Ton mehr kommt. Haelt processBlock() kurz an, "
                                  "kein Datenrennen mit dem Audiothread.");
    engineResetButton.setButtonText ("Engine Restart");
    engineResetButton.setColour (juce::TextButton::buttonColourId,
                                 juce::Colours::orangered.withAlpha (0.35f));
    engineResetButton.onClick = [this] { dopplerfeldProcessor.restartEngine(); };
    addAndMakeVisible (engineResetButton);

    tooltipsButton.setToggleState (true, juce::dontSendNotification);
    tooltipsButton.setTooltip ("Hilfehinweise beim Ueberfahren der Regler ein-/ausblenden.");
    tooltipsButton.onClick = [this] { tooltipWindow.enabled = tooltipsButton.getToggleState(); };
    addAndMakeVisible (tooltipsButton);

    setSize (margin * 2 + fieldWidth + margin + panelColumnWidth,
             margin * 2 + topBarHeight + 6 + fieldHeight + statusHeight);

    // 30 Hz: schnell genug, dass eine gezogene Quelle nicht ruckelt, und
    // langsam genug, dass die Wellenfronten nicht flimmern.
    startTimerHz (30);
}

void DopplerfeldEditor::setParameter (const char* paramID, double value)
{
    if (auto* parameter = dopplerfeldProcessor.apvts.getParameter (paramID))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) value));
}

void DopplerfeldEditor::timerCallback()
{
    refreshDisplay();
}

void DopplerfeldEditor::refreshDisplay()
{
    dopplerfeldProcessor.fillFieldSnapshot (snapshot);

    field.setFieldMetres ((double) *dopplerfeldProcessor.apvts.getRawParameterValue (Params::fieldMetres));
    field.setSnapshot (snapshot);

    sourceButton.setButtonText (dopplerfeldProcessor.isUsingSampleSource() ? "Quelle: Sample"
                                                                           : "Quelle: Motor");
    motionPanel.setPlaying (dopplerfeldProcessor.isPlayingMotion());
    motionPanel.setFlying (dopplerfeldProcessor.isFlyingBy());

    swarmPanel.setLoad (dopplerfeldProcessor.cpuLoadPercent(),
                        dopplerfeldProcessor.realCloneCount(),
                        dopplerfeldProcessor.cheapCloneCount());

    // 30Hz-Timer = ~33ms zwischen zwei Aufrufen (siehe startTimerHz weiter
    // unten) - fest verdrahtet statt gemessen, das Levelmeter braucht nur
    // eine grobe Zeitbasis für Decay/Clip-Halt, keine exakte.
    fieldPanel.pushLevels (dopplerfeldProcessor.consumeOutputPeakL(),
                           dopplerfeldProcessor.consumeOutputPeakR(),
                           1000.0 / 30.0);

    // Statuszeile neu zeichnen, nicht das ganze Fenster - die Panels darüber
    // ändern sich nur bei Bedienung.
    repaint (margin, getHeight() - statusHeight, fieldWidth, statusHeight);
}

juce::String DopplerfeldEditor::statusText() const
{
    juce::String text;

    // Feste Breite pro Zahl (printf-Padding), zusammen mit dem Monospace-Font
    // in paint(): bei jedem Timer-Tick ändern sich diese Werte, ohne feste
    // Breite verschiebt eine kürzer werdende Zahl (z.B. "9.3" -> "-9.3")
    // allen nachfolgenden Text um ein wechselndes Stück - die ganze Zeile
    // "wackelt". Mit fester Zeichenbreite bleiben Spalten stehen.
    text << "M  x " << juce::String::formatted ("%7.1f", snapshot.sourcePos.x)
         << " m   y " << juce::String::formatted ("%7.1f", snapshot.sourcePos.y) << " m";

    // @dpa-Feedback: Tempo der Quelle, Einheit per speedUnitButton umschaltbar.
    // Mach kommt aus derselben Momentangeschwindigkeit, nicht aus M_r (das ist
    // radial zum jeweiligen Ohr, hier geht es um die Quelle selbst).
    {
        double value = 0.0;
        const char* unit = "";

        switch (speedUnit)
        {
            case SpeedUnit::KmH:  value = snapshot.sourceSpeed * 3.6;  unit = "km/h"; break;
            case SpeedUnit::Ms:   value = snapshot.sourceSpeed;        unit = "m/s";  break;
            case SpeedUnit::Mach: value = snapshot.sourceSpeed
                                          / juce::jmax (1.0, snapshot.speedOfSound);
                                   unit = "Mach"; break;
        }

        text << "   " << juce::String::formatted ("%7.1f", value) << " " << unit;
    }

    // @dpa-Feedback: L-M-Abstand immer sichtbar, nicht nur bei Vorbeiflug.
    text << "   L-M " << juce::String::formatted ("%7.1f", (snapshot.sourcePos - snapshot.listener.head).length()) << " m";

    // @dpa-Feedback: CPU-Echtzeit-Anzeige (Wanduhrzeit/Audiozeit, geglättet -
    // siehe cpuLoadPercent()). Über 100% färbt paint() die ganze Statuszeile
    // rot (siehe dort) - reiner Text reicht hier, kein eigener Meter nötig.
    const float cpu = dopplerfeldProcessor.cpuLoadPercent();
    text << "      CPU " << juce::String::formatted ("%4.0f", (double) cpu) << " %"
         << " (Physik " << juce::String::formatted ("%4.0f", (double) dopplerfeldProcessor.cpuLoadPhysicsPercent()) << "%"
         << " / Quelle " << juce::String::formatted ("%4.0f", (double) dopplerfeldProcessor.cpuLoadSourcePercent()) << "%)";

    // Nur Direktschall und einfache Reflexionen einzeln auflisten. Mit beiden
    // Wänden, Boden und Mehrfachreflexion wären es zwanzig Pfade - die Zeile
    // wäre unlesbar und der Nutzen null, weil die Zweitordnungs-Pfade
    // paarweise dasselbe erzählen. Ihre Anzahl steht stattdessen als Zahl
    // dahinter, damit sichtbar bleibt, was gerade gerechnet wird.
    int higherOrder = 0;

    for (int i = 0; i < snapshot.pathCount; ++i)
    {
        const auto& info = snapshot.paths[(size_t) i];

        if (info.order > 1)
        {
            ++higherOrder;
            continue;
        }

        // Spiegelpfade sind an der Fläche markiert, aus der sie kommen (Boden
        // als ', Wände durchnummeriert) - sonst stünden bei eingeschalteten
        // Reflexionen mehrere gleich aussehende Blöcke da.
        const char* surfaceMark = " ";

        switch (info.order == 0 ? 0 : info.surface)
        {
            case 0:  surfaceMark = " "; break;   // Direktschall
            case 1:  surfaceMark = "'"; break;   // Boden
            case 2:  surfaceMark = "1"; break;   // Wand 1
            default: surfaceMark = "2"; break;   // Wand 2
        }

        text << "      " << (info.ear == 0 ? "L" : "R") << surfaceMark
             << " " << juce::String::formatted ("%7.1f", info.delaySeconds * 1000.0) << " ms"
             << "  M_r " << juce::String::formatted ("%5.2f", info.machRadial)
             << "  Zweige " << juce::String::formatted ("%2d", info.activeBranches);
    }

    if (higherOrder > 0)
        text << "      +" << higherOrder << " Mehrfachrefl.";

    if (dopplerfeldProcessor.isRecording())
        text << "      Aufnahme " << dopplerfeldProcessor.recordedFrameCount() << " Frames";
    else if (dopplerfeldProcessor.isPlayingMotion())
        text << "      Wiedergabe";

    return text;
}

void DopplerfeldEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1a));

    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.setFont (13.0f);
    g.drawText ("dopplerfeld", margin, margin, 100, topBarHeight, juce::Justification::centredLeft);

    // CPU über 100% ist hörbar (Aussetzer) - die ganze Statuszeile färbt sich
    // dafür rot, statt nur die Zahl selbst hervorzuheben. Einfacher als ein
    // gemischtfarbiger Text und im Zweifel eher zu auffällig als übersehen.
    const bool overBudget = dopplerfeldProcessor.cpuLoadPercent() > 100.0f;
    g.setColour (overBudget ? juce::Colours::orangered.withAlpha (0.85f)
                            : juce::Colours::white.withAlpha (0.6f));
    // Monospace statt Proportionalschrift: nur bei fester Zeichenbreite pro
    // Glyphe hält das Zahlen-Padding in statusText() die Spalten auch
    // tatsaechlich stabil (siehe Kommentar dort).
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)));
    g.drawFittedText (statusText(),
                      margin, getHeight() - statusHeight, fieldWidth, statusHeight,
                      juce::Justification::topLeft, 2);
}

void DopplerfeldEditor::resized()
{
    auto area = getLocalBounds().reduced (margin);

    auto topBar = area.removeFromTop (topBarHeight);
    topBar.removeFromLeft (100);   // Platz für den Schriftzug links
    sourceButton.setBounds (topBar.removeFromLeft (130));
    topBar.removeFromLeft (8);
    tooltipsButton.setBounds (topBar.removeFromLeft (130));
    topBar.removeFromLeft (8);
    viewButton.setBounds (topBar.removeFromLeft (170));
    topBar.removeFromLeft (8);
    speedUnitButton.setBounds (topBar.removeFromLeft (70));
    topBar.removeFromLeft (8);
    engineResetButton.setBounds (topBar.removeFromLeft (110));

    area.removeFromTop (6);

    auto fieldArea = area.removeFromLeft (fieldWidth);
    field.setBounds (fieldArea.removeFromTop (fieldHeight));

    area.removeFromLeft (margin);
    panelViewport.setBounds (area);

    layoutPanels();
}

void DopplerfeldEditor::layoutPanels()
{
    const int width = juce::jmax (100, panelViewport.getMaximumVisibleWidth());

    struct Entry { CollapsiblePanel* box; int contentHeight; };

    const Entry entries[] {
        { &enginePanelBox, engineContentHeight },
        { &samplePanelBox, sampleContentHeight },
        { &motionPanelBox, motionContentHeight },
        { &fieldPanelBox,  fieldContentHeight  },
        { &wallPanelBox,   wallContentHeight   },
        { &swarmPanelBox,  swarmContentHeight  }
    };

    int y = 0;

    for (const auto& entry : entries)
    {
        const int height = entry.box->isExpanded() ? CollapsiblePanel::headerHeight + entry.contentHeight
                                                    : CollapsiblePanel::headerHeight;

        entry.box->setBounds (0, y, width, height);
        y += height + 4;
    }

    panelHolder.setSize (width, y);
}
