#pragma once

#include "Medium.h"
#include "SourceTrajectory.h"
#include "Vec3.h"

#include <cstdint>

// Eine gefundene Wurzel der Retarded-Time-Gleichung (Plan 2.10). Jede Wurzel
// ist ein eigener Hörweg: bei Unterschall gibt es genau eine, im Mach-Kegel
// kommt ein Paar dazu.
struct Root
{
    double t_e        = 0.0;  // Emissionszeit [s]
    double R          = 0.0;  // Abstand |L(t_h) - M(t_e)| [m]
    double machRadial = 0.0;  // M_r = û·v_M / c, positiv bei Annäherung
    int    id         = -1;   // stabile Zweig-Identität über Aufrufe hinweg
};

// Löst F(t_e) = c*(t_h - t_e) - |L(t_h) - M(t_e)| = 0 (Plan 2.3).
//
// Reines C++, keine JUCE-Abhängigkeit (Plan 3.4), damit der physikalisch
// kritischste Teil offline gegen die geschlossene Lösung aus Plan 2.5 geprüft
// werden kann (Tests/solver_check.cpp).
//
// Der Löser trägt Zustand: eine kleine Liste bekannter Zweige mit ihrer
// letzten Wurzel. Daraus kommen zwei Dinge - ein guter Startwert für den
// nächsten Solver-Punkt (billig statt Vollscan, Plan 2.10 Schritt 2) und
// stabile ids, an denen die Envelope-Logik in H5 erkennt, ob ein Zweig
// derselbe geblieben, neu erschienen oder verschwunden ist.
//
// Eine Instanz gehört zu genau einem Empfangspunkt (Ohr, Spiegelquelle), weil
// der Zweigzustand an dessen Geschichte hängt.
class RetardedTimeSolver
{
public:
    // K aus Plan 2.6: mehr als vier gleichzeitige Zweige werden verworfen.
    static constexpr int maxBranches = 8;

    // Vergisst alle bekannten Zweige. Nach einem Positionssprung oder einem
    // prepareToPlay aufzurufen, damit keine Wurzel aus der alten Geometrie
    // nachgeführt wird.
    void reset();

    // Rückgabe: Anzahl der nach outRoots geschriebenen Wurzeln, aufsteigend
    // nach t_e sortiert (älteste Emission zuerst). Bei Überlauf werden die
    // ältesten verworfen, weil die jüngeren Emissionen den aktuellen Klang
    // tragen.
    //
    // allowFullScan trennt zwei Dinge, die bisher zusammenhingen: das
    // NACHFÜHREN bereits bekannter Zweige (billig, passiert immer) und das
    // ENTDECKEN neu entstandener (der Lipschitz-Vollscan über das gesamte
    // Suchfenster, im Überschall der mit Abstand teuerste Posten). Neue Zweige
    // entstehen nur bei einer Kegelankunft; sie deshalb seltener zu suchen als
    // die vorhandenen nachzuführen, kostet nichts weiter als eine
    // Entdeckungslatenz in der Größe des Abfrageabstands - und die liegt weit
    // unter der 3-ms-Toleranz, die solver_check für die Kegelankunft ansetzt.
    //
    // Ein Sonderfall bleibt immer beim Vollscan, egal was der Aufrufer sagt:
    // wenn kein einziger Zweig nachgeführt werden konnte, wäre die Alternative
    // Stille.
    int solve (const SourceTrajectory& traj,
               const MediumState& medium,
               Vec3 receiverPos,
               double t_h,
               Root* outRoots,
               int maxRoots,
               bool allowFullScan = true);

    // Untergrenze der Scan-Schrittweite (Plan 2.10: "minStep ist die
    // Trajektorien-Rasterweite, damit der Scan terminiert"). Der Löser kennt
    // das Raster der Trajektorie nicht, deshalb hier einstellbar; der Default
    // entspricht der 1-kHz-Rasterrate aus Plan 2.12.
    void   setMinScanStep (double seconds);
    double minScanStep() const { return minStep; }
    double trackingStep() const { return trackStep; }

    // Warum ein Zweig aufhoert, gemeldet zu werden - drei sich ausschliessende
    // Ursachen, damit die Behebung nicht wieder auf einer Vermutung aufsetzt:
    //
    //   trackLost   - die Bracket-Suche fand die Wurzel nicht mehr. Der Zweig
    //                 ist verloren, obwohl er physikalisch da sein duerfte.
    //   newIdGiven  - eine Wurzel liess sich keinem bekannten Zweig zuordnen
    //                 und bekam eine neue Identitaet. Fuer den Pfad ist das
    //                 ein Tod plus eine Geburt am selben Ort: neue Huellkurve,
    //                 neuer Filter, obwohl derselbe Hoerweg gemeint ist.
    //   droppedRoot - mehr Wurzeln als Steckplaetze, die aelteste faellt raus.
    std::uint64_t trackLostCount()  const { return trackLost; }
    std::uint64_t newIdCount()      const { return newIdGiven; }

    // Aufschluesselung der neuen Identitaeten nach dem Abstand zur naechsten
    // Vorhersage eines bekannten Zweigs, gemessen in Vielfachen der Toleranz:
    //
    //   Near - knapp daneben (< 2x). Dann ist es derselbe Hoerweg und die
    //          Toleranz ist zu eng.
    //   Far  - weit weg (>= 2x). Dann ist es wirklich eine neue Wurzel und die
    //          neue Identitaet ist richtig.
    //
    // Ohne diese Trennung laesst sich nicht sagen, ob die Zuordnung zu streng
    // ist oder ob im Ueberschall tatsaechlich staendig Zweige entstehen.
    std::uint64_t newIdNearCount() const { return newIdNear; }

    // Wie oft die Zuordnung ueber die Reihenfolge gegriffen hat.
    std::uint64_t orderMatchCount() const { return orderMatched; }

    // Verteilung der gefundenen Wurzelzahl je Aufruf, und wie eng benachbarte
    // Wurzeln beieinanderliegen.
    //
    // Die Frage dahinter: sind die vielen neuen Wurzeln echt? Physikalisch gibt
    // es im Ueberschall zwei zusaetzliche Hoerwege, nicht sechs. Findet der
    // Loeser regelmaessig mehr, und liegen die dicht beieinander, dann sind es
    // keine Hoerwege, sondern Nulldurchgaenge einer Funktion, die dort fast
    // flach ist: an der Kaustik gilt F' = -c*(1 - M_r) -> 0, und jede noch so
    // kleine Welligkeit in R(t_e) erzeugt dann gleich mehrere Vorzeichen-
    // wechsel. Die Welligkeit kaeme aus der Catmull-Rom-Interpolation der
    // Trajektorie auf ihrem 1-kHz-Raster.
    std::uint64_t rootCountBucket (int n) const
    {
        return (n >= 0 && n < 8) ? rootHistogram[n] : 0;
    }

    // Wie oft die Wurzelzahl von einem Aufruf zum naechsten wechselt.
    //
    // Ein sauberer Ueberflug hat genau zwei Wechsel: 1 -> 3, wenn der Kegel den
    // Hoerer erreicht, und 3 -> 1, wenn er ihn wieder verlaesst. Alles darueber
    // ist Flackern, und jedes Flackern kostet ein Zweigpaar samt Huellkurve und
    // Filter.
    std::uint64_t rootCountFlips() const { return countFlips; }

    // Wie oft zwei NACHGEFUEHRTE Zweige auf derselben Wurzel gelandet sind.
    //
    // Beim Zusammenfassen gleicher Wurzeln gewinnt die zuerst eingetragene
    // Identitaet, die zweite faellt weg. Zwei Zweige werden damit zu einem, die
    // Wurzelzahl sinkt, und beim naechsten Vollscan entsteht das Paar mit neuen
    // Identitaeten wieder. Das waere das Flackern.
    std::uint64_t collapsedTrackCount() const { return collapsedTracks; }

    std::uint64_t tightPairCount() const { return tightPairs; }   // Abstand < 1 ms
    std::uint64_t adjacentPairCount() const { return adjacentPairs; }

    // Zähler für verworfene Wurzeln über K hinaus (Plan 2.6: "Ein Überlauf
    // wird verworfen und im Debug-Build gezählt").
    int  droppedRoots() const { return droppedRootCount; }
    void clearDroppedRoots() { droppedRootCount = 0; }

    // Anzahl der Auswertungen des Residuums F seit dem letzten Nullsetzen.
    // F auszuwerten ist die mit Abstand teuerste und häufigste Einzeloperation
    // des Lösers (eine Catmull-Rom-Interpolation plus Wurzel), die Zahl ist
    // deshalb ein maschinenunabhängiges Maß für seine Last - anders als eine
    // Wanduhrmessung, die auf einem beschäftigten Rechner um Faktor zwei
    // schwankt und Regressionen darin verschwinden lässt (load_check).
    std::uint64_t residualEvaluations() const { return evalCount; }
    void clearResidualEvaluations() { evalCount = 0; }

private:
    // Höchstzahl gleichzeitig gesammelter Vorzeichenwechsel. Bewusst größer
    // als maxBranches, damit der Scan erst zählt und dann verwirft, statt
    // mitten im Fenster abzubrechen.
    static constexpr int scanCapacity = 16;

    struct Branch
    {
        int    id         = -1;
        double t_e        = 0.0;
        double machRadial = 0.0;
        double R          = 0.0;
        double lastT_h    = 0.0;
    };

    Branch branches[maxBranches];
    int    branchCount      = 0;
    int    nextId           = 0;
    int    droppedRootCount = 0;

    std::uint64_t evalCount = 0;

    double minStep = 1.0e-3;   // 1 kHz Trajektorienraster

    // Untergrenze der Schrittweite beim NACHFUEHREN eines bekannten Zweigs.
    //
    // Deutlich feiner als minStep, und das ist kein Feintuning, sondern der
    // Unterschied zwischen zwei verschiedenen Suchen. minStep deckelt den
    // Aufwand des VOLLSCANS, der ein Fenster von bis zu 25 Sekunden abgehen
    // muss; dort waeren feine Schritte unbezahlbar. Das Nachfuehren startet
    // dagegen von einem bereits guten Schaetzwert und ist ohnehin durch budget
    // begrenzt.
    //
    // Mit minStep als Untergrenze konnte das Nachfuehren nichts aufloesen, was
    // feiner als 1 ms liegt: selbst bei perfektem Schaetzwert erzwingt
    // max(|F|/Lip, minStep) einen 1-ms-Sprung, der ueber die Wurzel hinweg
    // setzt - und bei einem Wurzelpaar gleich ueber beide, also ohne
    // Vorzeichenwechsel. Der Zweig galt dann als verschwunden, obwohl er noch
    // da war. Der Loeser laeuft im Ueberschall alle 167 us, die Wurzel bewegt
    // sich also um Bruchteile davon - eine Untergrenze von 1 ms ist dafuer um
    // Groessenordnungen zu grob.
    //
    // Teurer wird es kaum: |F|/Lip unterschreitet diese Grenze nur in
    // unmittelbarer Naehe der Wurzel, und genau dort endet die Suche sofort mit
    // einem Vorzeichenwechsel.
    double trackStep = 1.0e-3 / 64.0;

    // Untergrenze für |1 - M_r| bei der Fortschreibung eines Zweigs, siehe
    // ausführliche Begründung an der Verwendungsstelle. Sichtbare
    // Modellkonstante, kein stiller Deckel: sie bestimmt, wie weit die Suche
    // einer davongelaufenen Wurzel folgen kann.
    static constexpr double denomFloor = 0.05;

    std::uint64_t trackLost  = 0;
    std::uint64_t newIdGiven = 0;
    std::uint64_t newIdNear  = 0;
    std::uint64_t orderMatched = 0;
    std::uint64_t rootHistogram[8] {};
    std::uint64_t countFlips  = 0;
    std::uint64_t collapsedTracks = 0;
    int           lastCandCount = -1;
    std::uint64_t tightPairs    = 0;
    std::uint64_t adjacentPairs = 0;
};
