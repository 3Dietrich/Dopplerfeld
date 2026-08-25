#include "DopplerEngine.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double twoPi = 6.283185307179586476925;

// Winkeldifferenz auf (-π, π] bringen, damit ein Sprung über den Nulldurchgang
// von yaw nicht als rasend schnelle Drehung in die Ohrgeschwindigkeit geht.
double wrapAngle (double a)
{
    a = std::fmod (a + 3.14159265358979323846, twoPi);

    if (a < 0.0)
        a += twoPi;

    return a - 3.14159265358979323846;
}
}

//======================================================================
// PathSet - ein kompletter Geometriesatz

void DopplerEngine::PathSet::prepare (double sampleRate, int maxBlockSize, size_t pathCount,
                                      double trajRateHz, double maxSeconds)
{
    sr = sampleRate;

    trajectory.prepare (trajRateHz, maxSeconds);

    paths.resize (pathCount);

    for (auto& p : paths)
    {
        p.prepare (sr, maxBlockSize);
        p.setTrajectoryGridSeconds (1.0 / trajRateHz);
    }

    // GEPRUEFT UND VERWORFEN: die Vollscans der Pfade ueber das
    // Entdeckungsintervall verteilen, damit nicht alle im selben Block scannen.
    // Bringt nichts, weil sich die Pfade ueber das gemeinsame Solver-Raster
    // wieder einsynchronisieren - gemessen stieg der teuerste Block bei
    // |M_r| 1,01 sogar von 28853 auf 44725 Auswertungen.

    // Die Abbildung selbst wird nicht hier gesetzt, sondern vor jedem Block aus
    // der Fläche geholt (renderInto): Wände dürfen sich bewegen, der Boden ist
    // nur der Sonderfall einer Fläche, die es nie tut.
}

void DopplerEngine::PathSet::reset (Vec3 pos, double time, const ListenerState& l)
{
    // Vorgeschichte konstant an pos (Plan 2.6): damit existiert von der ersten
    // Probe an mindestens eine Wurzel, egal ob der Satz gerade frisch
    // angefangen hat oder aus einem Sprung kommt.
    trajectory.reset (pos, time);
    lastPos = pos;

    listener     = l;
    prevListener = l;

    for (auto& p : paths)
        p.reset();
}

void DopplerEngine::PathSet::push (Vec3 pos, double t)
{
    trajectory.push (pos, t);
    lastPos = pos;
}

void DopplerEngine::PathSet::renderInto (juce::AudioBuffer<float>& dest, int numSamples)
{
    // Der Renderer-Vertrag verlangt Überschreiben, die Pfade addieren aber -
    // also hier einmal räumen, danach summieren sich mehrere Zweige und
    // mehrere Pfade wie gehabt auf.
    for (int ch = 0; ch < dest.getNumChannels(); ++ch)
        dest.clear (ch, 0, numSamples);

    if (signal == nullptr || medium == nullptr || recipes == nullptr
        || engine == nullptr || sr <= 0.0)
        return;

    const int numCh = dest.getNumChannels();
    const double dt = (double) numSamples / sr;

    // Position gilt zu blockStartTime, die Geschwindigkeit deckt den Block ab -
    // PropagationPath extrapoliert damit die Ohrposition an jedem Solver-Punkt
    // (Plan 3.5). Bei eingefrorenem Satz sind listener und prevListener gleich,
    // die Ohrgeschwindigkeit ist dann exakt 0.
    const Vec3 headVel = (listener.head - prevListener.head) * (1.0 / dt);

    // ω zeigt in z-Richtung. yaw wächst von +y nach +x, also mathematisch
    // negativ - deshalb das Minuszeichen (Herleitung in Listener.h).
    const double yawRate = -wrapAngle (listener.yaw - prevListener.yaw) / dt;

    for (size_t i = 0; i < paths.size() && i < recipes->size(); ++i)
    {
        const PathRecipe& recipe = (*recipes)[i];

        // Abgeschaltete Fläche heißt: der Spiegelpfad wird gar nicht erst
        // gerechnet. Sein Löser bleibt damit auf dem alten Zeitpunkt stehen und
        // sät sich beim Wiedereinschalten selbst neu, statt über eine Lücke zu
        // interpolieren.
        if (! engine->recipeEnabled (recipe))
            continue;

        // Wände dürfen wandern, deshalb vor jedem Block frisch übernehmen.
        // Beim Direktschall ist das die Identität, beim Boden immer dieselbe
        // Ebene - in beiden Fällen kostet es nur die Zuweisung.
        paths[i].setTransform (engine->recipeTransform (recipe));
        paths[i].setReflectionDamping (engine->recipeDamping (recipe),
                                       engine->recipeDampFcHz (recipe));

        const bool rightEar = (recipe.ear != 0);

        // Nach setTransform(), siehe dort: die Kopfachse wird mitgespiegelt.
        paths[i].setPanning (engine->panoramaAmount(), listenerRight (prevListener), rightEar);

        const Vec3 pos = earPosition (prevListener, rightEar);
        const Vec3 vel = earVelocity (prevListener, headVel, yawRate, rightEar);

        const int ch = std::min (recipe.ear, numCh - 1);

        paths[i].process (trajectory, *signal, *medium,
                          pos, vel, blockStartTime,
                          dest.getWritePointer (ch), numSamples);
    }
}

//======================================================================
// DopplerEngine

void DopplerEngine::prepare (double sampleRate, int maxBlockSize, double maxFieldMetres)
{
    sr       = sampleRate;
    maxBlock = std::max (1, maxBlockSize);

    // Längste mögliche Laufzeit: die Felddiagonale ist 1,152·n (Plan 2.1),
    // geteilt durch die langsamste je auftretende Schallgeschwindigkeit
    // (331,3 m/s bei 0 °C), plus Reserve. Bei n = 10000 sind das rund 42 s,
    // was der 40-s-Dimensionierung aus Plan 2.12 entspricht.
    //
    // Bemessen wird nach maxFieldMetres, nicht nach fieldMetres: die Puffer
    // stehen damit unabhängig vom aktuellen Feldmaßstab - erst das macht den
    // Feldgrößen-Crossfade weiter unten allokationsfrei möglich.
    const double diagonal = 1.152 * std::max (1.0, maxFieldMetres);
    maxHistorySeconds = std::max (0.5, diagonal / 331.3 * 1.2 + 0.1);

    signal.prepare (sr, maxHistorySeconds);

    silence.assign ((size_t) maxBlock, 0.0f);

    // Ein Pfadpaar (linkes/rechtes Ohr) je möglichem Weg: Direktschall, je eine
    // Reflexion an jeder Fläche, und je zwei Reflexionen an zwei
    // VERSCHIEDENEN Flächen. Zweimal dieselbe unendliche Ebene hintereinander
    // gibt es nicht - die Verkettung wäre die Identität und damit der
    // Direktschall.
    //
    // Alle liegen dauerhaft bereit, damit das Ein- und Ausschalten einer
    // Reflexion im Audiothread nichts allokiert; ausgeschaltet werden sie
    // übersprungen (renderInto) und kosten dann auch keine Löserzeit.
    recipes.clear();

    auto addPair = [this] (int first, int second)
    {
        recipes.push_back ({ 0, first, second });
        recipes.push_back ({ 1, first, second });
    };

    addPair (-1, -1);   // Direktschall

    for (int s = 1; s < surfaceCount; ++s)
        addPair (s, -1);

    for (int a = 1; a < surfaceCount; ++a)
        for (int b = 1; b < surfaceCount; ++b)
            if (a != b)
                addPair (a, b);

    // Klone: je ein Pfadpaar, nur Direktschall. Sie liegen wie alles andere
    // dauerhaft bereit und werden übersprungen, solange sie nicht eingeschaltet
    // sind.
    for (int k = 0; k < maxRealClones; ++k)
    {
        recipes.push_back ({ 0, -1, -1, k });
        recipes.push_back ({ 1, -1, -1, k });
    }

    // Propellerpaar: wie die Klone je ein Pfadpaar mit reinem Direktschall,
    // dauerhaft bereitliegend und uebersprungen, solange es aus ist.
    for (int prop = 0; prop < propellerCount; ++prop)
    {
        recipes.push_back ({ 0, -1, -1, -1, prop });
        recipes.push_back ({ 1, -1, -1, -1, prop });
    }

    // Der Direktschall ist die Fläche ohne Fläche: keine Spiegelung, keine
    // Dämpfung, nie abschaltbar.
    surfaces[0] = Surface{};
    surfaces[0].enabled = true;
    surfaces[0].damping = 0.0;

    surfaces[1].transform = groundMirrorTransform();
    surfaces[1].dampFcHz  = groundDampFcHz;

    for (int w = 0; w < maxWalls; ++w)
    {
        surfaces[(size_t) (2 + w)].dampFcHz = wallDampFcHz;
        setWall (w, false, wallGeometry[w].anchor,
                 wallGeometry[w].azimuthRad, wallGeometry[w].tiltRad,
                 surfaces[(size_t) (2 + w)].damping, 1.0);
    }

    geometry.prepare (sr, maxBlock, 2);

    geometry.active().prepare  (sr, maxBlock, recipes.size(), trajectoryRateHz, maxHistorySeconds);
    geometry.pending().prepare (sr, maxBlock, recipes.size(), trajectoryRateHz, maxHistorySeconds);

    setBoomLimitDb (boomLimitDb);
    setAirAbsorptionAmount (airAbsorbAmount);
    setDistanceCurve (distanceCurve);
    setNWave (nWaveOn, nWaveSizeM, nWaveGain);
    setReverseGain (reverseGain);
    setShockDuck (shockDuckAmount, shockDuckRange);
    setShadowTailSeconds (shadowTailSeconds);
    setJumpBoom (jumpBoom);
    setJumpSize (jumpSizeM);

    reset();
}

void DopplerEngine::reset()
{
    sampleClock   = 0;
    nextTrajIndex = 1;   // t = 0 belegt bereits der Reset-Stützpunkt

    // prepare() auf unveränderter Größe füllt den vorhandenen Speicher nur neu
    // und allokiert deshalb nicht - ein eigenes clear() hat SourceSignalBuffer
    // nicht, und der Puffer muss beim Zurücksetzen wirklich leer sein.
    if (sr > 0.0)
        signal.prepare (sr, maxHistorySeconds);

    geometry.reset();
    geometry.active().reset  (sourceTarget, 0.0, listener);
    geometry.pending().reset (sourceTarget, 0.0, listener);

    queuedJumpPos    = sourceTarget;
    queuedJumpVel    = Vec3{};
    fieldChangeArmed = false;

    // Nach einem Zeitsprung wäre die alte Marke in der Zukunft und der
    // Snapshot bliebe stehen, bis die Uhr sie wieder eingeholt hat.
    lastSnapshotTime = -1.0;
}

int DopplerEngine::fadeSamplesFor (FadeReason reason, double positionDeltaMetres) const
{
    FadeContext ctx;
    ctx.reason              = reason;
    ctx.sampleRate          = sr > 0.0 ? sr : 48000.0;
    ctx.positionDeltaMetres = positionDeltaMetres;

    return computeFadeSamples (ctx);
}

void DopplerEngine::configurePendingSet (Vec3 newPos, Vec3 preVelocity)
{
    configureSet (geometry.pending(), newPos, preVelocity);
}

void DopplerEngine::configureSet (PathSet& s, Vec3 newPos, Vec3 preVelocity)
{
    // Auf dem letzten bereits geschriebenen Rasterpunkt aufsetzen, nicht auf
    // der (rasterfremden) Blockzeit - sonst läge der nächste push() vor
    // newestTime() und die Zeitachse der Trajektorie wäre nicht mehr monoton.
    const double t = (double) (nextTrajIndex - 1) / trajectoryRateHz;

    // Komplette Vorgeschichte an der neuen Stelle: der neue Satz klingt sofort,
    // statt erst nach der Laufzeit einzusetzen (Plan 2.6/3.2). Ruhend, wenn
    // keine Anfangsgeschwindigkeit angegeben ist, sonst als gleichförmige
    // Bewegung auf derselben Geraden.
    const double preSpeed = preVelocity.length();

    if (preSpeed > 0.0)
    {
        // Wie weit die Gerade zurückreichen darf: der Puffer deckt eine
        // endliche Laufzeit ab, und der Schall vom ältesten Punkt muss den
        // Hörer noch erreichen können. Sonst fände der Löser dort gar keine
        // Wurzel und der neue Satz begänne stumm.
        //
        // Reichweite konservativ mit der langsamsten je auftretenden
        // Schallgeschwindigkeit (0 °C) und 10 % Sicherheitsabstand gerechnet -
        // dieselbe Zahl, nach der auch die Pufferlänge bemessen ist.
        const double reach   = 0.9 * 331.3 * maxHistorySeconds;
        const double startR  = (listener.head - newPos).length();
        const double allowed = std::max (0.0, (reach - startR) / preSpeed);

        s.trajectory.fillLinear (newPos, preVelocity, t,
                                 std::min (maxHistorySeconds, allowed));
    }
    else
    {
        s.trajectory.jumpTo (newPos, t);
    }

    s.lastPos = newPos;

    // Die neue Ohrgeometrie gehört zum neuen Satz; der alte behält seine und
    // friert sie ein (siehe pushTrajectory/process).
    s.listener     = listener;
    s.prevListener = listener;

    // Zweigidentitäten und Filterzustände hängen an der alten Geometrie und
    // dürfen nicht nachgeführt werden (siehe RetardedTimeSolver.h).
    // Abbildung und Reflexionsdämpfung stehen in den Flächen und werden vor
    // jedem Block übernommen (renderInto) - hier nur, was am Pfad selbst hängt.
    for (auto& p : s.paths)
    {
        p.reset();
        p.setBoomLimitDb (boomLimitDb);
        p.setAirAbsorptionAmount (airAbsorbAmount);
        p.setDistanceCurve (distanceCurve);
        p.setNWave (nWaveOn, nWaveSizeM, nWaveGain);
        p.setReverseGain (reverseGain);
        p.setShockDuck (shockDuckAmount, shockDuckRange);
        p.setShadowTailSeconds (shadowTailSeconds);
        p.setJumpBoom (jumpBoom);
        p.setJumpSize (jumpSizeM);
    }
}

void DopplerEngine::startGeometrySwitch (Vec3 newPos, Vec3 preVelocity, int fadeSamples)
{
    sourceTarget = newPos;

    if (geometry.isFading() || geometry.queuedSwitchDue())
    {
        // Läuft schon ein Fade: nur anmelden. Der pending()-Satz ist gerade
        // hörbar und darf nicht umkonfiguriert werden - deshalb wartet der
        // neue Zielzustand hier, bis der laufende Fade durch ist (Plan 3.7,
        // Warteschlange der Länge eins, der letzte gewinnt).
        queuedJumpPos = newPos;
        queuedJumpVel = preVelocity;
        geometry.beginSwitch (fadeSamples);
        return;
    }

    configurePendingSet (newPos, preVelocity);
    geometry.beginSwitch (fadeSamples);
}

void DopplerEngine::cutTo (Vec3 posMetres, Vec3 preVelocity)
{
    // Schnitt statt Ueberblendung (@dpa 20260824: "Ende erreicht, leise,
    // umbau, laut, start"). Der Unterschied zu jumpSourceTo() ist nicht die
    // Fadedauer, sondern dass hier gar nichts nebeneinander laeuft:
    //
    //   jumpSourceTo() - zwei komplette Geometriesaetze rechnen gleichzeitig
    //                    und werden gegeneinander geblendet. Das kostet
    //                    doppelte Loeserzeit und ist als Bewegung hoerbar,
    //                    weil der alte Satz waehrenddessen weiterfliegt.
    //   cutTo()        - beide Saetze werden an der neuen Stelle neu
    //                    aufgesetzt, mit ruhender Vorgeschichte. Nichts wird
    //                    geblendet, nichts laeuft doppelt.
    //
    // Hoerbar waere der Schnitt als Knacken - deshalb liegt er im Aufrufer
    // zwischen Aus- und Einblende des Ausgangs (siehe DopplerfeldProcessor,
    // Schnitt-Zustandsmaschine). Die Engine selbst blendet hier bewusst
    // nichts, sie fuehrt den Schnitt nur aus.
    //
    // Der Signalpuffer bleibt stehen: die Vorgeschichte der Bahn wird an der
    // neuen Stelle vollstaendig gefuellt (configureSet), der neue Ort klingt
    // deshalb sofort und nicht erst nach der Laufzeit. Ein Loeschen des
    // Puffers waere Stille ueber die ganze Laufstrecke - bei 1400 m gut vier
    // Sekunden.
    sourceTarget = posMetres;

    // Eine laufende oder angemeldete Ueberblendung gehoert zum alten Ort.
    geometry.reset();

    configureSet (geometry.active(),  posMetres, preVelocity);
    configureSet (geometry.pending(), posMetres, preVelocity);

    queuedJumpPos = posMetres;
    queuedJumpVel = preVelocity;
}

void DopplerEngine::markSourceJump (double speedStepMps)
{
    for (auto* s : { &geometry.active(), &geometry.pending() })
        for (auto& p : s->paths)
            p.setJumpMarker (currentTime(), speedStepMps);
}

void DopplerEngine::jumpSourceTo (Vec3 posMetres)
{
    // Sprungweite ab der zuletzt tatsächlich geschriebenen Position des
    // jüngsten Satzes, nicht ab sourceTarget: bei laufender Bewegung ist das
    // Ziel dem Klang voraus, und gefadet wird über das, was man hört.
    const PathSet& newest = geometry.isFading() ? geometry.pending() : geometry.active();
    const double   delta  = (posMetres - newest.lastPos).length();

    startGeometrySwitch (posMetres, Vec3{}, fadeSamplesFor (FadeReason::SourcePosition, delta));
}

void DopplerEngine::startLinearMotion (Vec3 posMetres, Vec3 velocity)
{
    const PathSet& newest = geometry.isFading() ? geometry.pending() : geometry.active();
    const double   delta  = (posMetres - newest.lastPos).length();

    startGeometrySwitch (posMetres, velocity,
                         fadeSamplesFor (FadeReason::SourcePosition, delta));
}

void DopplerEngine::setFieldMetres (double metres)
{
    // Schwelle statt Gleichheitsvergleich: dieselbe Reglerstellung darf keinen
    // Fade auslösen, und ein float-Parameter trifft seinen alten Wert beim
    // Hin- und Herrechnen nicht bitgenau.
    if (std::abs (metres - fieldMetres) < 1.0e-9)
        return;

    fieldMetres = metres;

    // Vor prepare() gibt es nichts zu überblenden (und keine Sample-Rate für
    // die Fadedauer) - dann ist das nur ein Wertespeicher.
    if (sr > 0.0)
        fieldChangeArmed = true;
}

void DopplerEngine::applyArmedFieldChange()
{
    if (! fieldChangeArmed)
        return;

    fieldChangeArmed = false;

    // Bewusste Abweichung vom Wortlaut aus Plan 3.7 ("DualPathCrossfader
    // <DopplerEngine> über die komplette Engine"):
    //
    // Positionen werden normiert gespeichert und erst beim Lesen in Meter
    // umgerechnet (Plan 2.1). Eine Feldgrößenänderung ist deshalb - INNERHALB
    // der Engine - exakt und ausschließlich ein Sprung der Geometrie in
    // Metern: Quellposition und Hörerposition wechseln unstetig, sonst
    // nichts. Der Signalpuffer, das Medium und die Puffergrößen (nach
    // maxFieldMetres bemessen, siehe prepare) bleiben unberührt.
    //
    // Genau diesen Sprung blendet der PathSet-Doppelpfad bereits vollständig
    // ab: eigene Trajektorie, eigener Löser mit eigenen Zweigidentitäten,
    // eigene Luftdämpfungs- und Envelope-Zustände, eigene Ohrgeometrie. Eine
    // zweite komplette DopplerEngine würde zusätzlich den Signalpuffer
    // verdoppeln (bei n_max = 10000 zweistellige MB) und einen zweiten
    // Schreiber auf ein Signal setzen, das identisch ist - ohne dass ein
    // einziges Sample anders klänge. Der Aufrufer muss dafür lediglich
    // Feldgröße UND die daraus folgenden Positionen im selben Block setzen,
    // was er ohnehin tut.
    //
    // Der Fade startet erst hier, am Blockanfang, und nicht schon in
    // setFieldMetres(): so ist die Reihenfolge der Setter egal - die neuen
    // Positionen stehen dann in jedem Fall schon.
    startGeometrySwitch (sourceTarget, Vec3{}, fadeSamplesFor (FadeReason::FieldSize, 0.0));
}

void DopplerEngine::setBoomLimitDb (double dB)
{
    boomLimitDb = dB;

    for (auto* s : { &geometry.active(), &geometry.pending() })
        for (auto& p : s->paths)
            p.setBoomLimitDb (dB);
}

void DopplerEngine::setAirAbsorptionAmount (double amount01)
{
    airAbsorbAmount = amount01;

    for (auto* s : { &geometry.active(), &geometry.pending() })
        for (auto& p : s->paths)
            p.setAirAbsorptionAmount (amount01);
}

void DopplerEngine::setDistanceCurve (double curve)
{
    distanceCurve = curve;

    for (auto* s : { &geometry.active(), &geometry.pending() })
        for (auto& p : s->paths)
            p.setDistanceCurve (curve);
}

void DopplerEngine::setGroundReflectionEnabled (bool shouldBeEnabled)
{
    surfaces[1].enabled = shouldBeEnabled;
}

void DopplerEngine::setGroundGain (double gainLinear)
{
    // Gleicher Weg wie bei den Waenden: der Pegel sitzt in der Abbildung, damit
    // eine Mehrfachreflexion ihn automatisch mitnimmt (siehe setWall).
    surfaces[1].transform.gain = (float) gainLinear;
}

void DopplerEngine::setGroundDampingAmount (double amount01)
{
    // Nur die Spiegelpfade: der Direktschall streift keine Fläche, seine
    // Höhen verliert er ausschließlich an die Luft. Durchgereicht wird der
    // Wert vor jedem Block in renderInto().
    surfaces[1].damping = amount01;
}

void DopplerEngine::setWall (int index, bool enabled, Vec3 anchorMetres,
                             double azimuthRad, double tiltRad, double damping01,
                             double gainLinear)
{
    if (index < 0 || index >= maxWalls)
        return;

    wallGeometry[index] = { anchorMetres, azimuthRad, tiltRad };

    Surface& s = surfaces[(size_t) (2 + index)];

    s.enabled   = enabled;
    s.damping   = damping01;
    s.transform = wallMirrorTransform (anchorMetres, azimuthRad, tiltRad);
    s.normal    = wallNormal (azimuthRad, tiltRad);

    // Gain sitzt in der Abbildung selbst, nicht in einem eigenen Surface-
    // Feld: composeTransforms() multipliziert outer.gain * inner.gain bei
    // Mehrfachreflexion automatisch mit (siehe recipeTransform()), ohne dass
    // die Verkettung dafuer extra angefasst werden muesste.
    s.transform.gain = (float) gainLinear;
}

void DopplerEngine::setNWave (bool shouldBeEnabled, double sizeMetres, double gainLinear)
{
    nWaveOn    = shouldBeEnabled;
    nWaveSizeM = sizeMetres;
    nWaveGain  = gainLinear;

    for (auto* s : { &geometry.active(), &geometry.pending() })
        for (auto& p : s->paths)
            p.setNWave (shouldBeEnabled, sizeMetres, gainLinear);
}

void DopplerEngine::setReverseGain (double gainLinear)
{
    reverseGain = gainLinear;

    for (auto* s : { &geometry.active(), &geometry.pending() })
        for (auto& p : s->paths)
            p.setReverseGain (gainLinear);
}

void DopplerEngine::setShockDuck (double amount01, double rangeMetres)
{
    shockDuckAmount = amount01;
    shockDuckRange  = rangeMetres;

    for (auto* s : { &geometry.active(), &geometry.pending() })
        for (auto& p : s->paths)
            p.setShockDuck (amount01, rangeMetres);
}


void DopplerEngine::setJumpBoom (double amount01)
{
    jumpBoom = amount01;

    for (auto* s : { &geometry.active(), &geometry.pending() })
        for (auto& p : s->paths)
            p.setJumpBoom (amount01);
}

void DopplerEngine::setJumpSize (double metres)
{
    jumpSizeM = metres;

    for (auto* s : { &geometry.active(), &geometry.pending() })
        for (auto& p : s->paths)
            p.setJumpSize (metres);
}

void DopplerEngine::setShadowTailSeconds (double seconds)
{
    shadowTailSeconds = seconds;

    for (auto* s : { &geometry.active(), &geometry.pending() })
        for (auto& p : s->paths)
            p.setShadowTailSeconds (seconds);
}

Vec3 DopplerEngine::cloneOffset (int index, double spreadMetres)
{
    // Drei zueinander irrationale Winkelschritte (goldener Schnitt und
    // Verwandte) verteilen die Klone gleichmäßig, ohne sich je zu wiederholen
    // und ohne dass ein Zufallsgenerator im Spiel wäre. Der Betrag wächst mit
    // der Wurzel des Index, damit die Punkte flächig streuen statt sich auf
    // einem Ring zu drängen.
    const double u = (double) (index + 1);

    const double a1 = u * 2.39996322972865332;    // goldener Winkel
    const double a2 = u * 1.89654;
    const double r  = spreadMetres * std::sqrt (u / (double) maxRealClones);

    return { r * std::cos (a1),
             r * std::sin (a1),
             r * 0.35 * std::sin (a2) };
}

void DopplerEngine::setRealClones (int count, double spreadMetres, double gainLinear)
{
    realClones     = std::min (maxRealClones, std::max (0, count));
    cloneSpread    = std::max (0.0, spreadMetres);
    // Kein oberer Deckel mehr: gainLinear kommt aus einem dB-Regler
    // (-36..+36dB), der ausdruecklich ueber 0dB (=1.0) hinaus darf.
    cloneRealLevel = std::max (0.0, gainLinear);
}

void DopplerEngine::setPropellers (bool enabled, double gainLinear)
{
    propellersOn  = enabled;
    propellerGain = std::max (0.0, gainLinear);
}

void DopplerEngine::setSecondOrderEnabled (bool shouldBeEnabled)
{
    secondOrderOn = shouldBeEnabled;
}

void DopplerEngine::setBounceGain (double gain01)
{
    // Streng unter 1 geklemmt: das ist der Faktor je zusätzlicher Generation,
    // und genau er garantiert, dass eine weitere Generation leiser ist als die
    // vorige - unabhängig von der Geometrie. Bei genau 1 wäre eine zweifach
    // reflektierte Welle nur durch den längeren Weg gedämpft, und der kann bei
    // zwei nah beieinander stehenden Wänden fast null sein.
    bounceGain = std::min (0.99, std::max (0.0, gain01));
}

void DopplerEngine::setBounceGainBoost (double gainLinear)
{
    // Keine Klemmung: das hier ist ausdruecklich der Regler, der ueber 0dB
    // hinaus darf - die Generationsgarantie liegt allein bei bounceGain
    // (s.o.). Sicherheitsnetz gegen zu hohe Pegel ist der Master-Limiter,
    // nicht dieser Setter.
    bounceGainBoost = gainLinear;
}

void DopplerEngine::disableAllReflections()
{
    // Index 0 ist der Direktschall und bleibt.
    for (int i = 1; i < surfaceCount; ++i)
        surfaces[i].enabled = false;

    secondOrderOn = false;
    realClones    = 0;
}

bool DopplerEngine::recipeEnabled (const PathRecipe& r) const
{
    if (r.clone >= 0)
        return r.clone < realClones;

    if (r.prop >= 0)
        return propellersOn;

    switch (r.order())
    {
        case 0:  return true;   // Direktschall, immer
        case 1:  return surfaces[(size_t) r.first].enabled;
        default: return secondOrderOn
                        && surfaces[(size_t) r.first].enabled
                        && surfaces[(size_t) r.second].enabled;
    }
}

PathTransform DopplerEngine::recipeTransform (const PathRecipe& r) const
{
    if (r.prop >= 0)
    {
        // Wie beim Klon eine reine Verschiebung: Quelle um s verschieben ist
        // dasselbe wie Empfaenger um -s verschieben. Der Versatz selbst kommt
        // fertig aus dem Processor, weil dort die Flugrichtung bekannt ist
        // (siehe setPropellerOffset).
        PathTransform t;
        t.offset = -propellerOffset[(size_t) r.prop];
        t.gain   = (float) propellerGain;
        return t;
    }

    if (r.clone >= 0)
    {
        // Quelle um s verschieben == Empfänger um -s verschieben. Die
        // Abbildung ist deshalb eine reine Verschiebung, die lineare Matrix
        // bleibt die Einheitsmatrix.
        // Fester Versatz plus eigener Wackler dieses Klons. Ohne den zweiten
        // Teil stehen alle Klone starr zueinander, und was als Schwarm gedacht
        // ist, klingt wie ein einziger breiter Ton.
        PathTransform t;
        t.offset = -(cloneOffset (r.clone, cloneSpread)
                     + cloneJitterOffset[(size_t) r.clone]);

        // Eigener Pegel, wie bei den Waenden in der Abbildung: ohne ihn kommt
        // jeder Klon mit vollem Pegel dazu, und schon acht Stueck druecken den
        // Ausgang an den Limiter - dann klingt der Schwarm nicht breiter,
        // sondern zusammengefahren.
        t.gain = (float) cloneRealLevel;
        return t;
    }

    if (r.order() == 0)
        return PathTransform{};

    if (r.order() == 1)
    {
        PathTransform t = surfaces[(size_t) r.first].transform;

        // Seitenerkennung nur bei Waenden (Index >= 2), nicht beim Boden -
        // @dpa wollte ausdruecklich die Waende, und beim Boden stehen Quelle/
        // Hoerer ohnehin so gut wie immer auf derselben Seite (oberhalb).
        if (r.first >= 2)
            t.gain *= (float) wallSideGain (r.first - 2);

        return t;
    }

    // Der Schall trifft erst first, dann second; der Empfänger wird deshalb
    // erst an second und dann an first gespiegelt (siehe PathRecipe).
    // t.gain traegt an dieser Stelle bereits wall1Gain * wall2Gain aus der
    // Verkettung (composeTransforms multipliziert outer.gain * inner.gain) -
    // bounceGain (Generationsfaktor <1) und bounceGainBoost (freier Boost)
    // kommen hier zusaetzlich obendrauf.
    PathTransform t = composeTransforms (surfaces[(size_t) r.first].transform,
                                         surfaces[(size_t) r.second].transform);

    t.gain *= (float) (bounceGain * bounceGainBoost);

    return t;
}

double DopplerEngine::wallSideGain (int wallIndex) const
{
    if (wallIndex < 0 || wallIndex >= maxWalls)
        return 1.0;

    const Surface& s      = surfaces[(size_t) (2 + wallIndex)];
    const Vec3&    anchor = wallGeometry[(size_t) wallIndex].anchor;

    const double dSrc = s.normal.dot (sourceTarget  - anchor);
    const double dLis = s.normal.dot (listener.head - anchor);

    // Vorzeichen von dSrc und dLis gleich => Quelle und Hoerer auf derselben
    // Seite der Wandebene, genau die Bedingung, unter der eine
    // Spiegelquellen-Reflexion ueberhaupt entsteht (die reale Wand wirft den
    // Schall in denselben Raum zurueck, aus dem er kam). dSrc * dLis ist
    // dafuer ein durchgehend stetiges Mass: positiv bei gleicher Seite,
    // negativ bei verschiedener, und durchlaeuft exakt 0, wenn Quelle ODER
    // Hoerer die Ebene passieren - deshalb weich ueber sideFadeMetres statt
    // hart geschaltet, sonst kliekt es beim Ueberqueren.
    constexpr double sideFadeMetres = 1.5;

    const double t = 0.5 + 0.5 * (dSrc * dLis) / (sideFadeMetres * sideFadeMetres);

    return std::min (1.0, std::max (0.0, t));
}

double DopplerEngine::recipeDamping (const PathRecipe& r) const
{
    if (r.clone >= 0)
        return 0.0;   // Klone laufen nur über den Direktschall

    if (r.order() == 0)
        return 0.0;

    const double a = surfaces[(size_t) r.first].damping;

    if (r.order() == 1)
        return a;

    // Zwei Flächen hintereinander: der Pfad hat nur EINE Dämpfungsstufe, die
    // beiden werden deshalb zusammengefasst. 1 - (1-a)(1-b) ist die übliche
    // Reihenschaltung zweier Absorptionsgrade - bei b = 0 fällt sie exakt auf
    // a zurück, und sie kann nie über 1 hinauslaufen. Eine echte Kaskade aus
    // zwei getrennten Filtern wäre genauer, kostet aber einen zweiten
    // Filterzustand je Zweig für einen Unterschied, den man an einer
    // Modellkonstante ohnehin nicht festmachen kann.
    const double b = surfaces[(size_t) r.second].damping;

    return 1.0 - (1.0 - a) * (1.0 - b);
}

double DopplerEngine::recipeDampFcHz (const DopplerEngine::PathRecipe& r) const
{
    if (r.order() == 0)
        return groundDampFcHz;

    const double a = surfaces[(size_t) r.first].dampFcHz;

    if (r.order() == 1)
        return a;

    // Die dunklere der beiden Flächen gibt den Ton an.
    return std::min (a, surfaces[(size_t) r.second].dampFcHz);
}

std::uint64_t DopplerEngine::solverEvaluations() const
{
    std::uint64_t total = 0;

    for (const auto* s : { &geometry.active(), &geometry.pending() })
        for (const auto& p : s->paths)
            total += p.solverEvaluations();

    return total;
}

void DopplerEngine::beginChunk()
{
    // Ein angemeldeter Geometriewechsel wird fällig, sobald der laufende Fade
    // durch ist - und zwar bevor die nächsten Bahnpunkte geschrieben werden,
    // sonst landen sie noch im alten Satz.
    if (geometry.queuedSwitchDue())
    {
        configurePendingSet (queuedJumpPos, queuedJumpVel);
        geometry.startQueuedSwitch();
    }

    applyArmedFieldChange();
}

void DopplerEngine::pushSourceTick (Vec3 posMetres)
{
    sourceTarget = posMetres;

    const double t = (double) nextTrajIndex / trajectoryRateHz;

    // Solange ein Wechsel angemeldet ist, gehört die aktuelle Zielposition
    // schon dem NÄCHSTEN Satz. Der gerade einblendende darf nicht dorthin
    // wandern, sonst zieht er den Sprung als rasende Bewegung nach - man
    // hörte ihn dann doch, nur als Doppler-Schleifer statt als Klick.
    const bool fading = geometry.isFading();
    const bool follow = ! geometry.hasQueuedSwitch();

    PathSet& newest    = fading ? geometry.pending() : geometry.active();
    PathSet& fadingOut = geometry.active();

    newest.push (follow ? posMetres : newest.lastPos, t);

    // Der ausblendende Satz hält seine Position. Er repräsentiert den
    // Zustand VOR dem Sprung; würde er mitlaufen, wäre er kein
    // Vergleichsklang mehr, sondern eine zweite Bewegung.
    if (fading)
        fadingOut.push (fadingOut.lastPos, t);

    ++nextTrajIndex;
}

void DopplerEngine::fillTrajectoryUpTo (double untilTime)
{
    const double grid = 1.0 / trajectoryRateHz;

    // Ein Rasterpunkt über das Blockende hinaus, damit newestTime() den ganzen
    // Block abdeckt. Sonst friert der Löser die Quelle im letzten Bruchteil
    // einer Millisekunde ein (Randbedingung aus Plan 2.6) und M_r bekäme dort
    // eine falsche Null.
    //
    // Im Normalbetrieb hat der Processor genau bis hierher getickt und die
    // Schleife läuft null Mal. Sie greift nur, wenn jemand process() ohne die
    // Bewegungskette davor aufruft - dann steht die Quelle still, statt dass
    // der Löser ins Leere greift.
    while ((double) nextTrajIndex * grid <= untilTime + grid)
    {
        const PathSet& newest = geometry.isFading() ? geometry.pending() : geometry.active();
        pushSourceTick (newest.lastPos);
    }
}

void DopplerEngine::publishSnapshot (const MediumState& medium)
{
    const double now = currentTime();

    // Die Anzeige läuft mit ~30 Hz, ein Snapshot kostet aber gut hundert
    // Trajektorien-Auswertungen. Bei 128-Sample-Teilblöcken käme das sonst
    // mehrere hundert Mal pro Sekunde - die Obergrenze hier lässt der GUI
    // Luft und hält die Anzeigearbeit im Audiothread trotzdem klein.
    if (lastSnapshotTime >= 0.0 && now - lastSnapshotTime < snapshotIntervalSeconds)
        return;

    lastSnapshotTime = now;

    // Der jüngste Satz, nicht der aktive: während eines Fades wandert die
    // Quelle im pending()-Satz weiter, der aktive hält seine Position an.
    // Alles im Snapshot kommt aus demselben Satz, sonst zeigten Spur und
    // Pfadwerte zwei verschiedene Geometrien.
    const PathSet& set = geometry.isFading() ? geometry.pending() : geometry.active();

    FieldSnapshot& s = snapshotBuffers[snapshotWriteSlot];

    snapshotGeneration.fetch_add (1, std::memory_order_release);   // ungerade: Schreiben läuft

    s.now        = now;
    s.sourcePos  = set.lastPos;
    s.listener   = listener;
    s.speedOfSound = medium.speedOfSound();
    s.groundReflectionOn = isGroundReflectionEnabled();

    {
        Vec3 p, v;
        s.sourceSpeed = set.trajectory.sampleAt (std::min (now, set.trajectory.newestTime()), p, v)
                        ? v.length() : 0.0;
    }

    const double tNewest = std::min (now, set.trajectory.newestTime());
    const double tOldest = set.trajectory.oldestTime();

    // Spur: gleichmäßig über die letzten Sekunden abgetastet statt jeden
    // Rasterpunkt zu kopieren (1 kHz Raster wären 3000 Punkte für dieselbe
    // Linie). Am Anfang, wenn noch keine trailSeconds Historie existiert,
    // klemmt der Startpunkt am ältesten bekannten Eintrag.
    constexpr double trailSeconds = 3.0;

    const double t0 = std::max (tOldest, tNewest - trailSeconds);

    s.trailCount = 0;

    if (tNewest > t0)
    {
        constexpr int wanted = FieldSnapshot::maxTrailPoints;

        for (int i = 0; i < wanted; ++i)
        {
            const double t = t0 + (tNewest - t0) * (double) i / (double) (wanted - 1);

            Vec3 p, v;

            if (set.trajectory.sampleAt (t, p, v))
                s.trail[(size_t) s.trailCount++] = p;
        }
    }

    // Wellenfronten: der Editor zeichnet Kreise mit Radius c*(now - t_k) um
    // M(t_k). Der Abstand der Emissionszeiten skaliert mit der Feldgröße -
    // eine feste Schrittweite läge bei n = 1 m sofort außerhalb des Bildes
    // und bei n = 10000 m alle Fronten aufeinander. Bezug ist die Zeit, die
    // der Schall für die Feldbreite braucht.
    const double c = medium.speedOfSound();

    const double spacing = std::min (0.5,
                                     std::max (0.02,
                                               fieldMetres / (c * (double) FieldSnapshot::maxWavefronts)));

    s.wavefrontCount = 0;

    for (int i = 1; i <= FieldSnapshot::maxWavefronts; ++i)
    {
        const double tEmit = now - (double) i * spacing;

        if (tEmit < tOldest)
            break;   // weiter zurück reicht die Historie nicht

        Vec3 p, v;

        if (! set.trajectory.sampleAt (tEmit, p, v))
            break;

        s.wavefrontEmitTimes[(size_t) s.wavefrontCount] = tEmit;
        s.wavefrontPositions[(size_t) s.wavefrontCount] = p;
        ++s.wavefrontCount;
    }

    // Bild-Wellenfronten der Reflexionen: dieselben Emissionspunkte wie oben,
    // nur durch die jeweilige Wandspiegelung geschickt (applyPathTransform
    // ist eine allgemeine affine Punktabbildung - dieselbe, mit der sonst der
    // EMPFÄNGER gespiegelt wird, liefert auf die QUELLE angewandt genau die
    // Bildquelle, weil eine Spiegelung ihre eigene Inverse ist).
    static_assert (maxWalls == 2,
                   "wallPairWavefronts unten ist von Hand auf zwei Waende geschrieben.");

    for (int w = 0; w < maxWalls && w < FieldSnapshot::maxWalls; ++w)
    {
        auto&          wf   = s.wallWavefronts[(size_t) w];
        const Surface& surf = surfaces[(size_t) (2 + w)];

        wf.active = surf.enabled;
        wf.gain   = (float) wallSideGain (w);   // nur sichtbar, wo es auch klaenge

        if (wf.active)
            for (int i = 0; i < s.wavefrontCount; ++i)
                wf.positions[(size_t) i] = applyPathTransform (surf.transform, s.wavefrontPositions[(size_t) i]);
    }

    {
        const bool pairsActive = secondOrderOn && surfaces[2].enabled && surfaces[3].enabled;

        // Beide Wandseiten muessen stimmen, sonst kann die doppelte Reflexion
        // gar nicht entstehen - dieselbe Multiplikations-Logik wie bei zwei
        // Daempfungsstufen in Serie (siehe recipeDamping).
        const float pairGain = (float) (wallSideGain (0) * wallSideGain (1));

        const PathTransform orderAB = composeTransforms (surfaces[2].transform, surfaces[3].transform);
        const PathTransform orderBA = composeTransforms (surfaces[3].transform, surfaces[2].transform);

        const PathTransform* const pairTransforms[FieldSnapshot::maxWallPairs] { &orderAB, &orderBA };

        for (int p = 0; p < FieldSnapshot::maxWallPairs; ++p)
        {
            auto& wf = s.wallPairWavefronts[(size_t) p];
            wf.active = pairsActive;
            wf.gain   = pairGain;

            if (wf.active)
                for (int i = 0; i < s.wavefrontCount; ++i)
                    wf.positions[(size_t) i] = applyPathTransform (*pairTransforms[p], s.wavefrontPositions[(size_t) i]);
        }
    }

    s.pathCount = 0;

    for (size_t i = 0; i < set.paths.size() && i < recipes.size()
                       && s.pathCount < FieldSnapshot::maxPaths; ++i)
    {
        const PathRecipe& recipe = recipes[i];

        // Nicht gerechnete Pfade gehören nicht in die Anzeige - ihre Werte
        // stammen sonst vom letzten Einschaltzeitpunkt und stünden dort fest.
        if (! recipeEnabled (recipe))
            continue;

        // Klone einzeln aufzulisten würde die Anzeige fluten; sie stehen als
        // Zahl in s.realCloneCount.
        if (recipe.clone >= 0)
            continue;

        auto& info = s.paths[(size_t) s.pathCount];

        info.surface        = std::max (0, recipe.first);
        info.order          = recipe.order();
        info.ear            = recipe.ear;
        info.activeBranches = set.paths[i].numActiveBranches();
        info.delaySeconds   = set.paths[i].lastDelaySeconds();
        info.machRadial     = set.paths[i].lastMachRadial();

        ++s.pathCount;
    }

    s.realCloneCount = realClones;

    // Die Punkte, von denen aus gehoert wird: der Klon-Versatz sitzt in der
    // Geometrie mit umgekehrtem Vorzeichen (Empfaenger verschieben statt
    // Quelle), fuer die Anzeige wird er deshalb wieder umgedreht.
    s.clonePositionCount = std::min (realClones, FieldSnapshot::maxShownClones);

    for (int i = 0; i < s.clonePositionCount; ++i)
        s.clonePositions[(size_t) i] = set.lastPos + cloneOffset (i, cloneSpread)
                                     + cloneJitterOffset[(size_t) i];

    // Zweig-Todesmessung über ALLE gerechneten Pfade, auch die oben aus der
    // Liste gefilterten (Klone, abgeschaltete): gefragt ist, wie oft im ganzen
    // Modell ein Zweig mitten im Klang abgeschnitten wird, nicht wie sich das
    // auf die Anzeigezeilen verteilt.
    {
        std::uint64_t deaths = 0, loud = 0, evicted = 0, caustic = 0, abrupt = 0;
        std::uint64_t lost = 0, fresh = 0, dropped = 0, freshNear = 0, ordered = 0;
        std::uint64_t tight = 0, adjacent = 0, flips = 0, collapsed = 0, handed = 0;
        std::uint64_t nPairs = 0, nRising = 0, nFalling = 0;
        std::array<std::uint64_t, 8> hist {};
        double envSum = 0.0, envMax = 0.0, tauSum = 0.0, tauMax = 0.0;

        for (size_t i = 0; i < set.paths.size(); ++i)
        {
            const auto d = set.paths[i].branchDeaths();

            deaths  += d.deaths;
            loud    += d.loudDeaths;
            evicted += d.evictions;
            envSum  += d.envSum;
            envMax   = std::max (envMax, d.envMax);
            caustic += d.causticDeaths;
            tauSum  += d.tauSum;
            tauMax   = std::max (tauMax, d.tauMax);
            abrupt  += d.abruptDeaths;
            lost    += d.trackLost;
            fresh   += d.newIds;
            freshNear += d.newIdsNear;
            ordered   += d.orderMatches;
            flips     += d.countFlips;
            collapsed += d.collapsed;
            handed    += d.handovers;
            tight     += d.tightPairs;
            adjacent  += d.adjacentPairs;
            nPairs    += d.nWavePairBirths;
            nRising   += d.nWaveRising;
            nFalling  += d.nWaveFalling;

            for (int k = 0; k < 8; ++k)
                hist[(size_t) k] += d.rootHist[k];
            dropped += d.droppedRoots;
        }

        s.branchDeaths       = deaths;
        s.loudBranchDeaths   = loud;
        s.branchDeathEnvMean = deaths > 0 ? envSum / (double) deaths : 0.0;
        s.branchDeathEnvMax  = envMax;
        s.branchEvictions    = evicted;
        s.causticDeaths      = caustic;
        s.deathTauMeanMs     = caustic > 0 ? 1000.0 * tauSum / (double) caustic : 0.0;
        s.deathTauMaxMs      = 1000.0 * tauMax;
        s.abruptDeaths       = abrupt;
        s.trackLost          = lost;
        s.newIds             = fresh;
        s.newIdsNear         = freshNear;
        s.orderMatches       = ordered;
        s.rootHist           = hist;
        s.countFlips         = flips;
        s.collapsedTracks    = collapsed;
        s.handovers          = handed;
        s.tightPairs         = tight;
        s.adjacentPairs      = adjacent;
        s.droppedRoots       = dropped;
        s.nWavePairBirths    = nPairs;
        s.nWaveRising        = nRising;
        s.nWaveFalling       = nFalling;
    }

    for (int w = 0; w < maxWalls && w < FieldSnapshot::maxWalls; ++w)
    {
        auto& info = s.walls[(size_t) w];

        info.on         = surfaces[(size_t) (2 + w)].enabled;
        info.anchor     = wallGeometry[w].anchor;
        info.azimuthRad = wallGeometry[w].azimuthRad;
        info.tiltRad    = wallGeometry[w].tiltRad;
    }

    snapshotIndex.store (snapshotWriteSlot, std::memory_order_release);
    snapshotWriteSlot = 1 - snapshotWriteSlot;

    snapshotGeneration.fetch_add (1, std::memory_order_release);   // gerade: fertig
}

void DopplerEngine::fillSnapshot (FieldSnapshot& dest) const
{
    // Vier Versuche: jeder gescheiterte bedeutet, dass der Audiothread genau
    // während der Kopie veröffentlicht hat. Danach bleibt dest unverändert,
    // die Anzeige zeigt also einen Frame länger den alten Stand - besser als
    // den Message-Thread hier festzuhalten.
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const unsigned int before = snapshotGeneration.load (std::memory_order_acquire);

        if ((before & 1u) != 0u)
            continue;

        const int index = snapshotIndex.load (std::memory_order_acquire);

        dest = snapshotBuffers[index];

        if (snapshotGeneration.load (std::memory_order_acquire) == before)
            return;
    }
}

void DopplerEngine::process (juce::AudioBuffer<float>& stereoOut,
                             const float*              sourceMono,
                             const MediumState&        medium)
{
    const int numSamples = stereoOut.getNumSamples();
    const int numCh      = stereoOut.getNumChannels();

    if (numSamples <= 0 || numCh <= 0 || sr <= 0.0)
        return;

    // 0) Angemeldeter Geometriewechsel und Feldgröße zuerst: der laufende Fade
    //    ist durch, jetzt darf pending() neu konfiguriert werden. Der Processor
    //    hat das für diesen Teilblock schon erledigt (beginChunk), der Aufruf
    //    hier ist die Absicherung für jeden anderen Aufrufer.
    beginChunk();

    const double blockStart = (double) sampleClock / sr;
    const double blockEnd   = (double) (sampleClock + numSamples) / sr;

    // 1) Quellsignal in den geteilten Ringpuffer. Muss vor den Pfaden
    //    passieren, weil die auf dem vollständigen Datenbestand laufen
    //    (Plan 3.6). Genau ein Schreiber, auch während eines Fades - beide
    //    Geometriesätze lesen dasselbe Signal.
    const float* mono = sourceMono;

    if (mono == nullptr)
    {
        if ((int) silence.size() < numSamples)
            silence.assign ((size_t) numSamples, 0.0f);

        mono = silence.data();
    }

    signal.write (mono, numSamples);

    // 2) Bewegungspfad: geschrieben hat ihn der Processor Tick für Tick
    //    (pushSourceTick). Hier wird nur noch geprüft, dass er den ganzen Block
    //    abdeckt.
    fillTrajectoryUpTo (blockEnd);

    // 3) Blockkontext setzen. Der jüngste Satz übernimmt die aktuelle
    //    Ohrgeometrie, der ausblendende friert seine ein (prevListener ==
    //    listener, also Ohrgeschwindigkeit 0).
    const bool fading = geometry.isFading();
    const bool follow = ! geometry.hasQueuedSwitch();

    auto setContext = [&] (PathSet& s, bool takesNewListener)
    {
        s.prevListener = s.listener;

        if (takesNewListener)
            s.listener = listener;

        s.signal         = &signal;
        s.medium         = &medium;
        s.recipes        = &recipes;
        s.engine         = this;
        s.blockStartTime = blockStart;
        s.sr             = sr;
    };

    setContext (fading ? geometry.pending() : geometry.active(), follow);

    if (fading)
        setContext (geometry.active(), false);

    // 4) Rendern. Ohne laufenden Fade rendert nur der aktive Satz, direkt in
    //    stereoOut - der zweite kostet dann keine CPU.
    geometry.process (stereoOut);

    // Größere Blöcke als in prepare() angekündigt kann der Doppelpfad nicht
    // bedienen (die Zwischenpuffer stehen fest); der Rest muss still sein
    // statt alten Hostinhalt zu zeigen.
    if (numSamples > maxBlock)
        for (int ch = 0; ch < numCh; ++ch)
            stereoOut.clear (ch, maxBlock, numSamples - maxBlock);

    sampleClock += numSamples;

    // Erst nach dem Fortschreiben der Uhr: der Snapshot soll den Zustand am
    // Blockende zeigen, und genau bis dorthin reicht die Trajektorie.
    publishSnapshot (medium);
}
