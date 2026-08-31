#pragma once

#include "Medium.h"
#include "PathTransform.h"
#include "RetardedTimeSolver.h"
#include "SourceSignalBuffer.h"
#include "SourceTrajectory.h"
#include "Vec3.h"

#include <atomic>
#include <algorithm>
#include <cstdint>

namespace pathdetail
{

// std::atomic ist weder kopier- noch verschiebbar, PropagationPath liegt aber
// laut Plan 3.6 in einem std::vector. Dieser Wrapper verhält sich beim
// Kopieren/Verschieben wie ein gewöhnlicher Wert und macht damit die implizit
// erzeugten Konstruktoren von PropagationPath wieder benutzbar - ohne dass die
// Anzeigewerte (Audiothread schreibt, Message-Thread liest) zu nackten
// double-Membern mit formalem Datenrennen werden müssten.
template <typename T>
class DisplayValue
{
public:
    DisplayValue() = default;
    DisplayValue (const DisplayValue& o) { store (o.load()); }
    DisplayValue& operator= (const DisplayValue& o) { store (o.load()); return *this; }

    T    load() const { return v.load (std::memory_order_relaxed); }
    void store (T x)  { v.store (x, std::memory_order_relaxed); }

private:
    std::atomic<T> v { T{} };
};

} // namespace pathdetail

// Ein Ausbreitungsweg von der Quelle zu genau einem Empfangspunkt (Plan 3.4).
// Beliebig oft instanziierbar: zwei Ohren in Phase 1, dazu später Boden- und
// Wandspiegelungen als weitere Instanzen mit anderem PathTransform.
//
// Der Pfad besitzt weder Trajektorie noch Signal - beide kommen als const-Ref
// herein. Das ist die Bedingung dafür, dass sich Instanzen billig
// vervielfachen lassen (12 Leser auf einem Datensatz, Plan 2.12).
//
// Eigener Zustand pro Instanz:
//   - ein RetardedTimeSolver (dessen Zweigzustand hängt an der Geschichte
//     genau dieses Empfangspunkts, siehe RetardedTimeSolver.h)
//   - pro Zweig ein Luftdämpfungsfilter und ein Envelope. Beide gehören zum
//     Zweig, nicht zum Pfad (Plan 2.9): ein neu erscheinender Zweig fängt bei
//     Null an, statt den Zustand eines fremden Zweigs zu erben.
//
// JUCE-frei wie der Rest der Physics-Schicht, damit der physikalisch heikle
// Teil (Fokussierungsfaktor, Hermite-Ableitungen) offline prüfbar bleibt.
class PropagationPath
{
public:
    // Ein Slot pro möglichem Wurzelzweig - mehr Zweige als der Löser liefern
    // kann, gibt es nicht (K = 4 aus Plan 2.6).
    static constexpr int maxBranchSlots = RetardedTimeSolver::maxBranches;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void setTransform (const PathTransform& t) { transform = t; }
    const PathTransform& getTransform() const  { return transform; }

    // Boom-Limit als Regularisierung gegen die Divergenz bei M_r -> 1
    // (Plan 2.7): eps = 10^(-L_dB/20), Default L_dB = 30 -> eps ~ 0,0316.
    void   setBoomLimitDb (double dB);
    double boomLimitDb() const { return boomDb; }

    // Entfernungsabhängigkeit der Amplitude (@dpa-Skizze "Amp-Verlauf": drei
    // Kurven über Distanz gegen Amplitude, die mittlere neutral). Statt starr
    // A_geo = 1/R rechnet der Pfad 1/R^k mit einstellbarem k.
    //
    // curve = 0 ergibt genau k = 1, also das physikalisch richtige
    // Kugelwellen-Gesetz. Positive
    // Werte machen k größer (der Pegel fällt schneller mit der Entfernung ab),
    // negative kleiner (er fällt flacher ab und trägt weiter).
    //
    // k bleibt hart über null: bei k = 0 wäre die Amplitude von der Entfernung
    // völlig unabhängig, ein Vorbeiflug hätte dann keine Ferne mehr.
    void   setDistanceCurve (double curve);
    double distanceExponentValue() const { return distanceExponent; }

    // Anteil der Luftdämpfung, 0 = aus (Filter durchgereicht), 1 = voll
    // (Plan 3.11, Parameter airAbsorbAmount). Skaliert den One-Pole-Koeffi-
    // zienten in Richtung Bypass, damit "aus" wirklich aus ist und nicht bloß
    // der 18-kHz-Fall.
    void setAirAbsorptionAmount (double amount01);

    // fc(R) = clamp(fc0*(R_ref/R)^k, 200, 18000) - Defaults aus Plan 2.9.
    void setAirAbsorption (double fc0Hz, double refMetres, double exponent);

    // Zusätzliche Höhendämpfung für gespiegelte Pfade (Boden, später Wände).
    // Eigener Grad neben der Luftdämpfung, weil beide Verschiedenes
    // beschreiben: die Luftdämpfung modelliert die geflogene STRECKE und
    // wächst deshalb mit R, die Reflexionsdämpfung modelliert die FLÄCHE und
    // fällt einmal pro Reflexion an - unabhängig davon, wie weit der Schall
    // danach noch fliegt. Boden schluckt Höhen deutlich stärker als Luft.
    //
    // amount01 blendet wie airAbsorptionAmount gegen Bypass: 0 heißt wirklich
    // aus (ideal harte Fläche), nicht bloß "der milde Fall". fcHz ist die
    // Eckfrequenz bei voller Stärke; sie bleibt eine Modellkonstante des
    // Aufrufers, damit hier kein fester Wert im Weg steht, falls später
    // Wandmaterialien dazukommen.
    void setReflectionDamping (double amount01, double fcHz);

    // Solver-Rate (Plan 2.11). Bei erkanntem Überschall im Fenster wird
    // automatisch auf supersonic umgeschaltet, weil M_r dort schnell wechselt.
    void setSolverStride (int normalStride, int supersonicStride);

    // Der Löser braucht die Rasterweite der Trajektorie als Untergrenze für
    // seine Scan-Schrittweite (siehe RetardedTimeSolver::setMinScanStep). Der
    // Pfad kennt die Trajektorie nur als const-Ref und fragt sie nicht danach,
    // deshalb reicht der Besitzer der Puffer (DopplerEngine) sie hier durch.
    void setTrajectoryGridSeconds (double seconds);

    // Zeitlicher Mindestabstand zweier Vollscans des Lösers (Suche nach NEU
    // entstandenen Zweigen). Bekannte Zweige werden davon unberührt an jedem
    // Solver-Punkt nachgeführt - es geht ausschließlich darum, wie schnell
    // eine Kegelankunft bemerkt wird.
    //
    // Der Grund für die Trennung: im Überschall läuft der Löser auf Stride 8,
    // also alle 167 us bei 48 kHz. Ohne diese Trennung wuerde jeder dieser
    // Aufrufe das komplette Suchfenster neu abscannen, obwohl in 167 us fast
    // nie ein Zweig hinzukommt. Der Scan ist dabei der mit Abstand teuerste
    // Posten des ganzen Plugins.
    void setDiscoveryIntervalSeconds (double seconds);


    // Anti-Klick-Rampe beim Erscheinen/Verschwinden eines Zweigs (Plan 3.7,
    // letzter Absatz): 0,5 bis 2 ms. Das ist ausdrücklich kein globaler
    // Crossfade - der würde den Knall wegmitteln, den man hören will.
    void setBranchRampSeconds (double seconds);

    // Druckwellen-/N-Wellen-Schicht für den Überschallknall.
    //
    // Ausdrücklich ADDITIV und getrennt vom bestehenden Amplituden-Mechanismus:
    // die Formel A = 1/(R·sqrt((1-M_r)²+eps²)) bleibt unangetastet, die N-Welle
    // kommt oben drauf. Sie ist damit auch klar getrennt von "Boom Limit"
    // (reine Amplitudendeckelung über eps, keine Pulsform) und vom
    // Master-Softclip.
    //
    // Ausgelöst wird sie an der GEBURT eines Zweigpaars (zwei neue Zweige im
    // selben Solver-Segment, beide nahe M_r = 1 - die Kegelankunft) sowie
    // zusätzlich, wenn der M_r eines bereits bestehenden Zweigs die 1
    // durchquert (z.B. Beschleunigen durch Mach 1 bei laufendem Zweig). Siehe
    // ausführliche Begründung an den beiden Auslösestellen in process().
    //
    // sizeMetres ist die Ausdehnung des Körpers und bestimmt sowohl die
    // Pulsdauer (größer = tiefer und länger, kleiner = kürzer und knackiger)
    // als auch die Amplitude (größer = lauter, siehe triggerNWave()).
    //
    // edge01 ist die SCHÄRFE der beiden Stoßfronten (@dpa 20260827: "mir sind
    // die Überschallecken meist zu zahm, zu tiefgepasst, zu weich ... ich will
    // den echten knall"). Sie greift an der Anstiegszeit nRise, also an genau
    // der Größe, die über Peitschenschlag oder Wusch entscheidet - und zwar an
    // BEIDEN Anteilen, dem körpereigenen und dem, der aus der Entfernung
    // kommt. Sonst bliebe ein Knall aus 3 km auch am Anschlag weich, und
    // gerade der ist gemeint.
    //
    // 0,5 lässt die Fronten so, wie sie ohne Regler wären; nach oben werden
    // sie steiler, nach unten weicher, je Hälfte um den Faktor
    // 2^nWaveEdgeOctaves. Kein Deckel am oberen Ende außer dem Sample-Raster
    // selbst: eine senkrechte Kante ist genau das, was ein Knall ist, und ob
    // sie aliast, entscheidet der Hörer am Regler.
    // pressure ist die Staerke der Auslenkung ZWISCHEN den beiden Fronten -
    // der Druckwelle, auf der der uebrige Sound reitet. 0 laesst nur die
    // Fronten stehen, 1 ist die vollstaendige N-Welle, darueber betont.
    void setNWave (bool shouldBeEnabled, double sizeMetres, double gainLinear,
                   double edge01, double pressure);


    // ---- Rollen nach dem Knall -------------------------------------------
    //
    // Was nach einer Stossfront kommt, ist kein zweites Bild der Quelle,
    // sondern der Knall selbst: Turbulenz faltet die Wellenfront, Boden und
    // Gelaende werfen zurueck, und jeder dieser Umwege ist laenger als der
    // direkte Weg. Derselbe Knall trifft dadurch vielfach ein, jedes Mal
    // spaeter, leiser und dunkler - und genau das ist das anhaltende Rollen,
    // das ein echter Ueberflug hinter sich herzieht.
    //
    // Die zusaetzlichen Hoerwege der Laufzeitgleichung leisten das NICHT. Sie
    // sind ein Ergebnis linearer Strahlentheorie an einer Stelle, an der die
    // nicht gilt, und experimentell nicht nachgewiesen (siehe
    // Params::extraPathGainDb). Sie bleiben deshalb fest unterdrueckt.
    //
    // Es setzt NACH der Stossfront ein, nicht mit ihr (@dpa 20260831: "Der
    // N noise nimmst Du die Kraft wenn Du sie mit Rauschen vollpackst!
    // deswegen war mein Auftrag auch 'nach der N-Wave!'"). Solange die Welle
    // laeuft, gehoert der Platz ihr allein.
    //
    //   gainLinear   Pegel gegenueber der Stossfront, die ihn ausloest
    //   seconds      Abklingzeit; 0 schaltet das Rollen ab
    //   edgeLoHz     Kantenrate am Anfang - einzeln hoerbare Rueckwuerfe
    //   edgeHiHz     Kantenrate am Ende - so dicht, dass es Rauschen ist
    //   toneHz       Tiefpass ueber den Kanten; tief = rot-braun
    void setRumble (double gainLinear, double seconds,
                    double edgeLoHz, double edgeHiHz, double toneHz);

    // Absenkung des uebrigen Schalls, waehrend eine Stossfront ueber diesen
    // Weg laeuft (@dpa 20260821: "waehrend der N-Wave darf kein zusaetzlicher
    // Schall hinzukommen - hoechstens ein 'luftholen-geraeusch'! aber keine
    // Noise vom Motor. Das hat mit der Stossfront zu tun.").
    //
    // amount01 ist die Tiefe: 0 = aus, 1 = waehrend des Pulses ganz stumm.
    // Gilt fuer den ganzen Pfad, nicht nur fuer den Zweig, der den Puls
    // traegt - der Motorton liefe sonst ueber den Nachbarzweig weiter.
    //
    // Gemeint ist die GANZE N-Welle, nicht nur ihre Fronten: zwischen Bug- und
    // Heckstoss darf ebenfalls nichts durchkommen (@dpa 20260823: "es ist
    // immer was zu hoeren zwischen den zwei knallen.. das soll weg"). Danach
    // kommt der Ton ueber shockDuckRelease zurueck, kurz genug, dass es kein
    // Nachklappen gibt, und lang genug, dass es nicht knackt.
    //
    // rangeMetres ist die Entfernung, BIS ZU DER die Absenkung voll wirkt
    // (@dpa 20260824: "wir muessen also bestimmen ab welcher entfernung die
    // N-Wave noch 'echt' ist"). Nah an der Quelle ist die Stossfront eine
    // echte Diskontinuitaet und nimmt alles andere mit; weiter weg ist sie
    // laengst zerfallen, und was ankommt, ist Grollen SAMT allem drumherum.
    // Innerhalb von range steht der Faktor auf 1, dahinter faellt er mit
    // range / R, bei doppelter Entfernung also auf die Haelfte. 0 heisst
    // "gilt ueberall gleich".
    //
    // Ohne das Plateau liefe der Abfall schon ab dem ersten Meter, und volle
    // Stille zwischen den Stossfronten waere ueberhaupt nicht erreichbar:
    // gemessen ("Kreis, Druckwelle 0") stand der Motorton bei Reichweite
    // 1345 m und rund 245 m Abstand noch bei 49 % seines sonstigen Pegels,
    // mit Plateau bei 1 %.
    void setShockDuck (double amount01, double rangeMetres);


    // Was ein BEWEGUNGSSPRUNG hoerbar macht (@dpa 20260823: "der Vorbeiflug
    // 'Knall-Start' muesste ja mindestens subsonic zu hoeren sein ... Bisher
    // ist noch nicht zu hoeren!").
    //
    // Ein Geschwindigkeitssprung der Quelle - Knall-Start, ein Sprung in der
    // abgespielten Bahn - springt beim Hoerer in Amplitude und Tonhoehe,
    // sobald die Kante ankommt. Das Modell hat dafuer keine eigene
    // Schicht: die N-Welle haengt allein an M_r = 1 und bleibt unterschallig
    // stumm, und der Sprung selbst wird ueber die Laenge eines Solver-Segments
    // interpoliert, wird also zur weichen Rampe statt zur Kante.
    //
    // Der Weg dorthin ist die Druckwelle, ueber ihren eigenen Regler:
    //
    //   jumpBoom - eine Druckwelle darauf. Ein Geschwindigkeitssprung ist
    //              formal unendliche Beschleunigung, und die strahlt
    //              physikalisch eine Druckwelle ab. Nutzt dieselbe N-Wellen-
    //              Schicht wie der Ueberschallknall, mit einer Amplitude, die
    //              mit der Sprunghoehe waechst.
    // Sperrzeit nach einem Knall (@dpa 20260830). Solange sie laeuft, loest
    // hier kein zweiter aus.
    //
    // Grund: der Wackler schiebt die Quelle ueber die Schallmauer und gleich
    // wieder zurueck - gemessen im Peitschentest liegen die kuerzesten
    // Abstaende bei 17, 34 und 51 ms. Beide Fronten sind echt (Ein- und
    // Austritt), zusammen klingen sie aber wie ein Stolpern statt wie ein
    // Hieb. Die Sperre laesst den ersten stehen und wirft die Nachzuegler weg.
    //
    // Der Zustand liegt NICHT im einzelnen Hoerweg, sondern in einer Sperre,
    // die sich mehrere teilen. Ein Weg allein bringt es nicht: die zwei
    // Fronten eines Wackel-Durchgangs kommen ueber verschiedene Wege herein
    // (gemessen: vier verschiedene Pfadobjekte innerhalb einer Millisekunde),
    // und jeder haette seine eigene Sperre. Wer sie dagegen ganz global macht,
    // wirft das zweite Ohr weg - der Knall trifft es eine halbe Millisekunde
    // spaeter, und das ist die Ortung. Deshalb eine Sperre JE OHR (siehe
    // DopplerEngine::boomGates).
    struct BoomGate
    {
        double holdSeconds = 0.0;
        double lastTime    = -1.0e18;
    };

    void setBoomGate (BoomGate* gate) { boomGate = gate; }

    void setJumpBoom (double amount01);

    // Zeitpunkt (in Quellzeit), an dem die Bahn umgeschrieben wurde, und die
    // Hoehe des Geschwindigkeitssprungs dort in m/s. Gesetzt von der Engine,
    // die als Einzige weiss, wann sie das tut. Ein Hoerweg merkt die Kante,
    // sobald seine Emissionszeit ueber die Marke laeuft - das ist je Zweig ein
    // anderer Moment und genau richtig so.
    void setJumpMarker (double emissionTime, double speedStepMps);

    // Laenge des Startknalls in Metern (Params::jumpBoomSize). Kurz heisst
    // knackig, lang heisst wummernd - und anders als bei nWaveSize steckt
    // dahinter keine Koerpergroesse, sondern nur die Dauer der Kante.
    void setJumpSize (double metres);

    // Phase 2 (Plan 2.7 / Abschnitt 7). In Phase 1 ohne Wirkung, damit später
    // kein Aufrufer geändert werden muss.
    // Klassisches Pegel-Panning zusaetzlich zur Ohrgeometrie (@dpa 20260819:
    // "bitte noch ein normales Panning fuer die Kopfdrehung anbieten, also den
    // Anteil des normalen pannings von 0 - 100%").
    //
    // Die Ohrgeometrie allein verschiebt das Stereobild bei einer Kopfdrehung
    // fast nur ueber die Laufzeit. Der Pegelunterschied zwischen den beiden
    // Ohren ist bei einer weit entfernten Quelle winzig - der Kopf dreht sich,
    // im Stereobild passiert wenig. Das hier legt den Pegelunterschied darueber,
    // den ein gewoehnlicher Panorama-Regler machen wuerde.
    //
    // Gerechnet wird mit der Richtung, aus der der Schall TATSAECHLICH kommt,
    // also von der retardierten Quellposition zum Ohr. Nur so eilt das
    // Stereobild dem Klang nicht voraus: bei 400 m/s liegt zwischen der Stelle,
    // an der die Quelle gerade ist, und der, aus der man sie hoert, ein
    // erheblicher Winkel. Jede Spiegelung bekommt darueber automatisch ihre
    // eigene Richtung - eine Wandreflexion von links kommt von links.
    //
    // amount 0 schaltet das Panorama aus, 1 ist volles Panorama. right ist die
    // Rechts-Achse des Kopfes in Weltkoordinaten (siehe Listener.h), rightEar
    // waehlt das Ohr. Nach setTransform() aufrufen.
    void setPanning (double amount, Vec3 right, bool rightEar);

private:
    // Pegelfaktor dieses Ohres fuer Schall, der aus Richtung incoming kommt
    // (Einheitsvektor, vom Ohr zur Quelle). Siehe setPanning().
    double panoramaGain (Vec3 incoming) const;

public:

    void setNearFieldEnabled (bool shouldBeEnabled) { nearFieldOn = shouldBeEnabled; }
    void setDominantFrequencyHz (double hz) { dominantFreqHz = hz; }

    // Der einzige Rechenaufruf. Liest aus den geteilten Puffern, schreibt
    // additiv auf out (mehrere gleichzeitig aktive Zweige summieren sich - so
    // entsteht der Überschall-Doppelschlag ohne Sondercode, Plan 2.8).
    //
    // receiverPos gilt zu blockStartTime, receiverVel für den ganzen Block:
    // daraus wird die Ohrposition an jedem Solver-Punkt linear extrapoliert,
    // wie Plan 3.5 es verlangt ("pro Solver-Punkt neu ausgewertet, nicht pro
    // Block").
    void process (const SourceTrajectory&   traj,
                  const SourceSignalBuffer& sig,
                  const MediumState&        medium,
                  Vec3   receiverPos,
                  Vec3   receiverVel,
                  double blockStartTime,
                  float* out,
                  int    numSamples);

    // Nur für die Anzeige (Plan 3.12), lock-frei gelesen.
    int    numActiveBranches() const { return dispBranches.load(); }
    double lastDelaySeconds() const  { return dispDelay.load(); }

    // Siehe loudestContribution/loudestDTau.
    double loudestSampleDTau() const  { return loudestDTau.load(); }
    double loudestSampleLevel() const { return loudestContribution.load(); }
    double lastMachRadial() const    { return dispMach.load(); }

    // Messung zur Frage "mit welchem Hüllkurvenwert stirbt ein Zweig?"
    // (@dpa 20260819, Abbruch am Ende der Überschall-Hälfte).
    //
    // Ein Zweig, den der Löser nicht mehr meldet, wird über rampSeconds auf
    // null gefahren - unabhängig davon, wie laut er in diesem Moment war. Ist
    // die Rampe die Ursache des Abbruchs, muss env beim Übergang von "gemeldet"
    // auf "nicht mehr gemeldet" nahe 1 liegen; wäre der Zweig ohnehin schon
    // ausgeklungen, läge er nahe 0.
    //
    // Gezählt wird der Übergang selbst, nicht das spätere Freigeben des Slots:
    // env ist dort noch der Wert VOR der Abwärtsrampe.
    struct BranchDeathStats
    {
        std::uint64_t deaths     = 0;   // Übergänge gemeldet -> nicht mehr gemeldet
        std::uint64_t loudDeaths = 0;   // davon mit env >= 0,5
        double        envSum     = 0.0; // Summe der env-Werte, für den Mittelwert
        double        envMax     = 0.0;

        // Wie oft ein neu ankommender Zweig einen noch ausklingenden verdrängt
        // hat, weil kein Steckplatz frei war (siehe freeSlot()). Der Ausklang
        // hält einen Platz länger besetzt als die alte 1-ms-Rampe, deshalb ist
        // das die Zahl, an der man sieht, ob der Ausklang sich selbst im Weg
        // steht. Bleibt sie klein, ist der Fall theoretisch.
        std::uint64_t evictions = 0;

        // Wie viele der Tode ueberhaupt den Kaustik-Ausklang bekommen haben
        // (deathTau > 0), und wie lang der dann war. Ohne diese zwei Zahlen
        // laesst sich nicht unterscheiden, ob der Ausklang wirkt oder ob er
        // rechnerisch existiert und praktisch immer auf rampSeconds faellt.
        std::uint64_t causticDeaths = 0;
        double        tauSum        = 0.0;
        double        tauMax        = 0.0;

        // Die Pruefgroesse: Tode mit env >= 0,5, die schneller als
        // abruptSeconds auf null gegangen sind. Das ist der Abbruch, gezaehlt
        // statt gehoert. Null davon ist das Ziel.
        std::uint64_t abruptDeaths = 0;

        // Todesursachen aus dem Loeser, siehe RetardedTimeSolver.
        std::uint64_t trackLost   = 0;
        std::uint64_t newIds      = 0;
        std::uint64_t newIdsNear  = 0;
        std::uint64_t orderMatches = 0;
        std::uint64_t rootHist[8] {};
        std::uint64_t countFlips    = 0;
        std::uint64_t collapsed     = 0;
        std::uint64_t handovers     = 0;
        std::uint64_t tightPairs    = 0;
        std::uint64_t adjacentPairs = 0;
        std::uint64_t droppedRoots = 0;

        // N-Wellen-Auslöser, getrennt nach Ursache: Paar-Geburt an der
        // Kegelankunft, aufsteigender M_r-Durchgang (Unterschall ->
        // Überschall) und ABSTEIGENDER Durchgang (Überschall -> Unterschall).
        // Der absteigende ist der Verdacht aus @dpas Aufnahme 20260821: der
        // zeitverkehrte Zweig läuft nach der Kegelankunft zurück durch 1 und
        // löst denselben Puls ein zweites Mal aus.
        std::uint64_t nWavePairBirths = 0;
        std::uint64_t nWaveRising     = 0;
        std::uint64_t nWaveFalling    = 0;
    };

    BranchDeathStats branchDeaths() const
    {
        BranchDeathStats s;
        s.deaths     = deathCount.load();
        s.loudDeaths = deathLoudCount.load();
        s.envSum     = deathEnvSum.load();
        s.envMax     = deathEnvMax.load();
        s.evictions     = evictionCount.load();
        s.causticDeaths = causticCount.load();
        s.tauSum        = deathTauSum.load();
        s.tauMax        = deathTauMax.load();
        s.abruptDeaths  = abruptCount.load();
        s.trackLost     = solver.trackLostCount();
        s.newIds        = solver.newIdCount();
        s.newIdsNear    = solver.newIdNearCount();
        s.orderMatches  = solver.orderMatchCount();

        for (int i = 0; i < 8; ++i)
            s.rootHist[i] = solver.rootCountBucket (i);

        s.countFlips    = solver.rootCountFlips();
        s.collapsed     = solver.collapsedTrackCount();
        s.handovers     = handoverCount.load();
        s.tightPairs    = solver.tightPairCount();
        s.adjacentPairs = solver.adjacentPairCount();
        s.droppedRoots  = (std::uint64_t) std::max (0, solver.droppedRoots());
        s.nWavePairBirths = nWavePairBirthCount.load();
        s.nWaveRising     = nWaveRisingCount.load();
        s.nWaveFalling    = nWaveFallingCount.load();
        return s;
    }

    int maxBlockSize() const { return maxBlockSamples; }

    // Lastmaß des Lösers dieses Pfades (siehe
    // RetardedTimeSolver::residualEvaluations). Nur für Messungen.
    std::uint64_t solverEvaluations() const { return solver.residualEvaluations(); }

private:
    // Zustand eines Wurzelzweigs am zuletzt gerechneten Solver-Punkt.
    struct Branch
    {
        int    id   = -1;
        bool   used = false;

        double tau  = 0.0;   // τ = t_h − t_e [s]
        double dTau = 0.0;   // dτ/dt_h, analytisch (Plan 2.11)
        double amp  = 0.0;   // A = 1/(R·sqrt((1−M_r)² + eps²))
        double R    = 0.0;
        double mach = 0.0;

        double lpCoeff = 1.0;   // Luftdämpfung, pro Solver-Punkt aktualisiert
        double lpZ     = 0.0;   // Filterzustand - gehört zum Zweig (Plan 2.9)

        // Zustand des Schatten-Tiefpasses, siehe shadowRefHz. Eigener Zustand
        // neben lpZ: die Luftdaempfung beschreibt die geflogene Strecke, der
        // Schatten die Beugung an der Kaustik - zwei verschiedene Dinge, und
        // ein gemeinsamer Filterzustand liesse das eine im anderen
        // nachklingen.
        double shadowZ = 0.0;
        double refZ    = 0.0;   // Reflexionsdämpfung, ebenfalls je Zweig
        double env     = 0.0;   // Anti-Klick-Rampe, 0..1

        // Ob der Löser diesen Zweig im VORIGEN Solver-Segment gemeldet hat.
        // Daran hängt die Flanke "gemeldet -> nicht mehr gemeldet": sie zählt
        // die Todesmessung (siehe branchDeaths()) und legt gleichzeitig die
        // Länge des Ausklangs fest (siehe deathTau).
        bool wasAlive = false;

        // |dM_r/dt| aus den letzten beiden Solver-Punkten, in 1/s. Das ist die
        // Geschwindigkeit, mit der dieser Hörweg durch die Kaustik läuft, und
        // damit das Maß für die Breite des Übergangs - siehe deathTau.
        double machRate = 0.0;

        // Zeitkonstante des Ausklangs, in Sekunden, beim Tod einmal aus
        // machRate berechnet und danach fest. Siehe maxDeathTailSeconds.
        double deathTau = 0.0;

        // env im Moment des Todes und die seither vergangenen Samples. Daraus
        // entsteht die eigentliche Pruefgroesse: ein LAUTER Zweig, der in
        // wenigen Millisekunden auf null geht, ist genau der Abbruch. Siehe
        // abruptDeaths in BranchDeathStats.
        double deathEnvValue   = 0.0;
        int    deathSampleCount = 0;

        // Verzögerung im Moment des Todes. NICHT b.tau nehmen: das läuft
        // während des Ausklangs mit der zuletzt bekannten Steigung weiter, und
        // die ist an der Kaustik so steil, dass der Wert schon nach einem
        // Solver-Segment zig Millisekunden entfernt liegt. Der Vergleich für
        // die Zustandsübergabe braucht den Stand von damals.
        double deathTauValue   = 0.0;

        // --- N-Wellen-Schicht, siehe setNWave() ---
        //
        // machSeen wird beim ersten Solver-Punkt eines Zweigs gesetzt; ohne
        // einen gültigen Vorwert gibt es keine Durchquerung zu erkennen, und
        // ein frisch geborener Zweig würde sonst bei jeder Geburt auslösen.
        bool   machSeen  = false;
        double prevMach  = 0.0;

        // Laufzeit seit der Auslösung in Sekunden; negativ heißt "keine Welle".
        double nPhase    = -1.0;
        double nDuration = 0.0;
        double nRise     = 0.0;
        double nAmp      = 0.0;

        // Einseitige Druckbeule statt N-Welle (@dpa 20260824: "wenn der Knall
        // subsonic ist, dann ist es ja tatsaechlich eine einfache Beule, je
        // nach Groesse und Speed des Erzeugers laenger oder kuerzer, aber
        // 'einseitig' (nur /Druck\\)"). Das N mit seinen zwei Stossfronten
        // entsteht erst, wenn die Quelle die Schallgeschwindigkeit
        // ueberschreitet - eine unterschallige Beschleunigung schiebt nur eine
        // Verdichtung vor sich her.
        bool   nSingleSided = false;

        // Zustand der beiden Hochpaesse auf der N-Wellen-Schicht, siehe
        // nWaveHighpassHz. Zwei, weil einer nicht reicht: der Rumpf der Welle
        // ist eine RAMPE, und ein Hochpass erster Ordnung macht daraus eine
        // Konstante (er differenziert). Gemessen blieb der Mittelwert dann
        // ueber 80 ms bei -0,055 stehen statt bei null. Erst die zweite Stufe
        // laesst auch diese Konstante abklingen.
        double nHpZ  = 0.0;
        double nHpZ2 = 0.0;
    };

    // Wert der N-Welle zum Phasenzeitpunkt: steiler Anstieg auf +A, linearer
    // Abfall durch null, steiler Rücksprung von -A auf 0. Die klassische
    // N-Form, nicht bloß ein abklingender Impuls.
    static double nWaveAt (const Branch& b);

    // Zielzustand eines Zweigs am Ende des laufenden Solver-Segments.
    struct Target
    {
        bool   present = false;
        double tau     = 0.0;
        double dTau    = 0.0;
        double amp     = 0.0;
        double R       = 0.0;
        double mach    = 0.0;
        double lpCoeff = 1.0;
    };

    void   seedAt (const SourceTrajectory& traj, const MediumState& medium,
                   Vec3 recvPos, Vec3 recvVel, double t_h);
    void   evaluateRoot (const SourceTrajectory& traj, const Root& root,
                         Vec3 recvPos, Vec3 recvVel, double c, Target& out) const;
    int    findSlot (int id) const;
    int    freeSlot();

    // Setzt Dauer, Anstiegszeit, Amplitude und Phase des N-Wellen-Pulses auf
    // Zweig b und startet ihn (nPhase = 0). Gemeinsamer Code für beide
    // Auslöser (Paar-Geburt an der Kegelankunft und M_r-Durchgang eines
    // bereits bestehenden Zweigs, siehe process()) - beide sollen exakt
    // denselben Puls erzeugen, keine zwei leicht auseinanderlaufenden Formeln.
    // levelScale skaliert die Pulsamplitude. 1 = der volle Ueberschallknall;
    // der Sprung-Knall kommt mit einem kleineren Wert herein, der mit der
    // Sprunghoehe waechst.
    // ducksOthers sagt, ob dieser Puls den uebrigen Schall absenkt (siehe
    // setShockDuck). Nur eine echte Stossfront tut das - sie ist die
    // Diskontinuitaet selbst, hinter der fuer die Dauer des Pulses nichts
    // anderes herkommt. Eine unterschallige Druckbeule ist keine: sie laeuft
    // MIT dem uebrigen Schall, nicht statt seiner.
    //
    // singleSided macht aus der N-Welle die einseitige Beule, siehe
    // Branch::nSingleSided.
    // sizeOverride > 0 setzt die Laenge der Welle, statt sie aus nWaveSize zu
    // nehmen. Gebraucht wird das vom Startknall: er bildet eine
    // BESCHLEUNIGUNG ab und keinen Koerper, und wie lange die dauert, hat mit
    // der Groesse des Objekts nichts zu tun.
    //
    // radiusOverride > 0 setzt die Entfernung, mit der Amplitude und
    // Frontbreite gerechnet werden, statt der aktuellen Entfernung des
    // Zweigs. Gebraucht wird das vom Startknall: der entstand am STARTPUNKT,
    // und von dort ist er gelaufen - wo die Quelle inzwischen steht, hat
    // damit nichts zu tun. Ohne diesen Umweg waere ein Knall bei Mach 3
    // um ein Vielfaches leiser als bei Mach 0,6, weil die Quelle in derselben
    // Zeit weiter geflogen ist (@dpa 20260825: "Ich will einen Knall
    // unabhaengig vom M speed").
    void triggerNWave (Branch& b, double c, double listenerTimeNow, double levelScale = 1.0,
                       bool ducksOthers = true, bool singleSided = false,
                       double radiusOverride = 0.0, double sizeOverride = 0.0);

    // Absenkungsfaktor durch die Stossfront zur Hoererzeit t, 1 = unberuehrt.
    double shockDuckAt (double listenerTime) const;

    double lowpassCoeff (double R) const;

    // Phase-2-Vorbereitung (Plan 2.7). Liefert in Phase 1 konstant 0, der
    // Aufruf steht aber bereits an der richtigen Stelle.
    double nearFieldGain (double R, double dominantFrequencyHz) const;

    RetardedTimeSolver solver;
    PathTransform      transform;

    // Der Versatz des vorigen Blocks. Der Transform wird nur einmal je Block
    // gesetzt (wandernde Waende, Klon-Versatz samt Wackler), seine Aenderung
    // waere sonst ein Positionssprung im Blockraster: bei 512 Samples alle
    // 10,7 ms, und schon ein halber Meter Sprung sind 1,5 ms Laufzeitsprung.
    // Das klingt nach Bitcrusher, nicht nach Bewegung (@dpa 20260820: "Klone
    // klingen Bitcrashed"). Statt zu springen wandert der Empfaenger den Block
    // ueber von hier nach dort, siehe process().
    Vec3               prevTransformOffset {};
    bool               hasPrevTransformOffset = false;

    Branch branches[maxBranchSlots];

    double sr              = 0.0;
    int    maxBlockSamples = 0;

    double lastSolveTime     = 0.0;
    double lastDiscoveryTime = 0.0;
    bool   seeded            = false;

    // 0,5 ms. Obergrenze der Entdeckungslatenz für eine Kegelankunft und damit
    // eine Modellkonstante wie die Rampendauer, kein Regler. Die Größe ist an
    // der Toleranz gewählt, die solver_check für die Kegelankunft ansetzt
    // (3 ms) - mit reichlich Abstand darunter.
    double discoverySeconds = 0.5e-3;


    // Exponent von R in A_geo = 1/R^k, siehe setDistanceCurve().
    //
    // plainInverseR ist die Abkürzung für k = 1: dort wird std::pow gar nicht
    // gerufen, sondern R direkt benutzt. Nicht aus Sparsamkeit, sondern damit
    // der Standardfall garantiert bitgleich zum reinen 1/R bleibt, statt an der
    // Genauigkeit von pow(R, 1.0) zu hängen.
    double distanceExponent = 1.0;
    bool   plainInverseR    = true;

    // Exponent an den beiden Reglerenden. Unsymmetrisch um 1, weil "flacher"
    // sich schon bei kleiner Änderung deutlich hört, "schärfer" aber Luft nach
    // oben braucht.
    static constexpr double distanceExponentSteep = 2.5;
    static constexpr double distanceExponentFlat  = 0.3;

    // Regularisierung und Untergrenzen (Plan 2.7).
    double boomDb    = 30.0;
    double eps       = 0.0316227766016838;   // 10^(-30/20)
    double minRadius = 0.05;                 // R_min [m]

    // Luftdämpfung (Plan 2.9).
    double airFc0      = 18000.0;
    double airRefM     = 10.0;
    double airExponent = 0.7;
    double airAmount   = 1.0;

    // Reflexionsdämpfung. 0 = aus, dann wird der Filter gar nicht erst
    // durchlaufen (der Direktschall zahlt für dieses Bauteil also nichts).
    double reflectAmount = 0.0;
    double reflectFcHz   = 800.0;

    int baseStride       = 64;   // Plan 2.11: 64 Samples, 750 Hz bei 48 kHz
    int supersonicStride = 8;    // Plan 2.11: adaptiv feiner bei Überschall

    double trajGridSeconds = 1.0e-3;   // 1 kHz Rasterrate (Plan 2.12)
    double rampSeconds     = 1.0e-3;   // Mitte von 0,5..2 ms (Plan 3.7)

    // Ausklang eines Zweigs, den der Löser nicht mehr meldet.
    //
    // Der EINSATZ eines Zweigs bleibt die lineare Rampe aus Plan 3.7: eine
    // Kegelankunft ist eine echte Stoßfront, die darf steil sein. Sein ENDE
    // ist aber etwas anderes. Zwei Wurzeln laufen an der Mach-Front zusammen
    // und verschwinden dort - und zwar bei ihrer GRÖSSTEN Amplitude, weil der
    // Fokussierungsfaktor 1/sqrt((1-M_r)²+eps²) genau dort sein Maximum hat.
    // Sie mit derselben festen Rampe auf null zu fahren, schneidet den Klang
    // bei vollem Pegel ab; gemessen (Zweig-Tod-Zählwerk, load_check) stirbt ein
    // Zweig im Überschall mit env im Mittel 0,69 bis 0,95 und Maximum 1,000.
    // Das ist der von @dpa beschriebene Abbruch am Ende der Überschall-Hälfte.
    //
    // Physikalisch endet das Feld an einer Faltungskaustik nicht, es geht mit
    // einem Ausläufer in den Schattenbereich weiter. Dessen Breite ist keine
    // freie Wahl: sie hängt davon ab, wie schnell die Geometrie durch die
    // Kaustik läuft. Deshalb
    //
    //     tau = eps / |dM_r/dt|
    //
    // also die Zeit, die M_r braucht, um sich um genau eine
    // Regularisierungsbreite zu bewegen. eps ist dabei nicht neu erfunden,
    // sondern dasselbe eps, mit dem "Boom Limit" die Divergenz schon glättet -
    // der Ausläufer bekommt damit exakt die Breite, auf die das Modell den
    // Kaustik-Übergang ohnehin festgelegt hat. Ein Durchflug knapp durch Mach 1
    // ist damit von selbst kurz und knackig, ein langsames Hineingleiten von
    // selbst weich, ohne dass irgendwo eine Zeit eingestellt werden müsste.
    //
    // Nach unten begrenzt rampSeconds (kürzer als die Anti-Klick-Rampe darf der
    // Ausklang nie werden, sonst wäre er wieder ein Knacks). Nach oben begrenzt
    // maxDeathTailSeconds: bei dM_r/dt gegen null ginge tau gegen unendlich,
    // der Zweig bliebe für immer hörbar und würde einen Steckplatz belegen.
    // 100 ms ist grosszügig gewählt - das Zehn- bis Hundertfache dessen, was
    // die Messung als typischen Kaustik-Durchlauf zeigt - und steht hier
    // ausdrücklich sichtbar, statt als stiller Deckel im Code zu verschwinden.
    static constexpr double maxDeathTailSeconds = 0.1;

    // Wie viele Regularisierungsbreiten um M_r = 1 herum noch als "an der
    // Kaustik" gelten. Nur dort bekommt ein sterbender Zweig den Ausläufer;
    // ausserhalb ist der Fokussierungsfaktor unauffällig und die urspruengliche
    // lineare Anti-Klick-Rampe gilt weiterhin (siehe Auswertung im .cpp).
    //
    // Ohne diese Eingrenzung bekaemen auch Tode einen langen Ausklang, die gar
    // nichts mit der Mach-Front zu tun haben (verlorene Nachführung) - an den
    // Verdraengungen im load_check zeigt sich das: sie hielten dann Steckplätze
    // besetzt, bis ein neu ankommender Zweig sie hart hinauswirft. Das würde den
    // abgeschnittenen Zweig nur an eine andere Stelle verschieben.
    static constexpr double causticWidths = 4.0;

    // Breite des Fensters um M_r = 1, in dem ein neues Wurzelpaar als
    // Kegelankunft gilt und ein Zweigtod als Kaustik-Tod.
    //
    // Das ist GEOMETRIE und hängt deshalb nicht an eps. eps ist der Regler
    // "Boom Limit": er glättet die Amplitudenformel an der Front, er
    // entscheidet nicht, OB ein Kegel eintrifft. Als Fenster benutzt macht er
    // das Gegenteil dessen, was auf ihm steht - je weiter aufgedreht, desto
    // schmaler das Fenster, desto seltener wird ein Knall überhaupt erkannt.
    // Gemessen (whip_probe, peitschentest, 5 s): 39 Kegelankünfte bis 30 dB,
    // 5 bei 45 dB, 1 bei 60 dB, und die Spitze fiel dabei von 0,835 auf 0,366.
    //
    // Der Wert ist der, den die Vorgabe von 30 dB ergab (4,0 · 10^(-30/20)),
    // damit sich an der Vorgabestellung nichts ändert.
    static constexpr double causticWindow = 0.12649110640673518;

    // --- Form des Schattenausklangs ---
    //
    // Hinter einer Faltungskaustik hoert das Feld nicht auf, es geht gebeugt
    // weiter, und die Beugung ist FREQUENZABHAENGIG. Die geschlossene Loesung
    // dafuer ist die Airy-Funktion; fuer den Schattenbereich gilt
    //
    //     Ai(x) ~ exp(-2/3 * x^(3/2)),   x ~ s * f^(1/3)
    //
    // (s = Abstand hinter der Schattengrenze, f = Frequenz). Setzt man s
    // proportional zur Zeit, die der Hoerweg seit dem Uebergang im Schatten
    // liegt, folgt daraus die Huellkurve
    //
    //     A(f, t) = exp( -(t/tau)^(3/2) * (f/f_ref)^(1/2) )
    //
    // Daran haengen die beiden Eigenschaften, die ein blosses exp(-t/tau)
    // nicht hat und die @dpa wiederholt als falsch gehoert hat
    // (20260827: "da ist oft eine richtiggehende Kante ... bei Schatten 1ms
    // macht es halblaut 'Bum-hhaupt!' direkt danach '..entfernt Leise'"):
    //
    //   Der Exponent 3/2 laesst den Abfall mit Steigung NULL beginnen. Ein
    //   Ein-Pol faellt vom ersten Moment an mit voller Rate - das ist der
    //   Knick, den man als Kante hoert, und er bleibt einer, egal wie lang
    //   man tau stellt.
    //
    //   Der Faktor f^(1/2) nimmt die Hoehen zuerst. Gemessen an @dpas
    //   Aufnahme vom 20260827 fiel das GANZE Spektrum gleichzeitig
    //   (30-60 Hz um 38 dB, 1-2 kHz um 30 dB, 4-8 kHz um 29 dB) - ein
    //   Schnitt durchs Band, kein Schatten. Etwas, das hinter einer Ecke
    //   verschwindet, wird dumpf, bevor es leise wird.
    //
    // shadowRefHz ist die Frequenz, deren Abklingzeit genau tau ist; darueber
    // geht es schneller, darunter langsamer. Der Wert ist eine Modell-
    // konstante - der echte Bezug haengt am Kruemmungsradius der Kaustik, den
    // das Modell nicht fuehrt. 1 kHz liegt dort, wo das Ohr am genauesten
    // hinhoert, und macht damit die Zeitkonstante tau zu dem, was der Regler
    // verspricht.
    static constexpr double shadowRefHz     = 1000.0;

    // Exponent der Zeit in der Huellkurve oben. Steht als benannte Groesse
    // hier und nicht als 1.5 im Code, weil er die Form des Ausklangs IST.
    static constexpr double shadowTimeExponent = 1.5;

    // Ausklang fuer einen Zweig, der NICHT an der Kaustik stirbt, sondern weil
    // der Loeser seine Wurzel verloren hat.
    //
    // Schall hoert nicht abrupt auf. Verschwindet ein Zweig bei voller
    // Huellkurve, ist die naheliegende Erklaerung deshalb nicht "die Quelle ist
    // verstummt", sondern "wir haben sie aus den Augen verloren" - ein
    // Loeserereignis, kein akustisches. Ihn dann in einer Millisekunde
    // wegzublenden loescht echtes Signal: gemessen fiel der Pegel nach einem
    // Ueberschall-Vorbeiflug binnen einer halben Sekunde um 15 bis 20 dB,
    // waehrend die groessere Entfernung in derselben Zeit nur 2,4 dB erklaert
    // (@dpa 20260820: "warum ist das Rückwärts noch laut und danach ist
    // ploetzlich stille.. das kann doch nicht wahr sein!").
    //
    // Der Zweig laeuft dabei mit seiner zuletzt bekannten Steigung weiter, das
    // ist eine Extrapolation und wird mit der Zeit ungenauer - deshalb kurz
    // genug, dass daraus kein Hall wird, und nur fuer Zweige, die laut genug
    // sterben, dass ihr Fehlen als Loch auffaellt.
    static constexpr double lostBranchTailSeconds = 0.08;
    static constexpr double lostBranchMinEnv      = 0.05;

    // Unterhalb dieses Hüllkurvenwerts gilt ein sterbender Zweig als fertig und
    // sein Steckplatz wird frei. Nötig, weil ein exponentieller Ausklang die
    // Null nie erreicht. -80 dB liegt unter allem, was neben dem Direktschall
    // noch hörbar wäre.
    static constexpr double envFloor = 1.0e-4;

    // Ab wann ein Ausklang als "schlagartig" gilt. 2 ms deshalb, weil @dpas
    // Aufnahme den Abbruch mit ueber 20 dB in 0,75 ms zeigt und die
    // Anti-Klick-Rampe 1 ms lang ist - beides liegt klar darunter, ein
    // Ausklang, der der Kaustik folgt, klar darueber.
    static constexpr double abruptSeconds = 2.0e-3;

    // Wie nah zwei Verzögerungen liegen müssen, damit ein neu gemeldeter Zweig
    // als Fortsetzung eines sterbenden gilt und dessen Zustand übernimmt.
    //
    // 2 ms sind bei 343 m/s rund 70 cm Wegunterschied. Zwei wirklich
    // verschiedene Hörwege liegen im Feldmassstab weiter auseinander; liegen
    // sie enger, sind sie ohnehin im Begriff zu verschmelzen und ein
    // Zustandsübergang zwischen ihnen ist unhörbar.
    static constexpr double handoverTauSeconds = 2.0e-3;

    // Wie lange ein gestorbener Zweig als Fortsetzungskandidat gilt. Danach ist
    // der Klang ohnehin abgeklungen und eine Übergabe würde einen alten
    // Filterzustand in einen neuen Hörweg tragen.
    static constexpr double handoverMaxAgeSeconds = 20.0e-3;

    // Panorama-Anteil samt Kopfachse und Ohr, siehe setPanning().
    double panAmount   = 0.0;
    Vec3   panRight    { 1.0, 0.0, 0.0 };
    bool   panRightEar = false;

    bool   nearFieldOn     = false;
    double dominantFreqHz  = 0.0;

    // N-Wellen-Schicht. Default aus - eine Druckwelle, die immer mitläuft,
    // wollen die wenigsten.
    bool   nWaveOn        = false;

    // Siehe setBoomGate. Ohne Sperre (nullptr oder holdSeconds 0) loest jeder
    // Durchgang aus wie bisher.
    BoomGate* boomGate = nullptr;
    double nWaveSizeM     = 15.0;

    // Schaerfe der Stossfronten, 0..1 (siehe setNWave). 0,5 ist die Mitte und
    // heisst "wie ohne Regler".
    double nWaveEdge      = 0.5;

    // Staerke der Nulllinien-Auslenkung, siehe Params::nWavePressure.
    double nWavePressure  = 1.0;

    // Laenge des Startknalls in Metern, siehe setJumpSize(). Eigene Groesse,
    // weil er eine Beschleunigung abbildet und keinen Koerper.
    double jumpSizeM      = 1.5;

    // Regelbarer Pegel des Knalls, linear (siehe Params::nWaveGainDb).
    double nWaveGain      = 1.0;


    // Siehe setExtraPathGain(). 1.0 = unveraendert.
    // Fest unterdrueckt statt einstellbar - Begruendung an setRumble() und in
    // Params.h. Nicht null: ein hart abgeschalteter Zweig waere ein Sprung im
    // Signal, und die Wege sind ohnehin gerechnet.
    static constexpr double extraPathGain = 0.001;   // -60 dB

    // Rollen: Stellwerte und laufender Zustand.
    double rumbleGain    = 0.5;      // Pegel gegenueber der Stossfront
    double rumbleSeconds = 1.5;      // Abklingzeit
    double rumbleEdgeLo  = 5.0;      // Kantenrate am Anfang
    double rumbleEdgeHi  = 3000.0;   // Kantenrate am Ende
    double rumbleTone    = 180.0;    // Tiefpass ueber den Kanten

    double rumbleAmp     = 0.0;      // Amplitude des laufenden Rollens, 0 = still
    double rumbleLpZ     = 0.0;      // Zustand des Farbfilters
    double rumbleHold    = 0.0;      // gehaltener Wert zwischen zwei Kanten
    double rumblePhase   = 0.0;      // Zaehler bis zur naechsten Kante
    std::uint32_t rumbleRng = 0x9E3779B9u;

    // Sekunden seit der Stossfront. NEGATIV heisst: die Welle laeuft noch, das
    // Rollen wartet - siehe setRumble(). Es zaehlt trotzdem mit, damit es genau
    // dann einsetzt, wenn die Welle vorbei ist.
    double rumbleAge     = 0.0;

    // Wieviele Zeitkonstanten das Rollen in seiner eingestellten Dauer
    // durchlaeuft. Fuenf heisst: am Ende steht es bei -43 dB, also unter allem,
    // was noch zu hoeren waere - es reisst nicht ab, es ist zu Ende.
    static constexpr double rumbleDecays = 5.0;

    // Wieviel spaeter ein Zweig eintreffen muss, um als ZUSAETZLICHER Weg zu
    // gelten. Zwei Zweige, die praktisch gleichzeitig ankommen, sind derselbe
    // Hoerweg kurz vor oder nach einer Falte - die darf der Regler nicht
    // gegeneinander ausspielen.
    static constexpr double extraPathMinDelay = 0.002;


    // Siehe setShockDuck(). Default 0 = aus, damit bestehende Presets
    // unveraendert klingen.
    double shockDuckAmount = 0.0;
    double shockDuckRange  = 0.0;   // 0 = entfernungsunabhaengig

    // Tiefe der gerade laufenden Absenkung, beim Ausloesen aus der Entfernung
    // gerechnet (siehe setShockDuck).
    double shockDuckStrength = 0.0;

    // Rueckkehrzeit nach der Stossfront. Feste Groesse statt Regler: sie soll
    // nur die Kante entschaerfen, nicht gestaltet werden.
    static constexpr double shockDuckRelease = 0.01;

    // Ende der zuletzt ausgeloesten Stossfront, in Hoererzeit. Pfadweit statt
    // je Zweig gefuehrt (siehe setShockDuck) und als Zeitpunkt statt als
    // Huellkurve, weil die Zweige nacheinander ueber denselben Sample-Bereich
    // laufen - ein gemeinsamer Huellkurvenzustand liesse sich so nicht
    // fortschreiben, ein gemeinsamer Zeitpunkt schon.
    double shockEndTime = -1.0e18;


    // Siehe setJumpBoom().
    double jumpBoom   = 0.0;

    // Siehe setJumpMarker(). Der Anfangswert liegt so weit in der
    // Vergangenheit, dass er nie ueberschritten wird - ohne gesetzte Marke
    // gibt es keinen Sprung.
    double jumpMarkerTime     = -1.0e18;
    double jumpMarkerStrength = 0.0;

    // Wann der Startknall beim Hoerer ANKOMMT, in Hoererzeit. Wird beim
    // ersten process() nach dem Setzen der Marke einmal ausgerechnet
    // (Emissionszeit plus Laufzeit vom Startpunkt ueber DIESEN Weg) und danach
    // nur noch abgewartet.
    //
    // Der Grund fuer diesen Umweg (@dpa 20260825: "was ist nur mit dem
    // KnallStart los.. er ist wieder nicht hoeren"): die Emissionszeit eines
    // Zweigs direkt gegen die Marke zu pruefen, funktioniert nur bei
    // Unterschall, wo ein Zweig die Bahn durchgehend verfolgt. Bei Ueberschall
    // werden Zweige neu GEBOREN - ihre Emissionszeit beginnt jenseits der
    // Marke und laeuft nie darueber. Ohne den Umweg ueber die Ankunftszeit
    // bleibt der Knall bei Mach 1,5 und Mach 3 gemessen nicht nur leise,
    // sondern bitgleich abwesend.
    //
    // Die Ankunftszeit haengt an nichts davon. Der Knall entsteht am
    // Startpunkt und braucht seine Laufzeit dorthin - mehr ist daran nicht.
    double jumpArrivalTime = -1.0e18;
    bool   jumpArmed       = false;

    // Abstand des Startpunkts ueber diesen Weg, aus derselben Rechnung wie
    // jumpArrivalTime. Damit werden Amplitude und Frontbreite des Startknalls
    // bestimmt - siehe radiusOverride in triggerNWave().
    double jumpDistance    = 0.0;

    // Spitzendruck der N-Welle in einem Meter Abstand. Modellkonstante, kein
    // Regler: die Regler sind An/Aus und Größe. Der Wert ist so gewählt, dass
    // die Welle in typischer Vorbeiflug-Entfernung in derselben Größenordnung
    // liegt wie der Direktschall - beurteilen muss ihn @dpas Ohr.
    // Pegel der Druckwelle bei nWaveRefMetres. Gross, weil ein Ueberschallknall
    // die Szene beherrscht statt sich einzureihen: bei 8.0 kam auf 500 m eine
    // Amplitude von 0,036 heraus, also 8 dB UNTER dem Motorgeraeusch derselben
    // Szene (gemessene Signalspitze -21 dB) - @dpa hoerte dort folgerichtig
    // "ein bisschen ziusch.. aber nichts was an einen Schlag oder Druck oder
    // gar nur Lautheit erinnert". Ein realer Knall liegt Zehnerpotenzen darueber.
    //
    // Mit 40.0 sind es auf 500 m rund 0,18, also etwa 6 dB ueber der Szene.
    // In der Naehe uebersteuert das und laeuft in den Limiter - das ist kein
    // Versehen: ein Knall aus 20 m IST ohrenbetaeubend, und der Limiter ist
    // sichtbar und abschaltbar, statt die Welle heimlich klein zu halten.
    static constexpr double nWaveLevel = 40.0;

    // Abstandsgesetz der N-Welle, siehe ausführliche Begründung an der
    // Verwendungsstelle. Der Exponent 3/4 ist der Standardwert für eine
    // nichtlinear alternde N-Welle (gegenüber 1 für gewöhnlichen Kugelschall);
    // die Bezugsentfernung hält den eingehörten Nahbereich fest.
    static constexpr double nWaveDistanceExponent = 0.75;
    static constexpr double nWaveRefMetres        = 20.0;

    // Größenkopplung der Lautstärke (@dpa: "die N-Welle ist das Druckabbild
    // des Körpers ... Größerer Körper = lauterer Knall"). Ausführliche
    // Begründung an der Verwendungsstelle in triggerNWave().
    //
    // nWaveSizeRefMetres ist bewusst dieselbe Zahl wie der Skew-Mittelpunkt
    // und Default des "N-Wave Size"-Reglers (Params.cpp, 15 m): dort ist der
    // Kopplungsfaktor exakt 1 und der eingehörte Klang bleibt bei
    // mittlerer Reglerstellung unverändert - kein Presets-Sprung.
    static constexpr double nWaveSizeRefMetres = 15.0;
    static constexpr double nWaveSizeExponent  = 0.75;

    // Reichweite des Schaerfereglers, in Oktaven JE REGLERHAELFTE (siehe
    // setNWave). Fuenf Oktaven sind Faktor 32: aus 6 ms Anstieg - der Wert, den
    // ein 17-m-Koerper in 2 km Entfernung erzeugt - werden am oberen Anschlag
    // 0,19 ms. Das ist bei 48 kHz eine Flanke ueber neun Samples, also eine
    // hoerbare Kante und kein weicher Uebergang mehr; nach unten wird derselbe
    // Knall zum Grollen von knapp 0,2 s Anstieg.
    static constexpr double nWaveEdgeOctaves = 5.0;

    // --- Warum die N-Welle hochpassgefiltert wird ---
    //
    // Eine N-Welle ist ein DRUCKVERLAUF: Sprung auf +A, linearer Abfall durch
    // null, Ruecksprung von -A auf 0. Als Audiosignal addiert ist ihr Rumpf
    // eine einzige langsame Auslenkung - bei einem 17-m-Koerper dauert sie
    // 99 ms, das entspricht rund 10 Hz. Diese Auslenkung beherrscht den
    // Pegel, und wenn sie am Heckstoss endet, reisst sie alles mit.
    //
    // Ohne Hochpass ist genau das die hoerbare Kante. Gemessen im
    // Kreisflug-Szenario des load_check: der Mittelwert des Ausgangs springt
    // bei t = 2,254 s auf +0,0034 (Bugstoss), laeuft linear durch null auf
    // -0,0054 und springt bei t = 2,352 s zurueck - 98 ms Dauer, exakt
    // 2*size/c, und der Pegel faellt dabei um 41 dB in 2 ms.
    //
    // Gehoert wird eine N-Welle so nicht. Weder Ohr noch Lautsprecher
    // uebertragen 10 Hz nennenswert; was ankommt, sind die beiden
    // Stossfronten - der Doppelknall, den man von Ueberschallflugzeugen
    // kennt. Der Hochpass bildet genau diesen Weg ab: er laesst die Fronten
    // stehen und nimmt die Auslenkung dazwischen heraus.
    //
    // 20 Hz ist die untere Hoergrenze und zugleich die Gegend, in der jede
    // Wiedergabekette ohnehin abschneidet. Der Knall selbst bleibt davon
    // unberuehrt - eine Stossfront ist breitbandig, ihre Energie liegt weit
    // oberhalb.
    static constexpr double nWaveHighpassHz = 20.0;

    // Wie klein der Filterzustand sein muss, damit eine Welle als beendet
    // gilt. Der Nachschwinger der Hochpaesse gehoert zur Welle - wird er
    // abgeschnitten, ist genau das wieder ein Sprung (gemessen 0,246 statt
    // 0,004 groesster Samplesprung).
    static constexpr double nWaveTailFloor = 1.0e-6;

    pathdetail::DisplayValue<int>    dispBranches;
    pathdetail::DisplayValue<double> dispDelay;
    pathdetail::DisplayValue<double> dispMach;

    // Todesmessung, siehe branchDeaths(). Audiothread schreibt, Message-Thread
    // liest - dieselbe Bauart wie die Anzeigewerte darüber, damit
    // PropagationPath kopierbar bleibt (std::vector in DopplerEngine).
    pathdetail::DisplayValue<std::uint64_t> deathCount;
    pathdetail::DisplayValue<std::uint64_t> deathLoudCount;
    pathdetail::DisplayValue<double>        deathEnvSum;
    pathdetail::DisplayValue<double>        deathEnvMax;
    pathdetail::DisplayValue<std::uint64_t> evictionCount;
    pathdetail::DisplayValue<std::uint64_t> causticCount;
    pathdetail::DisplayValue<double>        deathTauSum;
    pathdetail::DisplayValue<double>        deathTauMax;
    pathdetail::DisplayValue<std::uint64_t> abruptCount;
    pathdetail::DisplayValue<std::uint64_t> handoverCount;

    // Auslöserichtung der N-Welle, siehe BranchDeathStats weiter oben.
    // Messung zur "Fahne" (@dpa 20260827): mit welchem dTau kommt der
    // LAUTESTE Beitrag? Davon haengt ab, ob der Regler "Rueckwaerts" ihn
    // ueberhaupt fassen kann - seine Blende oeffnet erst ueber dTau = 1.
    pathdetail::DisplayValue<double> loudestContribution;
    pathdetail::DisplayValue<double> loudestDTau;

    pathdetail::DisplayValue<std::uint64_t> nWavePairBirthCount;
    pathdetail::DisplayValue<std::uint64_t> nWaveRisingCount;
    pathdetail::DisplayValue<std::uint64_t> nWaveFallingCount;
};
