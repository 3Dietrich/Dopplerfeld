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
      reverbPanel (p.apvts),
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

    field.onTapDragged = [this] (int index, double normX, double normY)
    {
        setParameter (Params::tapId (index, Params::TapPart::x).toRawUTF8(), normX);
        setParameter (Params::tapId (index, Params::TapPart::y).toRawUTF8(), normY);
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

    // Losgelassen mit Schwung: die Geschwindigkeit geht an die Bewegungskette,
    // dort laeuft die Quelle damit aus (DopplerfeldProcessor::startSourceCoast).
    field.onSourceCoast = [this] (Vec3)
    {
        // Die Geschwindigkeit kommt nicht von hier: die Bewegungskette misst
        // ihre eigene (siehe startSourceCoast).
        dopplerfeldProcessor.startSourceCoast();
    };

    field.onListenerRotated = [this] (double yawRadians)
    {
        setParameter (Params::lisYaw, juce::radiansToDegrees (yawRadians));
    };

    engineControlPanelBox.setContent (&engineControlPanel);
    enginePanelBox.setContent (&enginePanel);

    // Ein Wechsel der Betriebsart aendert die Hoehe des Motor-Panels - die
    // Panelspalte muss danach neu gesetzt werden.
    enginePanel.onLayoutChanged = [this] { layoutPanels(); };
    samplePanelBox.setContent (&samplePanel);
    motionPanelBox.setContent (&motionPanel);
    fieldPanelBox.setContent (&fieldPanel);
    wallPanelBox.setContent (&wallPanel);
    reverbPanelBox.setContent (&reverbPanel);

    // Der Bypass sitzt in der Kopfzeile und bleibt damit auch zugeklappt
    // erreichbar (@dpa 20260829: "am coolsten waere auf dem Header der ja auch
    // zugeklappt sichtbar ist").
    reverbBypassButton.setClickingTogglesState (true);
    reverbBypassButton.setTooltip (Tooltips::text (Tooltips::Key::ReverbBypass));
    reverbBypassButton.setColour (juce::TextButton::buttonColourId, Theme::panelHeader);
    reverbBypassButton.setColour (juce::TextButton::buttonOnColourId, Theme::Panel::wall.withAlpha (0.9f));
    reverbBypassButton.setColour (juce::TextButton::textColourOffId, Theme::muted);
    reverbBypassButton.setColour (juce::TextButton::textColourOnId, Theme::panel);

    reverbBypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        p.apvts, Params::reverbBypass, reverbBypassButton);

    reverbPanelBox.setHeaderControl (&reverbBypassButton, 62);

    // Dieselbe Machart fuer die Waende: "1", "2", "++" in ihrer Kopfzeile.
    {
        struct Entry { juce::TextButton* button; const char* id; Tooltips::Key key; };

        const Entry entries[]
        {
            { &wallHeaderSwitches.wall1,  Params::wall1On,     Tooltips::Key::WallOn },
            { &wallHeaderSwitches.wall2,  Params::wall2On,     Tooltips::Key::WallOn },
            { &wallHeaderSwitches.second, Params::reflect2ndOn, Tooltips::Key::SecondOrder }
        };

        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>* slots[]
        {
            &wall1HeaderAttachment, &wall2HeaderAttachment, &secondOrderHeaderAttachment
        };

        int i = 0;

        for (const auto& e : entries)
        {
            e.button->setClickingTogglesState (true);
            e.button->setTooltip (Tooltips::text (e.key));
            e.button->setColour (juce::TextButton::buttonColourId,   Theme::panelHeader);
            e.button->setColour (juce::TextButton::buttonOnColourId, Theme::Panel::wall.withAlpha (0.9f));
            e.button->setColour (juce::TextButton::textColourOffId,  Theme::muted);
            e.button->setColour (juce::TextButton::textColourOnId,   Theme::panel);

            *slots[i++] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                p.apvts, e.id, *e.button);
        }
    }

    wallPanelBox.setHeaderControl (&wallHeaderSwitches, 84);
    swarmPanelBox.setContent (&swarmPanel);

    // Alle Panels starten zugeklappt (CollapsiblePanel-Default, @dpa-Feedback)
    // - sonst steht die Spalte beim Oeffnen sofort voll.

    // Bereichsfarben - welcher Bereich welchen Ton bekommt, steht in
    // Theme::Panel (UI/Theme.h), damit die Panels selbst denselben Ton fuer
    // ihre Reiter benutzen koennen.
    engineControlPanelBox.setAccentColour (Theme::Panel::engineControl);
    enginePanelBox.setAccentColour        (Theme::Panel::engine);
    samplePanelBox.setAccentColour        (Theme::Panel::sample);
    motionPanelBox.setAccentColour        (Theme::Panel::motion);
    fieldPanelBox.setAccentColour         (Theme::Panel::field);
    wallPanelBox.setAccentColour          (Theme::Panel::wall);

    // Derselbe Ton wie die Reflexionen: der Hall ist dasselbe Thema, nur die
    // billige Naeherung davon. Ein eigener Ton waere ein siebter, und die
    // Palette hat sechs.
    reverbPanelBox.setAccentColour        (Theme::Panel::wall);
    swarmPanelBox.setAccentColour         (Theme::Panel::swarm);

    for (auto* box : { &engineControlPanelBox, &enginePanelBox, &samplePanelBox, &motionPanelBox,
                       &fieldPanelBox, &wallPanelBox, &reverbPanelBox, &swarmPanelBox })
    {
        box->onExpandedChanged = [this]
        {
            layoutPanels();

            // Jeder Klick geht sofort in den Prozessor - von dort schreibt ihn
            // das naechste Sichern mit ins Preset.
            storePanelOpenMask();
        };
        panelHolder.addAndMakeVisible (box);
    }

    panelViewport.setViewedComponent (&panelHolder, false);
    panelViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (panelViewport);

    samplePanel.onFileSelected = [this] (const juce::File& file)
    {
        dopplerfeldProcessor.loadSampleFile (file);
    };

    swarmPanel.onShowClonesToggled = [this] (bool show) { field.setShowClones (show); };

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

    sourceButton.setTooltip (Tooltips::text (Tooltips::Key::SourceButton));
    sourceButton.onClick = [this]
    {
        using Kind = DopplerfeldProcessor::SourceKind;

        const auto next = dopplerfeldProcessor.currentSourceKind() == Kind::Motor   ? Kind::Sample
                         : dopplerfeldProcessor.currentSourceKind() == Kind::Sample  ? Kind::AudioIn
                                                                                     : Kind::Motor;
        dopplerfeldProcessor.selectSourceKind (next);
    };
    // Hauptschalter ganz links, noch vor dem Schriftzug: er schaltet alles ab,
    // das gehoert an die auffaelligste Stelle und nicht in eine Klappe.
    masterOnButton.setTooltip (Tooltips::text (Tooltips::Key::MasterOn));
    addAndMakeVisible (masterOnButton);
    masterOnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        dopplerfeldProcessor.apvts, Params::masterOn, masterOnButton);

    addAndMakeVisible (sourceButton);

    field.setTooltip (Tooltips::text (Tooltips::Key::FieldDrag));

    // @dpa-Feedback: Hilfehinweise abschaltbar. Start an, weil neue Regler
    // ohne Erklaerung sonst raten heisst.
    viewButton.setTooltip (Tooltips::text (Tooltips::Key::ViewToggle));
    viewButton.onClick = [this]
    {
        const bool toPerspective = field.getViewMode() == FieldComponent::ViewMode::TopDown;

        field.setViewMode (toPerspective ? FieldComponent::ViewMode::Perspective
                                         : FieldComponent::ViewMode::TopDown);

        viewButton.setButtonText (Labels::text (toPerspective ? "Ansicht: Perspektive"
                                                          : "Ansicht: Draufsicht"));
    };
    viewButton.setButtonText (Labels::text ("Ansicht: Draufsicht"));
    addAndMakeVisible (viewButton);

    speedUnitButton.setTooltip (Tooltips::text (Tooltips::Key::SpeedUnitToggle));
    speedUnitButton.setButtonText ("km/h");
    speedUnitButton.onClick = [this]
    {
        speedUnit = (speedUnit == SpeedUnit::KmH) ? SpeedUnit::Ms
                  : (speedUnit == SpeedUnit::Ms)   ? SpeedUnit::Mach
                                                    : SpeedUnit::KmH;
        speedUnitButton.setButtonText (speedUnit == SpeedUnit::KmH ? "km/h"
                                      : speedUnit == SpeedUnit::Ms  ? "m/s"
                                                                    : "Mach");

        // Die Tempo-Regler haengen am selben Schalter (@dpa: "oder du schaltest
        // beim Hauptschalter die Value Anzeige des Knobs um").
        motionPanel.setSpeedUnit (speedUnit, displayAverages.speedOfSoundMps);
    };
    addAndMakeVisible (speedUnitButton);

    // Anfangszustand der Regler-Beschriftung, passend zum Schaltertext oben.
    // 343 m/s ist die Schallgeschwindigkeit bei den fest eingestellten 20 Grad;
    // beim Umschalten wird der gemessene Wert benutzt.
    motionPanel.setSpeedUnit (speedUnit, 343.0);

    engineResetButton.setTooltip (Tooltips::text (Tooltips::Key::EngineReset));
    engineResetButton.setButtonText (Labels::text ("Engine Restart"));
    engineResetButton.setColour (juce::TextButton::buttonColourId,
                                 juce::Colours::orangered.withAlpha (0.35f));
    engineResetButton.onClick = [this] { dopplerfeldProcessor.restartEngine(); };
    addAndMakeVisible (engineResetButton);

    tooltipsButton.setToggleState (true, juce::dontSendNotification);
    tooltipsButton.setTooltip (Tooltips::text (Tooltips::Key::TooltipsToggle));
    tooltipsButton.onClick = [this] { tooltipWindow.enabled = tooltipsButton.getToggleState(); };
    addAndMakeVisible (tooltipsButton);

    // Sprachumschalter DE/EN fuer die Hilfehinweise (@dpa-Auftrag). Setzt nur
    // den Zustand in Tooltips.h um und ruft refreshAllTooltips() - der
    // eigentliche Text bleibt zentral in Tooltips.h, hier wird er nur an
    // jeder Komponente neu gesetzt (setTooltip() speichert einmalig, es gibt
    // keine dynamische Nachschau bei jedem Hover).
    languageButton.setButtonText (Tooltips::currentLanguage() == Tooltips::Language::De ? "DE" : "EN");
    languageButton.setTooltip (Tooltips::text (Tooltips::Key::LanguageToggle));
    languageButton.onClick = [this]
    {
        Tooltips::toggleLanguage();
        languageButton.setButtonText (Tooltips::currentLanguage() == Tooltips::Language::De ? "DE" : "EN");
        refreshAllTooltips();
    };
    addAndMakeVisible (languageButton);

    // @dpa-Feedback: der Schalter sitzt im "Bewegung"-Panel (MotionPanel),
    // nicht in der Kopfzeile - der Zustand selbst gehoert weiterhin der
    // FieldComponent (die zieht ja). Kein Parameter, reines Bedienungsgefuehl.
    motionPanel.setCoastEnabled (field.isCoastEnabled());
    motionPanel.onCoastToggled = [this] (bool enabled) { field.setCoastEnabled (enabled); };
    motionPanel.onMouseFrameToggled = [this] (bool enabled) { field.setMouseFrameSmoothing (enabled); };

    engineControlPanel.setMotorGateEnabled (dopplerfeldProcessor.isMotorGateEnabled());
    engineControlPanel.onMotorGateToggled = [this] (bool enabled)
    {
        dopplerfeldProcessor.setMotorGateEnabled (enabled);
    };

    // Scope (@dpa-Feedback): gross, wegschaltbar, mit Freeze und Sync.
    addAndMakeVisible (scope);

    // Play-Toggle: die Komponente entscheidet selbst, WANN eine Wiedergabe
    // angestossen wird (Einschalten, oder ein Klick, s. ScopeComponent::
    // setPlaybackEnabled()/mouseUp()) - hier wird nur noch der angeforderte
    // Ausschnitt an den Processor weitergereicht. Copy-Ziel dort steht seit
    // prepareToPlay() fest, die Zeiger muessen also nicht ueber den Aufruf
    // hinaus gueltig bleiben.
    scope.onPlaybackRequested = [this] (const float* left, const float* right, int length)
    {
        dopplerfeldProcessor.requestScopePlayback (left, right, length);
    };

    scopeToggleButton.setTooltip (Tooltips::text (Tooltips::Key::ScopeToggle));
    scopeToggleButton.setButtonText (Labels::text (scopeVisible ? "Scope ausblenden" : "Scope"));
    scopeToggleButton.onClick = [this]
    {
        scopeVisible = ! scopeVisible;
        scopeToggleButton.setButtonText (Labels::text (scopeVisible ? "Scope ausblenden" : "Scope"));
        updateScopeVisibility();
    };
    addAndMakeVisible (scopeToggleButton);

    scopeFreezeButton.setTooltip (Tooltips::text (Tooltips::Key::ScopeFreeze));
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

            scopeFreezeButton.setButtonText (Labels::text ("Freeze: An"));
            scopeFreezeButton.setColour (juce::TextButton::buttonColourId,
                                         juce::Colours::orangered.withAlpha (0.35f));
        }
        else
        {
            scope.exitHistoryMode();
            scopeFreezeButton.setButtonText (Labels::text ("Freeze"));
            scopeFreezeButton.setColour (juce::TextButton::buttonColourId,
                                         juce::Colours::transparentBlack);
        }
    };
    scopeFreezeButton.setButtonText (Labels::text ("Freeze"));
    addAndMakeVisible (scopeFreezeButton);

    scopeSyncButton.setTooltip (Tooltips::text (Tooltips::Key::ScopeSync));
    scopeSyncButton.onClick = [this]
    {
        const bool sync = ! scope.isSyncEnabled();
        scope.setSyncEnabled (sync);
        scopeSyncButton.setButtonText (Labels::text (sync ? "Sync: An" : "Sync"));
        scopeSyncButton.setColour (juce::TextButton::buttonColourId,
                                   sync ? juce::Colours::orangered.withAlpha (0.35f)
                                        : juce::Colours::transparentBlack);
    };
    scopeSyncButton.setButtonText (Labels::text ("Sync"));
    addAndMakeVisible (scopeSyncButton);

    scopeEventButton.setTooltip (Tooltips::text (Tooltips::Key::ScopeEventTrigger));
    scopeEventButton.onClick = [this]
    {
        const bool on = ! scope.isEventTriggerEnabled();
        scope.setEventTriggerEnabled (on);
        scopeEventButton.setButtonText (Labels::text (on ? "Knall: An" : "Knall"));
        scopeEventButton.setColour (juce::TextButton::buttonColourId,
                                    on ? juce::Colours::orangered.withAlpha (0.35f)
                                       : juce::Colours::transparentBlack);
    };
    scopeEventButton.setButtonText (Labels::text ("Knall"));
    addAndMakeVisible (scopeEventButton);

    scopeHoldCombo.setTooltip (Tooltips::text (Tooltips::Key::ScopeHold));

    // Feste Stufen statt eines Reglers: die Haltezeit wird selten und dann
    // grob eingestellt, und die Werkzeugleiste ist schmal.
    scopeHoldCombo.addItem ("0,5 s", 1);
    scopeHoldCombo.addItem ("1 s",   2);
    scopeHoldCombo.addItem ("2 s",   3);
    scopeHoldCombo.addItem ("5 s",   4);
    scopeHoldCombo.setSelectedId (2, juce::dontSendNotification);
    scopeHoldCombo.onChange = [this]
    {
        static constexpr double seconds[] { 0.5, 1.0, 2.0, 5.0 };
        const int id = scopeHoldCombo.getSelectedId();

        if (id >= 1 && id <= 4)
            scope.setHoldSeconds (seconds[id - 1]);
    };
    scope.setHoldSeconds (1.0);
    addAndMakeVisible (scopeHoldCombo);

    scopeZoomInButton.setTooltip (Tooltips::text (Tooltips::Key::ScopeZoomIn));
    scopeZoomInButton.onClick = [this] { scope.zoomStep (0.7f); };
    addAndMakeVisible (scopeZoomInButton);

    scopeZoomOutButton.setTooltip (Tooltips::text (Tooltips::Key::ScopeZoomOutPrefix)
                                   + juce::String ((int) DopplerfeldProcessor::scopeMaxDisplaySeconds)
                                   + Tooltips::text (Tooltips::Key::ScopeZoomOutSuffix));
    scopeZoomOutButton.onClick = [this] { scope.zoomStep (1.4f); };
    addAndMakeVisible (scopeZoomOutButton);

    scopeSaveButton.setTooltip (Tooltips::text (Tooltips::Key::ScopeSave));
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

    scopePlayButton.setTooltip (Tooltips::text (Tooltips::Key::ScopePlay));
    scopePlayButton.onClick = [this]
    {
        const bool on = ! scope.isPlaybackEnabled();

        // Beides gehoert zusammen: die Komponente steuert Klick-Erkennung
        // und Cursor, der Processor die tatsaechliche Audio-Ersetzung (s.
        // DopplerfeldProcessor::setScopePlaybackModeEnabled()). Reihenfolge
        // wichtig - der Processor muss den Modus schon kennen, WENN
        // setPlaybackEnabled(true) unten synchron die erste Wiedergabe
        // ueber onPlaybackRequested anstoesst.
        dopplerfeldProcessor.setScopePlaybackModeEnabled (on);
        scope.setPlaybackEnabled (on);

        updateScopePlayButton (on);
    };
    scopePlayButton.setButtonText (Labels::text ("Play"));
    addAndMakeVisible (scopePlayButton);

    scopeSaveStatusLabel.setJustificationType (juce::Justification::centredLeft);
    scopeSaveStatusLabel.setColour (juce::Label::textColourId, juce::Colours::limegreen);
    scopeSaveStatusLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    addAndMakeVisible (scopeSaveStatusLabel);


    updateScopeVisibility();

    // Muss das LETZTE hinzugefuegte Kind sein - JUCE zeichnet/trifft Kinder in
    // Hinzufuege-Reihenfolge von unten nach oben, nur so liegt das Overlay
    // wirklich ueber allen Panels und faengt deren Klicks ab. Startet
    // unsichtbar, wenn es schon einmal gesehen wurde (WelcomeOverlay::hasBeenSeen()).
    addAndMakeVisible (welcomeOverlay);
    welcomeOverlay.setVisible (! WelcomeOverlay::hasBeenSeen());

    // Beschriftungen und Hinweise einmal in der aktuell gewaehlten Sprache
    // setzen. Die Knoepfe tragen im Quelltext ihren deutschen Text; wird der
    // Editor im EN-Betrieb geoeffnet, stuenden sie sonst bis zum ersten
    // Sprachwechsel deutsch da.
    // Zustandsstreifen: er kennt nur Dateien, das Uebersetzen in den
    // Zustandsblock steht hier. Es ist derselbe Block, den die
    // Standalone-App ueber ihr Optionen-Menue schreibt und liest
    // (copyXmlToBinary/getStateInformation) - die vorhandenen Presets bleiben
    // also unveraendert brauchbar.
    presetBar.onLoad = [this] (const juce::File& f)
    {
        juce::MemoryBlock block;

        if (! f.loadFileAsData (block) || block.getSize() < 8)
            return false;

        // Erst pruefen, dann laden: setStateInformation() steigt bei allem
        // aus, was kein Zustand dieses Plugins ist, und zwar still - der
        // Streifen meldete sonst "geladen", waehrend das vorige Preset
        // weiterlaeuft.
        if (! dopplerfeldProcessor.stateBlockIsOurs (block.getData(), (int) block.getSize()))
            return false;

        dopplerfeldProcessor.setStateInformation (block.getData(), (int) block.getSize());

        // Die Oberflaeche haengt an Parametern (Attachments) und aktualisiert
        // sich selbst; was NICHT am APVTS haengt - Quellwahl, Ansicht - holt
        // der 30-Hz-Timer ohnehin ab.
        return true;
    };

    presetBar.onSave = [this] (const juce::File& f) -> juce::String
    {
        // Nicht sichern, solange ein gerade geladener Zustand noch nicht
        // uebernommen ist: die Parameter waeren schon die neuen, die
        // Bewegungsaufzeichnung noch die alte, und das Ergebnis stuende
        // danach in der Datei (siehe stateLoadStillPending im Processor).
        // Der Audiothread braucht dafuer einen Block, von Hand ist dieses
        // Fenster nicht zu treffen - aber es kostet nichts, es zuzumachen,
        // und es geht um @dpas Aufnahmen.
        if (dopplerfeldProcessor.stateLoadStillPending())
            return Labels::text ("gerade geladen, noch nicht bereit");

        juce::MemoryBlock block;
        dopplerfeldProcessor.getStateInformation (block);

        if (block.getSize() == 0)
            return Labels::text ("konnte nicht schreiben");

        if (! f.replaceWithData (block.getData(), block.getSize()))
            return Labels::text ("konnte nicht schreiben");

        return {};
    };

    // Was kein Zustand ist, kommt gar nicht erst in die Liste: im
    // Preset-Ordner liegt auch das Beispiel-Sample fuer die Sample-Engine.
    presetBar.onCheck = [this] (const juce::File& f)
    {
        juce::MemoryBlock block;

        return f.loadFileAsData (block)
            && dopplerfeldProcessor.stateBlockIsOurs (block.getData(), (int) block.getSize());
    };

    presetBar.refreshList();

    addAndMakeVisible (presetBar);

    refreshAllTooltips();

    // Klappzustand aus dem Prozessor uebernehmen: das Fenster kann geschlossen
    // und wieder geoeffnet werden, und ein Preset kann ganz ohne Fenster
    // geladen worden sein. Ohne vorherigen Zustand ist die Maske 0, also alles
    // zu - genau der Grundzustand von CollapsiblePanel.
    lastPanelOpenMaskVersion = dopplerfeldProcessor.getPanelOpenMaskVersion();
    applyPanelOpenMask (dopplerfeldProcessor.getPanelOpenMask());

    // 30 Hz: schnell genug, dass eine gezogene Quelle nicht ruckelt, und
    // langsam genug, dass die Wellenfronten nicht flimmern.
    startTimerHz (30);
}

void DopplerfeldEditor::refreshAllTooltips()
{
    // FieldComponent selbst setzt keine Tooltips (siehe grep), field traegt
    // seinen einzigen Tooltip von aussen (s. Konstruktor oben) - deshalb
    // reicht es, ihn hier direkt neu zu setzen, ohne FieldComponent
    // anzufassen.
    sourceButton.setTooltip (Tooltips::text (Tooltips::Key::SourceButton));
    field.setTooltip (Tooltips::text (Tooltips::Key::FieldDrag));
    viewButton.setTooltip (Tooltips::text (Tooltips::Key::ViewToggle));
    speedUnitButton.setTooltip (Tooltips::text (Tooltips::Key::SpeedUnitToggle));
    engineResetButton.setTooltip (Tooltips::text (Tooltips::Key::EngineReset));
    tooltipsButton.setTooltip (Tooltips::text (Tooltips::Key::TooltipsToggle));
    languageButton.setTooltip (Tooltips::text (Tooltips::Key::LanguageToggle));

    presetBar.refreshTexts();

    scopeToggleButton.setTooltip (Tooltips::text (Tooltips::Key::ScopeToggle));
    scopeFreezeButton.setTooltip (Tooltips::text (Tooltips::Key::ScopeFreeze));
    scopeSyncButton.setTooltip (Tooltips::text (Tooltips::Key::ScopeSync));
    scopeEventButton.setTooltip (Tooltips::text (Tooltips::Key::ScopeEventTrigger));
    scopeHoldCombo.setTooltip (Tooltips::text (Tooltips::Key::ScopeHold));
    scopeZoomInButton.setTooltip (Tooltips::text (Tooltips::Key::ScopeZoomIn));
    scopeZoomOutButton.setTooltip (Tooltips::text (Tooltips::Key::ScopeZoomOutPrefix)
                                   + juce::String ((int) DopplerfeldProcessor::scopeMaxDisplaySeconds)
                                   + Tooltips::text (Tooltips::Key::ScopeZoomOutSuffix));
    scopeSaveButton.setTooltip (Tooltips::text (Tooltips::Key::ScopeSave));
    scopePlayButton.setTooltip (Tooltips::text (Tooltips::Key::ScopePlay));
    scope.refreshTooltips();

    // Beschriftungen der Kopfzeile und der Scope-Leiste mit umschalten
    // (@dpa 20260824). Die Knoepfe, deren Text den Zustand zeigt, bekommen
    // den passenden Zustandstext - sonst stuende nach dem Sprachwechsel
    // "Freeze" ueber einem eingefrorenen Bild.
    // Die Ueberschriften der Klappen gehoeren dazu (@dpa 20260824: "die
    // Klappen sind noch deutsch geblieben").
    for (auto* box : { &engineControlPanelBox, &enginePanelBox, &samplePanelBox,
                        &motionPanelBox, &fieldPanelBox, &wallPanelBox, &reverbPanelBox, &swarmPanelBox })
        box->refreshTitle();

    masterOnButton.setButtonText (Labels::text ("An"));
    tooltipsButton.setButtonText (Labels::text ("Hilfehinweise"));
    scopeSaveButton.setButtonText (Labels::text ("Speichern"));
    engineResetButton.setButtonText (Labels::text ("Engine Restart"));

    viewButton.setButtonText (Labels::text (field.getViewMode() == FieldComponent::ViewMode::Perspective
                                                ? "Ansicht: Perspektive"
                                                : "Ansicht: Draufsicht"));

    scopeToggleButton.setButtonText (Labels::text (scopeVisible ? "Scope ausblenden" : "Scope"));
    scopeFreezeButton.setButtonText (Labels::text (scope.isFrozen() ? "Freeze: An" : "Freeze"));
    scopeSyncButton.setButtonText (Labels::text (scope.isSyncEnabled() ? "Sync: An" : "Sync"));
    scopeEventButton.setButtonText (Labels::text (scope.isEventTriggerEnabled() ? "Knall: An" : "Knall"));
    updateScopePlayButton (scope.isPlaybackEnabled());

    // Der Quelle-Knopf wird ohnehin im 30-Hz-Timer gesetzt und braucht hier
    // nichts.

    engineControlPanel.refreshTooltips();
    enginePanel.refreshTooltips();
    samplePanel.refreshTooltips();
    motionPanel.refreshTooltips();
    fieldPanel.refreshTooltips();
    wallPanel.refreshTooltips();
    reverbPanel.refreshTooltips();
    reverbBypassButton.setTooltip (Tooltips::text (Tooltips::Key::ReverbBypass));

    wallHeaderSwitches.wall1.setTooltip (Tooltips::text (Tooltips::Key::WallOn));
    wallHeaderSwitches.wall2.setTooltip (Tooltips::text (Tooltips::Key::WallOn));
    wallHeaderSwitches.second.setTooltip (Tooltips::text (Tooltips::Key::SecondOrder));
    reverbBypassButton.setButtonText (Labels::text ("Bypass"));
    swarmPanel.refreshTooltips();
}

void DopplerfeldEditor::updateScopePlayButton (bool on)
{
    scopePlayButton.setButtonText (Labels::text (on ? "Play: An" : "Play"));
    scopePlayButton.setColour (juce::TextButton::buttonColourId,
                               on ? juce::Colours::orangered.withAlpha (0.35f)
                                  : juce::Colours::transparentBlack);
}

void DopplerfeldEditor::updateScopeVisibility()
{
    scope.setVisible (scopeVisible);
    scopeFreezeButton.setVisible (scopeVisible);
    scopeSyncButton.setVisible (scopeVisible);
    scopeEventButton.setVisible (scopeVisible);
    scopeHoldCombo.setVisible (scopeVisible);
    scopeZoomInButton.setVisible (scopeVisible);
    scopeZoomOutButton.setVisible (scopeVisible);
    scopeSaveButton.setVisible (scopeVisible);
    scopePlayButton.setVisible (scopeVisible);
    scopeSaveStatusLabel.setVisible (scopeVisible);

    setSize (margin * 2 + fieldWidth + margin + panelColumnWidth,
             margin * 2 + topBarHeight + 4 + presetBarHeight + 6 + fieldHeight
                 + (scopeVisible ? scopeBlockHeight : 0) + cpuMeterBlockHeight + statusHeight);
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

    // 30-Hz-Timer, siehe startTimerHz() im Konstruktor.
    updateStatusFlashes (1.0 / 30.0);

    field.setFieldMetres ((double) *dopplerfeldProcessor.apvts.getRawParameterValue (Params::fieldMetres));
    field.setSnapshot (snapshot);
    field.setDisplaySpeed (displayAverages.speedMps, displayAverages.speedOfSoundMps);

    {
        using Kind = DopplerfeldProcessor::SourceKind;

        switch (dopplerfeldProcessor.currentSourceKind())
        {
            case Kind::Sample:  sourceButton.setButtonText (Labels::text ("Quelle: Sample"));   break;
            case Kind::AudioIn: sourceButton.setButtonText (Labels::text ("Quelle: Audio In")); break;
            case Kind::Motor:
            default:            sourceButton.setButtonText (Labels::text ("Quelle: Motor"));    break;
        }
    }
    motionPanel.setPlaying (dopplerfeldProcessor.isPlayingMotion());
    motionPanel.setRecording (dopplerfeldProcessor.isRecording());
    motionPanel.setFlying (dopplerfeldProcessor.isFlyingBy());

    // Nach einem Preset-/State-Load muss der Schalter den geladenen Zustand
    // zeigen, nicht den Klick-Stand von vorher (wie der Quelle-Button oben).
    engineControlPanel.setMotorGateEnabled (dopplerfeldProcessor.isMotorGateEnabled());

    // Dasselbe fuer die Panelspalte: brachte der geladene Zustand einen
    // Klappzustand mit, klappt die Spalte darauf um. Die Version zaehlt nur
    // beim Laden hoch, eigene Klicks loesen hier also nichts aus.
    if (const int version = dopplerfeldProcessor.getPanelOpenMaskVersion();
        version != lastPanelOpenMaskVersion)
    {
        lastPanelOpenMaskVersion = version;
        applyPanelOpenMask (dopplerfeldProcessor.getPanelOpenMask());
    }

    // Engine-Restart nach einem State-Load (@dpa): setStateInformation()
    // fordert ihn nur an, ausgefuehrt wird er hier auf dem Nachrichten-
    // Thread, weil restartEngine() in prepareToPlay() allokieren darf.
    if (dopplerfeldProcessor.consumeEngineRestartRequest())
        dopplerfeldProcessor.restartEngine();

    // Die CPU-Zeile liest die Klonzahl direkt im paint() der Editor-Zeile
    // (s. dort), genau wie die CPU-Last selbst - kein setLoad() auf ein
    // Panel noetig.

    // 30Hz-Timer = ~33ms zwischen zwei Aufrufen (siehe startTimerHz weiter
    // unten) - fest verdrahtet statt gemessen, das Levelmeter braucht nur
    // eine grobe Zeitbasis für Decay/Clip-Halt, keine exakte.
    // Der Begrenzer wird an derselben Stelle angezeigt wie das Uebersteuern,
    // naemlich an der Clip-Marke des Pegelmessers (@dpa: "es gibt ja das
    // Meter, das hat die roten clip anzeigen - das ist die Anzeige, da gehoert
    // sie hin. ohne extra anzeige"). Er greift gerade DAMIT es nicht clippt -
    // ohne diese Meldung bliebe die Marke dunkel, obwohl der Ausgang an seiner
    // Obergrenze haengt. limiterHits() zaehlt je Block neu, die Haltezeit
    // steckt in der Clip-Marke selbst.
    fieldPanel.pushLevels (dopplerfeldProcessor.consumeOutputPeakL(),
                           dopplerfeldProcessor.consumeOutputPeakR(),
                           1000.0 / 30.0,
                           dopplerfeldProcessor.limiterHits() > 0);

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
            const bool firstTime = lastKnownScopeSampleRate <= 0.0;

            lastKnownScopeSampleRate = sr;
            scope.setSampleRateHint (sr);
            scope.setMaxDisplaySampleCount (
                (int) (sr * DopplerfeldProcessor::scopeMaxDisplaySeconds));

            // Voreinstellung 10 s (@dpa 20260819: "Der Scope soll default auf
            // 10s gesetzt werden"). Erst hier, nicht in der Komponente: die
            // kennt die Abtastrate nicht und rechnet in Samples. Nur beim ersten
            // Mal, damit ein spaeterer Wechsel der Abtastrate nicht die
            // eingestellte Zoomstufe ueberschreibt.
            if (firstTime)
                scope.setDisplaySeconds (scopeDefaultSeconds, sr);
        }

        const int captureLen = scope.captureWindowSampleCount();

        if ((int) scopeRawLeft.size() != captureLen)
        {
            scopeRawLeft.resize ((size_t) captureLen);
            scopeRawRight.resize ((size_t) captureLen);
        }

        dopplerfeldProcessor.fillScopeWindow (scopeRawLeft.data(), scopeRawRight.data(), captureLen);
        scope.feed (scopeRawLeft.data(), scopeRawRight.data(),
                    dopplerfeldProcessor.scopeWritePosition());

        // Abspielcursor (@dpa: "ein Cursor zeigt, wo die Wiedergabe gerade
        // steht") - Fortschritt kommt aus dem Audiothread, hier nur
        // abgeholt und weitergereicht. Laeuft auch im Freeze/History-Modus:
        // scope.feed() oben steigt dort zwar sofort aus, der Cursor bleibt
        // davon unabhaengig.
        scope.setPlaybackProgress (dopplerfeldProcessor.scopePlaybackProgress(),
                                   dopplerfeldProcessor.isScopePlaybackAudible());

        // Der Processor stellt den Play-Modus beim Neuvorbereiten zurueck
        // (prepareToPlay, z.B. Puffergroessen- oder Samplerate-Wechsel im
        // Host). Ohne diese Nachfuehrung behauptete der Knopf danach weiter
        // "an", waehrend am Ausgang laengst wieder das Dopplersignal steht.
        if (scope.isPlaybackEnabled() && ! dopplerfeldProcessor.isScopePlaybackModeEnabled())
        {
            scope.setPlaybackEnabled (false);
            updateScopePlayButton (false);
        }

        // Bestaetigungstext nach dem Speichern nur ein paar Sekunden stehen
        // lassen, nicht dauerhaft im Toolbar rumstehen.
        if (scopeSaveStatusUntilMs != 0 && juce::Time::getMillisecondCounter() > scopeSaveStatusUntilMs)
        {
            scopeSaveStatusLabel.setText ({}, juce::dontSendNotification);
            scopeSaveStatusUntilMs = 0;
        }
    }

    // Statuszeile UND die CPU-Zeile darueber neu zeichnen, nicht das ganze
    // Fenster - die Panels darüber ändern sich nur bei Bedienung.
    // Ueber die ganze Breite, so weit die Statuszeile reicht (siehe paint()).
    repaint (margin, getHeight() - statusHeight - cpuMeterBlockHeight,
             getWidth() - 2 * margin, cpuMeterBlockHeight + statusHeight);
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

    // Tempo und L-M stehen nicht in dieser Zeile: beide sind gross und gelb im
    // Cockpit-Display im Feld zu sehen (@dpa 20260828: "Die Angaben 2695.8
    // km/h L-M 820.7 m sind doch die, die im Display oben, gelb stehen? Dann
    // brauchen sie doch nicht auf debug zu sein!?"). Was hier steht, ist das,
    // was es sonst nirgends gibt - und das ist alles vergaenglich, steht also
    // im Nachleuchten (siehe updateStatusFlashes).

    // Die CPU-Last steht in der eigenen Zeile direkt darueber (siehe paint(),
    // cpuMeterBlockHeight), nicht in dieser Zeile - dort mit Balken statt nur
    // als Zahl, und immer sichtbar statt nur, wenn dieser Text gerade Platz
    // hat.

    // @dpa-Feedback: Einzelne Pfade (L/R, M_r, Zweige) sind zu klein und zu
    // viel für die Statuszeile - nur die Anzahl aktiver Mehrfachreflexionen
    // bleibt als grobe Andeutung, was gerade gerechnet wird.
    // Alles Vergaengliche steht im Nachleuchten (siehe updateStatusFlashes /
    // StatusFlash im Header): Mehrfachreflexionen, Aufnahme/Wiedergabe und
    // die Zweig-Abrisse. Diese Zeile traegt nur, was immer gilt.

    return text;
}

void DopplerfeldEditor::noteStatusFlash (const juce::String& key, const juce::String& text,
                                        juce::Colour liveBackground)
{
    for (auto& f : statusFlashes)
        if (f.key == key)
        {
            f.text     = text;
            f.age      = 0.0;
            f.live     = true;
            f.liveBack = liveBackground;
            return;
        }

    statusFlashes.push_back ({ key, text, 0.0, true, liveBackground });
}

void DopplerfeldEditor::updateStatusFlashes (double deltaSeconds)
{
    for (auto& f : statusFlashes)
    {
        f.age += deltaSeconds;
        f.live = false;
    }

    // Mehrfachreflexionen: eine grobe Andeutung, was gerade gerechnet wird.
    {
        int higherOrder = 0;

        for (int i = 0; i < snapshot.pathCount; ++i)
            if (snapshot.paths[(size_t) i].order > 1)
                ++higherOrder;

        if (higherOrder > 0)
            noteStatusFlash ("refl", "+" + juce::String (higherOrder) + " Mehrfachrefl.");
    }

    // Aufnahme und Wiedergabe hinterlegt, in denselben zwei Farben wie die
    // Knoepfe im Bewegung-Panel (@dpa 20260828: "Wiedergabe sollte gruen
    // hinterlegt werden, so wie Bewegung > Play/Stop") - dieselbe Sache,
    // dieselbe Farbe, egal wo man hinschaut.
    if (dopplerfeldProcessor.isRecording())
        noteStatusFlash ("motion", Labels::text ("Aufnahme") + " "
                                     + juce::String (dopplerfeldProcessor.recordedFrameCount())
                                     + " Frames",
                         juce::Colours::red.withAlpha (0.45f));
    else if (dopplerfeldProcessor.isPlayingMotion())
        noteStatusFlash ("motion", Labels::text ("Wiedergabe"),
                         juce::Colours::limegreen.withAlpha (0.4f));

    // Zweig-Abrisse (@dpa 20260819): ein Hoerweg verschwindet, und die
    // Anti-Klick-Rampe faehrt ihn in einer Millisekunde auf null. Die Zahl
    // dahinter ist der PEGEL, bei dem das passiert ist - nahe 1 heisst
    // abgeschnitten, obwohl er noch voll toente, nahe 0 heisst, er war
    // ohnehin ausgeklungen. Zwei Werte: Mittel und schlimmster Fall.
    //
    // Name und Werte muessen selbsterklaerend sein - eine Abkuerzung ist zu
    // kurz, um sie zu erraten, und zu fluechtig, um sie nachzuschlagen (@dpa:
    // "es steht dort manchmal etwas mit 'Env'? es kommt zu selten als dass
    // ich den Sinn begriefen konnte").
    // Aktiv nur, wenn gerade welche DAZUGEKOMMEN sind. Der Zaehler laeuft
    // kumulativ weiter; "groesser als null" liess den Abschnitt dauerhaft
    // hell stehen, und dann sagt er nichts mehr ueber den Moment.
    const bool deathsGrew = snapshot.branchDeaths > lastBranchDeaths;

    lastBranchDeaths = snapshot.branchDeaths;

    if (deathsGrew)
    {
        const double loudShare = 100.0 * (double) snapshot.loudBranchDeaths
                                       / (double) snapshot.branchDeaths;

        noteStatusFlash ("deaths",
                         Labels::text ("Zweig-Abriss") + " "
                             + juce::String ((int) snapshot.branchDeaths)
                             + ", " + Labels::text ("Pegel dabei") + " "
                             + juce::String::formatted ("%.2f", snapshot.branchDeathEnvMean)
                             + " / max " + juce::String::formatted ("%.2f", snapshot.branchDeathEnvMax)
                             + ", " + juce::String::formatted ("%.0f", loudShare) + " % "
                             + Labels::text ("laut"));
    }

    // Was ganz ausgeblendet ist, kann weg.
    statusFlashes.erase (std::remove_if (statusFlashes.begin(), statusFlashes.end(),
                                         [] (const StatusFlash& f)
                                         {
                                             return f.age > statusHoldSeconds + statusFadeSeconds;
                                         }),
                         statusFlashes.end());
}

void DopplerfeldEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::editorBackground);

    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.setFont (13.0f);
    g.drawText ("dopplerfeld", margin, margin, 100, topBarHeight, juce::Justification::centredLeft);

    // Bauzeit dieser Fassung, rechts aussen in der Kopfzeile. Ohne sie laesst
    // sich von aussen nicht unterscheiden, ob eine Aenderung nicht wirkt oder ob
    // schlicht eine aeltere Fassung laeuft. __DATE__/__TIME__ stehen beim
    // Uebersetzen fest, es gibt also nichts zu pflegen.
    //
    // Rechts und nicht neben dem Schriftzug: links reicht der Platz nur bis zum
    // ersten Knopf der Kopfzeile, dahinter verschwindet die Zeile unter ihm.
    // Rechts bleibt hinter dem letzten Knopf genug Rest, und rechtsbuendig
    // gezeichnet waechst sie in den freien Raum hinein statt in die Knopfreihe.
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                              10.0f, juce::Font::plain)));
    g.drawText (juce::String ("Build ") + __DATE__ + " " + __TIME__,
                getWidth() - margin - 140, margin, 140, topBarHeight,
                juce::Justification::centredRight);

    // Loeserlast-Zeile: eigener Balken + Zahl, IMMER sichtbar (siehe
    // cpuMeterBlockHeight im Header) - unabhaengig davon, ob ein Panel
    // (insbesondere das Schwarm-Panel) gerade auf- oder zugeklappt ist, denn
    // sie ist die Warnung vor hoerbaren Aussetzern.
    {
        // Gemittelter Wert wie im Rest der Statuszeile (s. updateDisplayAverages,
        // @dpa-Feedback "Langsamkeit der Anzeigewahrnehmung") - Balken und Zahl
        // muessen denselben Wert zeigen, sonst widersprechen sie sich optisch.
        const float cpu = (float) displayAverages.cpuPercent;

        const int meterTop = getHeight() - statusHeight - cpuMeterBlockHeight;
        const auto bar = juce::Rectangle<int> (margin, meterTop, fieldWidth, cpuMeterBarHeight).toFloat();

        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (bar, 2.0f);

        // Der Balken zeigt bis 150 %, nicht bis 100: der interessante Bereich
        // beginnt dort, wo es knapp wird, und ein Balken, der bei 100 % einfach
        // anschlaegt, verschweigt genau das.
        constexpr float fullScale = 150.0f;
        const float filled = juce::jlimit (0.0f, 1.0f, cpu / fullScale);

        const juce::Colour meterColour = cpu > 100.0f ? juce::Colours::orangered
                                        : cpu >  70.0f ? juce::Colours::orange
                                                       : juce::Colours::limegreen;

        g.setColour (meterColour.withAlpha (0.75f));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * filled), 2.0f);

        // Marke bei 100 %, damit der Balken eine Bezugsgroesse hat.
        const float markX = bar.getX() + bar.getWidth() * (100.0f / fullScale);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawLine (markX, bar.getY(), markX, bar.getBottom(), 1.0f);

        // CPU über 100% ist hörbar (Aussetzer) - die Beschriftung faerbt sich
        // dafuer zusaetzlich rot, dieselbe Farbe wie der Balken selbst.
        g.setColour (cpu > 100.0f ? juce::Colours::orangered.withAlpha (0.85f)
                                  : juce::Colours::white.withAlpha (0.75f));
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                                  11.0f, juce::Font::plain)));

        g.drawText (juce::String::formatted ("CPU %4.0f %%   Klone: %d",
                                             (double) cpu, dopplerfeldProcessor.realCloneCount()),
                    margin, meterTop + cpuMeterBarHeight + 2, fieldWidth, cpuMeterLabelHeight,
                    juce::Justification::centredLeft);

    }

    // Statuszeile selbst (Tempo/L-M/Reflexionen/...) faerbt sich nicht rot -
    // diese CPU-Warnung sitzt ausschliesslich im Balken darueber. (Die Farbe
    // wird weiter unten kurz vor drawText() gesetzt, nachdem die LED links
    // davon gezeichnet ist.)
    // Monospace statt Proportionalschrift: nur bei fester Zeichenbreite pro
    // Glyphe hält das Zahlen-Padding in statusText() die Spalten auch
    // tatsaechlich stabil (siehe Kommentar dort). drawText() statt
    // drawFittedText(): Letzteres skaliert die Schrift nach, wenn die Zeile
    // (durch "+N Mehrfachrefl." oder "Aufnahme"/"Wiedergabe" am Ende) mal
    // laenger, mal kuerzer wird - dann "atmet" die ganze Zeile mit, selbst
    // die fest gepaddeten Spalten davor. drawText() zeichnet immer in der
    // gesetzten Groesse und schneidet im Zweifel einfach ab.
    const int statusTop = getHeight() - statusHeight;

    // LED vor dem Text statt einer weiteren grauen Zahl darin (@dpa-Auftrag,
    // s. Kommentar bei statusStateDotDiameter im Header): rot waehrend der
    // Aufnahme, gruen waehrend der Wiedergabe, sonst unsichtbar. Dieselben
    // zwei Farben wie am Record-/Play-Knopf im Bewegung-Panel (siehe
    // MotionPanel::setRecording()/setPlaying()) - Statuszeile und Knopf
    // zeigen also denselben Zustand in derselben Farbe.
    const bool motionRecording = dopplerfeldProcessor.isRecording();
    const bool motionPlaying   = dopplerfeldProcessor.isPlayingMotion();

    const juce::Colour stateDotColour = motionRecording ? juce::Colours::red
                                       : motionPlaying   ? juce::Colours::limegreen
                                                          : juce::Colours::transparentBlack;

    const auto stateDotBounds = juce::Rectangle<float> ((float) margin,
                                                         (float) (statusTop + (statusHeight - statusStateDotDiameter) / 2),
                                                         (float) statusStateDotDiameter,
                                                         (float) statusStateDotDiameter);
    g.setColour (stateDotColour);
    g.fillEllipse (stateDotBounds);

    const int textLeft  = margin + statusStateDotDiameter + statusStateDotGap;

    // Ueber die ganze Fensterbreite, nicht nur ueber die des Feldes: unter der
    // Reglerspalte ist hier nichts, und die verganglichen Abschnitte dahinter
    // (Zweig-Abriss und Co.) brauchen den Platz - sonst passt der laengste
    // von ihnen nicht hinein und wird GAR NICHT gezeichnet, statt nur zu kurz
    // sichtbar zu sein.
    const int textWidth = getWidth() - margin - textLeft;

    const juce::Font statusFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                                    16.0f, juce::Font::plain));
    g.setFont (statusFont);

    const juce::String fixedPart = statusText();

    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.drawText (fixedPart,
                textLeft, statusTop, textWidth, statusHeight,
                juce::Justification::centredLeft, false);

    // Die verganglichen Abschnitte dahinter, jeder mit seiner eigenen
    // Deckkraft: solange er aktiv ist (und statusHoldSeconds danach) voll,
    // dann ueber statusFadeSeconds heruntergeblendet. Monospace, deshalb
    // laesst sich die Breite des Vorangegangenen exakt ausrechnen, statt sie
    // zu schaetzen.
    {
        int x = textLeft + (int) std::ceil (juce::GlyphArrangement::getStringWidth (statusFont, fixedPart));

        for (const auto& f : statusFlashes)
        {
            const double over = f.age - statusHoldSeconds;
            const double fade = over <= 0.0 ? 1.0
                                            : juce::jlimit (0.0, 1.0, 1.0 - over / statusFadeSeconds);

            if (fade <= 0.0)
                continue;

            const juce::String piece = " " + f.text + " ";
            const int width = (int) std::ceil (juce::GlyphArrangement::getStringWidth (statusFont, piece));

            if (x - textLeft + width > textWidth)
                break;

            // Laufend heisst voll weiss, nachleuchtend heisst deutlich
            // dunkler und dann weg. Der Abstand zwischen beiden ist der
            // eigentliche Zweck der Zeile: was JETZT passiert, soll ins Auge
            // springen, was gerade war, nur noch nachlesbar sein.
            const float alpha = f.live ? statusLiveAlpha
                                       : (float) (statusEchoAlpha * fade);

            // Hinterlegung nur, solange es laeuft - ein nachleuchtender
            // Abschnitt ist kein Zustand mehr.
            if (f.live && ! f.liveBack.isTransparent())
            {
                g.setColour (f.liveBack);
                g.fillRoundedRectangle ((float) x, (float) (statusTop + 2),
                                        (float) width, (float) (statusHeight - 4), 2.0f);
            }

            g.setColour (juce::Colours::white.withAlpha (alpha));
            g.drawText (piece, x, statusTop, width, statusHeight,
                        juce::Justification::centredLeft, false);

            x += width + 8;
        }
    }
}

void DopplerfeldEditor::resized()
{
    auto area = getLocalBounds().reduced (margin);

    auto topBar = area.removeFromTop (topBarHeight);

    topBar.removeFromLeft (100);   // Platz für den Schriftzug links

    // Der Hauptschalter steht abgesetzt HINTER dem Schriftzug, nicht davor:
    // direkt davor gelesen ergaeben Schalter und Name zusammen "An
    // dopplerfeld", also einen Satz statt zweier getrennter Dinge.
    topBar.removeFromLeft (10);
    masterOnButton.setBounds (topBar.removeFromLeft (50));
    topBar.removeFromLeft (14);
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
    topBar.removeFromLeft (8);
    // Sprachumschalter DE/EN, bewusst schmal (Kompaktheit auf dem Panel).
    languageButton.setBounds (topBar.removeFromLeft (40));

    area.removeFromTop (4);
    presetBar.setBounds (area.removeFromTop (presetBarHeight));
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
        scopeToolbar.removeFromLeft (8);
        scopeEventButton.setBounds (scopeToolbar.removeFromLeft (90));
        scopeToolbar.removeFromLeft (4);
        scopeHoldCombo.setBounds (scopeToolbar.removeFromLeft (70));
        scopeToolbar.removeFromLeft (16);
        scopeZoomOutButton.setBounds (scopeToolbar.removeFromLeft (28));
        scopeToolbar.removeFromLeft (4);
        scopeZoomInButton.setBounds (scopeToolbar.removeFromLeft (28));
        scopeToolbar.removeFromLeft (16);
        scopeSaveButton.setBounds (scopeToolbar.removeFromLeft (90));
        scopeToolbar.removeFromLeft (8);
        scopePlayButton.setBounds (scopeToolbar.removeFromLeft (90));
        scopeToolbar.removeFromLeft (8);
        scopeSaveStatusLabel.setBounds (scopeToolbar);

        fieldArea.removeFromTop (4);
        scope.setBounds (fieldArea.removeFromTop (scopeHeight));
    }

    area.removeFromLeft (margin);
    panelViewport.setBounds (area);

    layoutPanels();

    // Volle Editorflaeche, ungeachtet der obigen Aufteilung - das Overlay
    // legt sich darueber, nicht daneben.
    welcomeOverlay.setBounds (getLocalBounds());
}

std::array<CollapsiblePanel*, 8> DopplerfeldEditor::panelBoxes()
{
    return { &engineControlPanelBox, &enginePanelBox, &samplePanelBox,
             &motionPanelBox, &fieldPanelBox, &wallPanelBox, &reverbPanelBox, &swarmPanelBox };
}

void DopplerfeldEditor::storePanelOpenMask()
{
    int mask = 0;
    int bit  = 1;

    for (auto* box : panelBoxes())
    {
        if (box->isExpanded())
            mask |= bit;

        bit <<= 1;
    }

    dopplerfeldProcessor.setPanelOpenMask (mask);
}

void DopplerfeldEditor::applyPanelOpenMask (int mask)
{
    int bit = 1;

    for (auto* box : panelBoxes())
    {
        box->setExpanded ((mask & bit) != 0);
        bit <<= 1;
    }

    layoutPanels();
}

void DopplerfeldEditor::layoutPanels()
{
    const int width = juce::jmax (100, panelViewport.getMaximumVisibleWidth());

    struct Entry { CollapsiblePanel* box; int contentHeight; };

    const Entry entries[] {
        { &engineControlPanelBox, engineControlContentHeight },
        // Das Motor-Panel meldet seine Hoehe selbst: sie haengt an der
        // gewaehlten Betriebsart, weil dort nur steht, was diese Betriebsart
        // auch braucht (siehe EnginePanel::preferredContentHeight).
        { &enginePanelBox, enginePanel.preferredContentHeight() },
        { &samplePanelBox, sampleContentHeight },
        { &motionPanelBox, motionContentHeight },
        { &fieldPanelBox,  fieldContentHeight  },
        { &wallPanelBox,   wallContentHeight   },
        { &reverbPanelBox, reverbContentHeight },
        { &swarmPanelBox,  swarmContentHeight  }
    };

    int y = 0;

    for (const auto& entry : entries)
    {
        // Aufgeklappt kommt zur Inhaltshoehe die Luft unter dem Inhalt dazu
        // (CollapsiblePanel::contentPaddingBottom) - sonst schneidet das Panel
        // die letzte Reglerreihe an.
        const int height = entry.box->isExpanded()
                               ? CollapsiblePanel::headerHeight + entry.contentHeight
                                     + CollapsiblePanel::contentPaddingBottom
                               : CollapsiblePanel::headerHeight;

        entry.box->setBounds (0, y, width, height);
        y += height + 4;
    }

    panelHolder.setSize (width, y);
}
