#include "PluginEditor.h"
#include "Params.h"

#include <cmath>

DopplerfeldEditor::DopplerfeldEditor (DopplerfeldProcessor& p)
    : AudioProcessorEditor (&p),
      dopplerfeldProcessor (p),
      engineControlPanel (p.apvts),
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

    // Motor-Gating (@dpa-Feedback): reine Weiterleitung, die Entscheidung
    // (ob/wie reagiert wird) trifft der Processor - siehe setMotorGateEnabled().
    field.onSourceGrabbed  = [this] { dopplerfeldProcessor.notifySourceGrabbed(); };
    field.onSourceReleased = [this] { dopplerfeldProcessor.notifySourceReleased(); };

    field.onListenerRotated = [this] (double yawRadians)
    {
        setParameter (Params::lisYaw, juce::radiansToDegrees (yawRadians));
    };

    engineControlPanelBox.setContent (&engineControlPanel);
    enginePanelBox.setContent (&enginePanel);
    samplePanelBox.setContent (&samplePanel);
    motionPanelBox.setContent (&motionPanel);
    fieldPanelBox.setContent (&fieldPanel);
    wallPanelBox.setContent (&wallPanel);
    swarmPanelBox.setContent (&swarmPanel);

    // Alle Panels starten zugeklappt (CollapsiblePanel-Default, @dpa-Feedback)
    // - sonst steht die Spalte beim Oeffnen sofort voll.

    for (auto* box : { &engineControlPanelBox, &enginePanelBox, &samplePanelBox, &motionPanelBox,
                       &fieldPanelBox, &wallPanelBox, &swarmPanelBox })
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

    sourceButton.setTooltip ("Klangquelle umschalten: Motor-Generator, geladenes Sample oder "
                             "Audio-Eingang (live, sofern der Host/das Format einen bereitstellt).");
    sourceButton.onClick = [this]
    {
        using Kind = DopplerfeldProcessor::SourceKind;

        const auto next = dopplerfeldProcessor.currentSourceKind() == Kind::Motor   ? Kind::Sample
                         : dopplerfeldProcessor.currentSourceKind() == Kind::Sample  ? Kind::AudioIn
                                                                                     : Kind::Motor;
        dopplerfeldProcessor.selectSourceKind (next);
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

    // @dpa-Feedback: der Schalter sitzt im "Bewegung"-Panel (MotionPanel),
    // nicht in der Kopfzeile - der Zustand selbst gehoert weiterhin der
    // FieldComponent (die zieht ja). Kein Parameter, reines Bedienungsgefuehl.
    motionPanel.setCoastEnabled (field.isCoastEnabled());
    motionPanel.onCoastToggled = [this] (bool enabled) { field.setCoastEnabled (enabled); };

    engineControlPanel.setMotorGateEnabled (dopplerfeldProcessor.isMotorGateEnabled());
    engineControlPanel.onMotorGateToggled = [this] (bool enabled)
    {
        dopplerfeldProcessor.setMotorGateEnabled (enabled);
    };

    // Scope (@dpa-Feedback): gross, wegschaltbar, mit Freeze und Sync.
    addAndMakeVisible (scope);

    scopeToggleButton.setTooltip ("Oszilloskop ein-/ausblenden.");
    scopeToggleButton.setButtonText (scopeVisible ? "Scope ausblenden" : "Scope");
    scopeToggleButton.onClick = [this]
    {
        scopeVisible = ! scopeVisible;
        scopeToggleButton.setButtonText (scopeVisible ? "Scope ausblenden" : "Scope");
        updateScopeVisibility();
    };
    addAndMakeVisible (scopeToggleButton);

    scopeFreezeButton.setTooltip ("Scope-Bild anhalten und auf die komplette bisherige Historie "
                                  "umschalten (@dpa-Feedback: \"frei herumsuchen\") - darin "
                                  "waagerecht scrollen/ziehen zum Verschieben, senkrecht/Pinch "
                                  "weiter zum Zoomen. Der Ringpuffer laeuft im Hintergrund "
                                  "weiter, erst ein erneuter Klick holt wieder Live-Daten.");
    scopeFreezeButton.onClick = [this]
    {
        if (! scope.isFrozen())
        {
            // Komplette bisherige Historie einmalig ziehen (nicht nur das
            // kleine Live-Fenster) - danach ist das Bild statisch, darin
            // laesst sich frei suchen (siehe ScopeComponent::
            // enterHistoryMode()).
            const int capacity = dopplerfeldProcessor.scopeRingCapacity();
            std::vector<float> fullLeft ((size_t) capacity), fullRight ((size_t) capacity);
            dopplerfeldProcessor.fillScopeWindow (fullLeft.data(), fullRight.data(), capacity);
            scope.enterHistoryMode (fullLeft.data(), fullRight.data(), capacity);

            scopeFreezeButton.setButtonText ("Freeze: An");
            scopeFreezeButton.setColour (juce::TextButton::buttonColourId,
                                         juce::Colours::orangered.withAlpha (0.35f));
        }
        else
        {
            scope.exitHistoryMode();
            scopeFreezeButton.setButtonText ("Freeze");
            scopeFreezeButton.setColour (juce::TextButton::buttonColourId,
                                         juce::Colours::transparentBlack);
        }
    };
    scopeFreezeButton.setButtonText ("Freeze");
    addAndMakeVisible (scopeFreezeButton);

    scopeSyncButton.setTooltip ("Sync: richtet einen steigenden Nulldurchgang von L in der Mitte "
                                "des Scopes aus - der Trigger-Moment steht dann immer zentriert.");
    scopeSyncButton.onClick = [this]
    {
        const bool sync = ! scope.isSyncEnabled();
        scope.setSyncEnabled (sync);
        scopeSyncButton.setButtonText (sync ? "Sync: An" : "Sync");
        scopeSyncButton.setColour (juce::TextButton::buttonColourId,
                                   sync ? juce::Colours::orangered.withAlpha (0.35f)
                                        : juce::Colours::transparentBlack);
    };
    scopeSyncButton.setButtonText ("Sync");
    addAndMakeVisible (scopeSyncButton);

    scopeZoomInButton.setTooltip ("Reinzoomen (kuerzere Zeitbasis). Wirkt wie Mausrad hoch "
                                  "oder Pinch-Auseinanderziehen direkt auf dem Scope.");
    scopeZoomInButton.onClick = [this] { scope.zoomStep (0.7f); };
    addAndMakeVisible (scopeZoomInButton);

    scopeZoomOutButton.setTooltip ("Rauszoomen (laengere Zeitbasis, bis zu "
                                   + juce::String ((int) DopplerfeldProcessor::scopeMaxDisplaySeconds)
                                   + "s). Wirkt wie Mausrad runter oder Pinch-Zusammenziehen "
                                     "direkt auf dem Scope.");
    scopeZoomOutButton.onClick = [this] { scope.zoomStep (1.4f); };
    addAndMakeVisible (scopeZoomOutButton);

    scopeSaveButton.setTooltip ("Sichtbaren Scope-Ausschnitt als WAV (32bit float, stereo) in "
                                "~/Downloads ablegen (@dpa-Feedback: \"fuer Dich, debuggen\") - "
                                "Zeitstempel im Dateinamen, abspielbar mit der richtigen "
                                "Samplerate/Tonhoehe.");
    scopeSaveButton.onClick = [this]
    {
        const auto now = juce::Time::getCurrentTime();
        const juce::String stamp = juce::String::formatted (
            "%04d%02d%02d_%02d%02d%02d",
            now.getYear(), now.getMonth() + 1, now.getDayOfMonth(),
            now.getHours(), now.getMinutes(), now.getSeconds());

        auto downloads = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                              .getChildFile ("Downloads");
        downloads.createDirectory();

        auto file = downloads.getChildFile ("dopplerfeld_" + stamp + ".wav");
        const bool ok = scope.exportVisibleWindow (file);

        scopeSaveStatusLabel.setText (ok ? ("Gespeichert: " + file.getFileName())
                                         : "Speichern fehlgeschlagen",
                                     juce::dontSendNotification);
        scopeSaveStatusUntilMs = juce::Time::getMillisecondCounter() + 4000;
    };
    addAndMakeVisible (scopeSaveButton);

    scopeSaveStatusLabel.setJustificationType (juce::Justification::centredLeft);
    scopeSaveStatusLabel.setColour (juce::Label::textColourId, juce::Colours::limegreen);
    scopeSaveStatusLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    addAndMakeVisible (scopeSaveStatusLabel);

    updateScopeVisibility();

    // 30 Hz: schnell genug, dass eine gezogene Quelle nicht ruckelt, und
    // langsam genug, dass die Wellenfronten nicht flimmern.
    startTimerHz (30);
}

void DopplerfeldEditor::updateScopeVisibility()
{
    scope.setVisible (scopeVisible);
    scopeFreezeButton.setVisible (scopeVisible);
    scopeSyncButton.setVisible (scopeVisible);
    scopeZoomInButton.setVisible (scopeVisible);
    scopeZoomOutButton.setVisible (scopeVisible);
    scopeSaveButton.setVisible (scopeVisible);
    scopeSaveStatusLabel.setVisible (scopeVisible);

    setSize (margin * 2 + fieldWidth + margin + panelColumnWidth,
             margin * 2 + topBarHeight + 6 + fieldHeight
                 + (scopeVisible ? scopeBlockHeight : 0) + statusHeight);
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
    updateDisplayAverages();

    field.setFieldMetres ((double) *dopplerfeldProcessor.apvts.getRawParameterValue (Params::fieldMetres));
    field.setSpeedUnit (speedUnit);
    field.setSnapshot (snapshot);
    field.setDisplaySpeed (displayAverages.speedMps, displayAverages.speedOfSoundMps);

    {
        using Kind = DopplerfeldProcessor::SourceKind;

        switch (dopplerfeldProcessor.currentSourceKind())
        {
            case Kind::Sample:  sourceButton.setButtonText ("Quelle: Sample");   break;
            case Kind::AudioIn: sourceButton.setButtonText ("Quelle: Audio In"); break;
            case Kind::Motor:
            default:            sourceButton.setButtonText ("Quelle: Motor");    break;
        }
    }
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

    // Scope (@dpa-Feedback): nur ziehen, wenn eingeblendet - bei Freeze
    // ignoriert ScopeComponent::feed() das Fenster ohnehin, aber das Ziehen
    // selbst spart sich der Editor, solange gar nicht sichtbar ist.
    if (scopeVisible)
    {
        // Samplerate kann sich aendern (Host-Wechsel des Projekts) - nur bei
        // echter Aenderung neu rechnen, nicht bei jedem 33ms-Tick.
        const double sr = dopplerfeldProcessor.getSampleRate();

        if (sr > 0.0 && std::abs (sr - lastKnownScopeSampleRate) > 0.5)
        {
            lastKnownScopeSampleRate = sr;
            scope.setSampleRateHint (sr);
            scope.setMaxDisplaySampleCount (
                (int) (sr * DopplerfeldProcessor::scopeMaxDisplaySeconds));
        }

        const int captureLen = scope.captureWindowSampleCount();

        if ((int) scopeRawLeft.size() != captureLen)
        {
            scopeRawLeft.resize ((size_t) captureLen);
            scopeRawRight.resize ((size_t) captureLen);
        }

        dopplerfeldProcessor.fillScopeWindow (scopeRawLeft.data(), scopeRawRight.data(), captureLen);
        scope.feed (scopeRawLeft.data(), scopeRawRight.data());

        // Bestaetigungstext nach dem Speichern nur ein paar Sekunden stehen
        // lassen, nicht dauerhaft im Toolbar rumstehen.
        if (scopeSaveStatusUntilMs != 0 && juce::Time::getMillisecondCounter() > scopeSaveStatusUntilMs)
        {
            scopeSaveStatusLabel.setText ({}, juce::dontSendNotification);
            scopeSaveStatusUntilMs = 0;
        }
    }

    // Statuszeile neu zeichnen, nicht das ganze Fenster - die Panels darüber
    // ändern sich nur bei Bedienung.
    repaint (margin, getHeight() - statusHeight, fieldWidth, statusHeight);
}

void DopplerfeldEditor::updateDisplayAverages()
{
    // @dpa-Feedback ("Langsamkeit der Anzeigewahrnehmung", 20260818): erst
    // aufsummieren, dann alle 0.5s einmal umrechnen - dazwischen bleibt
    // displayAverages unveraendert, egal wie oft der 30Hz-Timer inzwischen
    // tickt. Damit zeigt die Anzeige einen ruhigen Momentanschnitt statt bei
    // jedem Tick den zappelnden Rohwert.
    auto& acc = displayAccumulator;

    acc.speedSum            += snapshot.sourceSpeed;
    acc.speedOfSoundSum     += snapshot.speedOfSound;
    acc.listenerDistanceSum += (snapshot.sourcePos - snapshot.listener.head).length();
    acc.cpuSum               += (double) dopplerfeldProcessor.cpuLoadPercent();
    ++acc.sampleCount;

    // 30Hz-Timer = ~33ms zwischen zwei Aufrufen (siehe startTimerHz), fest
    // verdrahtet statt gemessen - reicht fuer ein 0.5s-Mittelungsfenster.
    acc.elapsedMs += 1000.0 / 30.0;

    if (acc.elapsedMs < displayAverageWindowMs || acc.sampleCount == 0)
        return;

    displayAverages.speedMps          = acc.speedSum          / acc.sampleCount;
    displayAverages.speedOfSoundMps   = acc.speedOfSoundSum    / acc.sampleCount;
    displayAverages.listenerDistanceM = acc.listenerDistanceSum / acc.sampleCount;
    displayAverages.cpuPercent        = acc.cpuSum             / acc.sampleCount;

    acc = {};
}

juce::String DopplerfeldEditor::statusText() const
{
    juce::String text;

    // Feste Breite pro Zahl (printf-Padding), zusammen mit dem Monospace-Font
    // in paint(): ohne feste Breite verschiebt eine kürzer werdende Zahl
    // (z.B. "9.3" -> "-9.3") allen nachfolgenden Text um ein wechselndes
    // Stück - die ganze Zeile "wackelt". Mit fester Zeichenbreite bleiben
    // Spalten stehen. Alle drei Werte unten kommen zusaetzlich aus
    // displayAverages statt direkt aus snapshot - s. updateDisplayAverages()
    // fuer das 0.5s-Mittelungsfenster (@dpa-Feedback "Langsamkeit der
    // Anzeigewahrnehmung").

    // @dpa-Feedback: Tempo der Quelle, Einheit per speedUnitButton umschaltbar.
    // Mach kommt aus derselben Momentangeschwindigkeit, nicht aus M_r (das ist
    // radial zum jeweiligen Ohr, hier geht es um die Quelle selbst). Feste
    // Breite kommt schon aus FieldComponent::formatSpeed() selbst (auch vom
    // Cockpit-Display im Feld genutzt) - hier kein zweites Padding noetig.
    text << FieldComponent::formatSpeed (displayAverages.speedMps, displayAverages.speedOfSoundMps, speedUnit);

    // @dpa-Feedback: L-M-Abstand immer sichtbar, nicht nur bei Vorbeiflug.
    text << "   L-M " << juce::String::formatted ("%7.1f", displayAverages.listenerDistanceM) << " m";

    // @dpa-Feedback: CPU-Echtzeit-Anzeige (Wanduhrzeit/Audiozeit, geglättet -
    // siehe cpuLoadPercent()). Über 100% färbt paint() die ganze Statuszeile
    // rot (siehe dort) - reiner Text reicht hier, kein eigener Meter nötig.
    text << "   CPU " << juce::String::formatted ("%4.0f", displayAverages.cpuPercent) << " %";

    // @dpa-Feedback: Einzelne Pfade (L/R, M_r, Zweige) sind zu klein und zu
    // viel für die Statuszeile - nur die Anzahl aktiver Mehrfachreflexionen
    // bleibt als grobe Andeutung, was gerade gerechnet wird.
    int higherOrder = 0;

    for (int i = 0; i < snapshot.pathCount; ++i)
        if (snapshot.paths[(size_t) i].order > 1)
            ++higherOrder;

    if (higherOrder > 0)
        text << "   +" << higherOrder << " Mehrfachrefl.";

    if (dopplerfeldProcessor.isRecording())
        text << "   Aufnahme " << dopplerfeldProcessor.recordedFrameCount() << " Frames";
    else if (dopplerfeldProcessor.isPlayingMotion())
        text << "   Wiedergabe";

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
    // tatsaechlich stabil (siehe Kommentar dort). drawText() statt
    // drawFittedText(): Letzteres skaliert die Schrift nach, wenn die Zeile
    // (durch "+N Mehrfachrefl." oder "Aufnahme"/"Wiedergabe" am Ende) mal
    // laenger, mal kuerzer wird - dann "atmet" die ganze Zeile mit, selbst
    // die fest gepaddeten Spalten davor. drawText() zeichnet immer in der
    // gesetzten Groesse und schneidet im Zweifel einfach ab.
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 16.0f, juce::Font::plain)));
    g.drawText (statusText(),
                margin, getHeight() - statusHeight, fieldWidth, statusHeight,
                juce::Justification::centredLeft, false);
}

void DopplerfeldEditor::resized()
{
    auto area = getLocalBounds().reduced (margin);

    auto topBar = area.removeFromTop (topBarHeight);
    topBar.removeFromLeft (100);   // Platz für den Schriftzug links
    sourceButton.setBounds (topBar.removeFromLeft (150));   // "Quelle: Audio In" ist der laengste Text
    topBar.removeFromLeft (8);
    tooltipsButton.setBounds (topBar.removeFromLeft (130));
    topBar.removeFromLeft (8);
    viewButton.setBounds (topBar.removeFromLeft (170));
    topBar.removeFromLeft (8);
    speedUnitButton.setBounds (topBar.removeFromLeft (70));
    topBar.removeFromLeft (8);
    engineResetButton.setBounds (topBar.removeFromLeft (110));
    topBar.removeFromLeft (8);
    scopeToggleButton.setBounds (topBar.removeFromLeft (140));

    area.removeFromTop (6);

    auto fieldArea = area.removeFromLeft (fieldWidth);
    field.setBounds (fieldArea.removeFromTop (fieldHeight));

    if (scopeVisible)
    {
        fieldArea.removeFromTop (6);

        auto scopeToolbar = fieldArea.removeFromTop (scopeToolbarHeight);
        scopeFreezeButton.setBounds (scopeToolbar.removeFromLeft (90));
        scopeToolbar.removeFromLeft (8);
        scopeSyncButton.setBounds (scopeToolbar.removeFromLeft (90));
        scopeToolbar.removeFromLeft (16);
        scopeZoomOutButton.setBounds (scopeToolbar.removeFromLeft (28));
        scopeToolbar.removeFromLeft (4);
        scopeZoomInButton.setBounds (scopeToolbar.removeFromLeft (28));
        scopeToolbar.removeFromLeft (16);
        scopeSaveButton.setBounds (scopeToolbar.removeFromLeft (90));
        scopeToolbar.removeFromLeft (8);
        scopeSaveStatusLabel.setBounds (scopeToolbar);

        fieldArea.removeFromTop (4);
        scope.setBounds (fieldArea.removeFromTop (scopeHeight));
    }

    area.removeFromLeft (margin);
    panelViewport.setBounds (area);

    layoutPanels();
}

void DopplerfeldEditor::layoutPanels()
{
    const int width = juce::jmax (100, panelViewport.getMaximumVisibleWidth());

    struct Entry { CollapsiblePanel* box; int contentHeight; };

    const Entry entries[] {
        { &engineControlPanelBox, engineControlContentHeight },
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
