// Messprogramm zum Play-Knopf am Scope (@dpa 20260826: "play an schalten: es
// spielt die Scopeansicht von vorn bis hinten und geht dann auf null. wenn
// play an bleibt, kann man mit der maus im Scope clicken und dadurch an
// bestimmten stellen starten (wieder bis hinten)").
//
// Gibt einen bekannten Ausschnitt in die Wiedergabe und misst am Ausgang des
// Processors, was daraus wird: wann er zu hoeren ist, wann er aufhoert, was
// ein zweiter Start mittendrin macht und was beim Ausschalten passiert.
// Gemessen wird hinter der kompletten Ausgangsstufe, also an dem, was der
// Host bekaeme.
//
// Eigenes Target, nicht Teil von ctest:
//   cmake --build build --target scope_play_probe && build/scope_play_probe

#include "Params.h"
#include "PluginProcessor.h"
#include "UI/ScopeComponent.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int    blockSize  = 512;

int failures = 0;

void check (const char* label, double got, double want, double tolerance)
{
    const bool ok = std::abs (got - want) <= tolerance;

    if (! ok)
        ++failures;

    std::printf ("  %-52s %9.5f  (erwartet %7.5f +-%.3f)  %s\n",
                 label, got, want, tolerance, ok ? "ok" : "FEHLER");
}

// Spielt n Bloecke und liefert den groessten Betrag, der dabei am Ausgang
// stand.
double runBlocks (DopplerfeldProcessor& proc, int blocks)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer         midi;

    double peak = 0.0;

    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            peak = std::max (peak, (double) buffer.getMagnitude (ch, 0, blockSize));
    }

    return peak;
}

// Wie runBlocks(), liefert aber den kompletten Ausgang BEIDER Kanaele
// GETRENNT zurueck. Der blosse Spitzenwert oben reicht fuer die drei
// Symptome unten nicht: ein vertauschter oder verschobener Kanal haette
// denselben Spitzenwert wie der richtige.
void runBlocksCapture (DopplerfeldProcessor& proc, int blocks,
                       std::vector<float>& outL, std::vector<float>& outR)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer         midi;

    outL.clear();
    outR.clear();
    outL.reserve ((size_t) (blocks * blockSize));
    outR.reserve ((size_t) (blocks * blockSize));

    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);

        const float* l = buffer.getReadPointer (0);
        const float* r = buffer.getReadPointer (1);

        for (int i = 0; i < blockSize; ++i)
        {
            outL.push_back (l[i]);
            outR.push_back (r[i]);
        }
    }
}

bool allFinite (const std::vector<float>& v)
{
    for (float x : v)
        if (! std::isfinite (x))
            return false;

    return true;
}

// Groesster Betrag in v[from .. from+count).
double maxAbsIn (const std::vector<float>& v, int from, int count)
{
    double m = 0.0;

    for (int i = from; i < from + count; ++i)
        m = std::max (m, (double) std::abs (v[(size_t) i]));

    return m;
}

// Symptom (a) betrifft nicht nur den Processor, sondern schon die Index-
// rechnung IM Scope: mouseUp() rechnet den Klick-x in eine Sample-Position
// um, gestuetzt auf shownSampleCountForTest(). Das ist im History-Modus
// (nach Freeze) ein eigener Fall - siehe Kommentar dort im Header. Dieser
// Test prueft direkt an der ScopeComponent, ohne Maus/Fenster, dass die
// Zahl, die die Klickrechnung benutzt, dieselbe ist wie die, die paint()
// tatsaechlich zeichnet (traceCount dort).
void checkScopeComponentHistoryClickMath()
{
    std::printf ("\nScopeComponent: Klick-Skalierung im History-Modus (Symptom a):\n");

    ScopeComponent scope;

    constexpr double sr = 48000.0;
    scope.setSampleRateHint (sr);
    scope.setMaxDisplaySampleCount (1 << 20);
    scope.setDisplaySeconds (0.02, sr);   // 960 Samples, wie im load_check-Vorbild
    scope.setSyncEnabled (true);

    const int captureLen = scope.captureWindowSampleCount();

    // 220 Hz bei 48 kHz: rund 218,2 Samples/Periode, geht NICHT ganzzahlig in
    // 960 Samples auf - periodAlignedLength() zeichnet dadurch kuerzer als
    // displaySampleCount(), genau der Fall, der shownSampleCount vor dem
    // Freeze veraltern laesst. Mit Oberton (wie im load_check-Vorbild, nicht
    // ein reiner Sinus): buildSyncLowpass() schaetzt ihre Grenzfrequenz aus
    // der GESAMTEN Nulldurchgangsrate und zieht sie bewusst unter die
    // Grundwelle (siehe Kommentar dort) - bei einem reinen Sinus OHNE
    // Obertoene traefe die Grenzfrequenz die Grundwelle selbst und daempfte
    // sie mit weg, das faende dann gar keinen Sync mehr.
    std::vector<float> rawL ((size_t) captureLen), rawR ((size_t) captureLen);

    for (int n = 0; n < captureLen; ++n)
    {
        const double t = (double) n / sr;
        const double v = 0.5 * std::sin (juce::MathConstants<double>::twoPi * 220.0 * t)
                        + 0.45 * std::sin (juce::MathConstants<double>::twoPi * 7.0 * 220.0 * t + 0.7);
        rawL[(size_t) n] = (float) v;
        rawR[(size_t) n] = rawL[(size_t) n];
    }

    scope.feed (rawL.data(), rawR.data(), (std::uint32_t) captureLen);

    const int shownLive = scope.shownSampleCountForTest();
    std::printf ("  %-52s %9d  (displaySampleCount() = %d, Periode %.1f)\n",
                "gezeigt im Live-Sync, vor Freeze", shownLive, scope.displaySampleCount(),
                scope.periodSamplesForTest());

    // Einfrieren: der Editor uebergibt dabei sonst die komplette
    // Ringpuffer-Historie - hier reicht fuer den Test dasselbe Rohfenster
    // noch einmal, enterHistoryMode() sucht darin ohnehin neu.
    scope.enterHistoryMode (rawL.data(), rawR.data(), captureLen);

    const int shownFrozen = scope.shownSampleCountForTest();
    std::printf ("  %-52s %9d\n", "gezeigt im History-Modus, nach Freeze", shownFrozen);

    // paint() zeichnet im History-Modus IMMER displaySampleCount() (siehe
    // traceCount in ScopeComponent.cpp) - genau das muss auch hier
    // herauskommen, sonst rechnet ein Klick mit einer anderen Laenge als der,
    // die zu sehen ist.
    check ("gezeigt(History) == displaySampleCount()",
          (double) shownFrozen, (double) scope.displaySampleCount(), 0.0);
}
}

int main()
{
    // Fuer checkScopeComponentHistoryClickMath() unten: eine ScopeComponent
    // ist ein juce::Component, das braucht JUCEs GUI-Subsystem einmalig
    // initialisiert (wie in Tests/editor_shot.cpp), auch wenn hier nichts
    // sichtbar gezeichnet wird.
    juce::ScopedJuceInitialiser_GUI juceInit;

    checkScopeComponentHistoryClickMath();

    DopplerfeldProcessor proc;
    proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
    proc.prepareToPlay (sampleRate, blockSize);

    // Der Ausschnitt, den das Scope "sieht": 0,1 s Sinus, links doppelt so
    // laut wie rechts - daran laesst sich auch die Kanalzuordnung ablesen.
    constexpr int   windowSamples = 4800;
    constexpr float leftAmp       = 0.50f;
    constexpr float rightAmp      = 0.25f;

    std::vector<float> left ((size_t) windowSamples), right ((size_t) windowSamples);

    for (int i = 0; i < windowSamples; ++i)
    {
        const double phase = juce::MathConstants<double>::twoPi * 500.0 * (double) i / sampleRate;
        left [(size_t) i] = leftAmp  * (float) std::sin (phase);
        right[(size_t) i] = rightAmp * (float) std::sin (phase);
    }

    // Bezugspegel: mit den Voreinstellungen laeuft der Motor, am Ausgang
    // steht also etwas. Genau das macht "geht dann auf null" ueberhaupt
    // messbar - die Stille zwischen zwei Abspielvorgaengen ist dann eine
    // Aussage und nicht der Zustand, der ohnehin herrschte.
    const double idlePeak = runBlocks (proc, 20);

    std::printf ("Ausgang ohne Play (Bezug):\n");
    check ("Dopplersignal hoerbar", idlePeak > 0.05 ? 1.0 : 0.0, 1.0, 0.0);
    std::printf ("  %-52s %9.5f\n", "Spitzenwert", idlePeak);

    // 1) Play einschalten = der sichtbare Ausschnitt laeuft einmal durch.
    std::printf ("\nPlay an, ganzer Ausschnitt (0,1 s):\n");

    proc.setScopePlaybackModeEnabled (true);
    proc.requestScopePlayback (left.data(), right.data(), windowSamples);

    // 4800 Samples = 9,375 Bloecke; 10 Bloecke decken sie ab.
    check ("Spitzenwert waehrend der Wiedergabe", runBlocks (proc, 10), (double) leftAmp, 0.02);

    // 2) Danach Stille, obwohl der Toggle an bleibt ("geht dann auf null").
    check ("Spitzenwert danach (Toggle bleibt an)", runBlocks (proc, 20), 0.0, 1.0e-4);

    // 3) Klick im Scope: ab der halben Strecke bis hinten - dieselbe Anfrage,
    // nur mit verschobenem Zeiger und kuerzerer Laenge, genau das reicht der
    // Editor beim Klick ein.
    std::printf ("\nKlick auf die Mitte, Rest bis hinten:\n");

    constexpr int clickOffset = windowSamples / 2;

    proc.requestScopePlayback (left.data() + clickOffset, right.data() + clickOffset,
                               windowSamples - clickOffset);

    check ("Spitzenwert waehrend der Wiedergabe", runBlocks (proc, 6), (double) leftAmp, 0.02);
    check ("Spitzenwert danach", runBlocks (proc, 20), 0.0, 1.0e-4);

    // 4) Ausschalten: der Ausgang gehoert wieder dem Dopplersignal. Ohne
    // Quelle ist das die Stille von oben - gemessen wird hier, dass die
    // Umschaltung selbst nichts stehen laesst.
    std::printf ("\nPlay aus:\n");

    proc.setScopePlaybackModeEnabled (false);

    // Verglichen wird die Groessenordnung, nicht der Wert: das Dopplersignal
    // ist ein laufender Motor, sein Spitzenwert ist von Block zu Block ein
    // anderer.
    const double afterOffPeak = runBlocks (proc, 20);

    check ("Ausgang wieder da (Anteil des Bezugspegels)",
           afterOffPeak / juce::jmax (1.0e-9, idlePeak), 1.0, 0.35);

    // 5) Mitten hinein noch einmal starten: der laufende Vorgang wird
    // ausgeblendet und der neue uebernimmt, ohne dass der Ausgang dabei
    // ueber den Ausschnitt hinausschlaegt (kein harter Quellwechsel).
    std::printf ("\nZweiter Start mitten in die laufende Wiedergabe:\n");

    proc.setScopePlaybackModeEnabled (true);
    proc.requestScopePlayback (left.data(), right.data(), windowSamples);
    runBlocks (proc, 3);

    proc.requestScopePlayback (left.data(), right.data(), windowSamples);

    check ("Spitzenwert beim Umschalten", runBlocks (proc, 12), (double) leftAmp, 0.02);
    check ("Spitzenwert danach", runBlocks (proc, 20), 0.0, 1.0e-4);

    // 6) Positionstreue UND Kanaltrennung, bit-genau (Symptome a+b). Der
    // blosse Spitzenwert oben (Tests 1-5) kann eine Verschiebung oder eine
    // Kanalvertauschung nicht aufdecken - eine phasenverschobene oder
    // vertauschte Kurve haette denselben Spitzenwert. Hier werden L und R
    // ABSICHTLICH unterschiedlich laut UND unterschiedlich in der Frequenz
    // gemacht, ab einer Stelle abseits von Anfang/Mitte "geklickt", und der
    // Ausgang danach Sample fuer Sample mit der Quelle an DERSELBEN Position
    // verglichen - nicht nur sein Pegel.
    std::printf ("\nPositionstreue + Kanaltrennung, bit-genau (Symptome a+b):\n");
    {
        constexpr int    n2     = 4800;
        constexpr float  ampL   = 0.6f;
        constexpr float  ampR   = 0.3f;
        constexpr double freqL  = 440.0;
        constexpr double freqR  = 660.0;

        std::vector<float> srcL ((size_t) n2), srcR ((size_t) n2);

        for (int i = 0; i < n2; ++i)
        {
            srcL[(size_t) i] = ampL * (float) std::sin (juce::MathConstants<double>::twoPi * freqL * (double) i / sampleRate);
            srcR[(size_t) i] = ampR * (float) std::sin (juce::MathConstants<double>::twoPi * freqR * (double) i / sampleRate);
        }

        // "Klick" bei 40% der Strecke - bewusst weder 0 noch die Mitte, damit
        // ein Fehler, der zufaellig genau dort verschwindet, nicht uebersehen
        // wuerde.
        constexpr int clickOffset2 = (int) (0.4 * n2);
        const int     playLen      = n2 - clickOffset2;

        proc.setScopePlaybackModeEnabled (true);
        proc.requestScopePlayback (srcL.data() + clickOffset2, srcR.data() + clickOffset2, playLen);

        std::vector<float> outL, outR;
        runBlocksCapture (proc, playLen / blockSize + 4, outL, outR);

        check ("Ausgang durchgehend endlich (kein NaN/Inf)",
              allFinite (outL) && allFinite (outR) ? 1.0 : 0.0, 1.0, 0.0);

        // Exakt-Fenster: nach beiden Einblend-Rampen (scopePlaybackModeFade-
        // Seconds = 8 ms, scopePlaybackShotFadeSeconds = 3 ms, hier mit
        // reichlich Abstand ab Sample 450) und vor dem Ausblenden am
        // Pufferende (setzt bei playLen - Fadelaenge ein, hier mit Abstand
        // bis playLen - 250) steht beide Rampen exakt bei 1.0 - der Ausgang
        // muss dort BIT-GENAU dem Quellsample an derselben Position
        // entsprechen, links wie rechts.
        const int settleStart = 450;
        const int settleEnd   = juce::jmin (playLen - 250, (int) outL.size());

        double maxErrL = 0.0, maxErrR = 0.0;

        for (int i = settleStart; i < settleEnd; ++i)
        {
            maxErrL = std::max (maxErrL, (double) std::abs (outL[(size_t) i] - srcL[(size_t) (clickOffset2 + i)]));
            maxErrR = std::max (maxErrR, (double) std::abs (outR[(size_t) i] - srcR[(size_t) (clickOffset2 + i)]));
        }

        check ("L bit-genau an der geklickten Stelle (max. Abweichung)", maxErrL, 0.0, 1.0e-6);
        check ("R bit-genau an der geklickten Stelle (max. Abweichung)", maxErrR, 0.0, 1.0e-6);
    }

    // 7) Randfaelle der Puffergrenzen (Symptom c: "digitales Rauschen").
    // Laenge 0 (ganz rechts geklickt) und Laenge 1 (kuerzest moegliche
    // Wiedergabe, Fade-in und Fade-out fallen ins selbe Sample) duerfen
    // weder einen Ausreisser noch nicht-endliche Werte erzeugen.
    std::printf ("\nRandfaelle Puffergrenzen (Symptom c):\n");
    {
        constexpr int tiny = 8;
        std::vector<float> tinyL ((size_t) tiny), tinyR ((size_t) tiny);

        for (int i = 0; i < tiny; ++i)
        {
            tinyL[(size_t) i] = 0.9f;
            tinyR[(size_t) i] = -0.9f;
        }

        proc.requestScopePlayback (tinyL.data(), tinyR.data(), 0);

        std::vector<float> outZeroL, outZeroR;
        runBlocksCapture (proc, 4, outZeroL, outZeroR);

        check ("Laenge 0: Ausgang endlich", allFinite (outZeroL) && allFinite (outZeroR) ? 1.0 : 0.0, 1.0, 0.0);
        // Erste ~200 Samples ausgenommen: dort kann noch die Ausblende-Rampe
        // des VORIGEN Abspielvorgangs (Test 6) sauber auslaufen.
        check ("Laenge 0: danach praktisch still",
              maxAbsIn (outZeroL, 200, (int) outZeroL.size() - 200), 0.0, 1.0e-3);

        proc.requestScopePlayback (tinyL.data(), tinyR.data(), 1);

        std::vector<float> outOneL, outOneR;
        runBlocksCapture (proc, 4, outOneL, outOneR);

        check ("Laenge 1: Ausgang endlich", allFinite (outOneL) && allFinite (outOneR) ? 1.0 : 0.0, 1.0, 0.0);
        check ("Laenge 1: kein Ausreisser ueber die Quellamplitude hinaus",
              maxAbsIn (outOneL, 0, (int) outOneL.size()) <= 1.0 ? 1.0 : 0.0, 1.0, 0.0);
    }

    // 8) Zwei Anfragen OHNE dazwischenliegenden Block - das Gegenstueck zu
    // Test 5 (Umschalten waehrend der Wiedergabe), nur enger: hier kommt der
    // zweite Klick, BEVOR der Audiothread den ersten ueberhaupt einmal
    // gesehen hat (schnellste moegliche Doppelklick-Folge). Erwartet wird ein
    // sauberer Ausgang - der zweite Ausschnitt gewinnt, nichts wird
    // ueberschrieben, waehrend etwas anderes noch daraus liest.
    std::printf ("\nZwei Anfragen ohne dazwischenliegenden Block:\n");
    {
        constexpr int n3 = 2000;
        std::vector<float> aL ((size_t) n3), aR ((size_t) n3), bL ((size_t) n3), bR ((size_t) n3);

        for (int i = 0; i < n3; ++i)
        {
            aL[(size_t) i] = 0.7f * (float) std::sin (juce::MathConstants<double>::twoPi * 300.0 * (double) i / sampleRate);
            aR[(size_t) i] = aL[(size_t) i];
            bL[(size_t) i] = 0.4f * (float) std::sin (juce::MathConstants<double>::twoPi * 900.0 * (double) i / sampleRate);
            bR[(size_t) i] = bL[(size_t) i];
        }

        proc.setScopePlaybackModeEnabled (true);
        proc.requestScopePlayback (aL.data(), aR.data(), n3);
        proc.requestScopePlayback (bL.data(), bR.data(), n3);   // zweiter Klick, kein Block dazwischen

        std::vector<float> outA, outB;
        runBlocksCapture (proc, n3 / blockSize + 6, outA, outB);

        check ("Ausgang durchgehend endlich (kein NaN/Inf)",
              allFinite (outA) && allFinite (outB) ? 1.0 : 0.0, 1.0, 0.0);
        check ("Ausgang bleibt im gueltigen Amplitudenbereich",
              maxAbsIn (outA, 0, (int) outA.size()) <= 1.0 ? 1.0 : 0.0, 1.0, 0.0);
    }

    std::printf ("\n%s\n", failures == 0 ? "alles ok" : "FEHLER, siehe oben");
    return failures == 0 ? 0 : 1;
}
