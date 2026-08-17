#pragma once

#include "../Physics/Vec3.h"

// Zwei geradlinige Vorbeiflug-Bahnen als Bewegungsquelle (Backlog-Punkt
// "zwei neue Bewegungsgeneratoren"):
//
//   ThroughScreen - die Quelle fliegt in die Tiefe, also entlang der y-Achse
//                   ("durch den Bildschirm"), seitlich um n Meter am Hörer
//                   vorbei.
//   Crossing      - die Quelle quert waagerecht, also entlang der x-Achse, in
//                   n Metern Abstand vor bzw. hinter dem Hörer.
//
// Der Generator liefert nur Zielpositionen, genau wie Maus, Hostautomation
// und MotionPlayer. Er ist damit kein Sonderzustand: geglättet, in die
// Trajektorie geschrieben und gelöst wird danach wie immer.
//
// Die Geschwindigkeit ist ausdrücklich live veränderbar - die Bahn integriert
// den jeweils aktuellen Wert, statt eine feste Dauer vorauszuberechnen. Ein
// Automationsverlauf auf dem Geschwindigkeitsregler beschleunigt die Quelle
// deshalb wirklich, statt den Flug zu verwerfen und neu zu planen.
//
// Reines C++, keine JUCE-Abhängigkeit (wie der Rest von Source/Motion).
class FlyByGenerator
{
public:
    enum class Kind
    {
        ThroughScreen,   // entlang +y, seitlich versetzt
        Crossing         // entlang +x, in der Tiefe versetzt
    };

    // Wie der Flug anfängt. Der Unterschied liegt NICHT in dieser Klasse -
    // sie liefert in beiden Fällen dieselbe Bahn - sondern darin, womit der
    // Aufrufer die Vorgeschichte füllt:
    //
    //   Continuous - die Trajektorie wird rückwärts mit genau dieser
    //                geradlinigen Bewegung vorbelegt. Der Löser sieht dann
    //                eine Quelle, die schon immer geflogen ist; es gibt weder
    //                einen Positions- noch einen Geschwindigkeitssprung.
    //   Abrupt     - die Vorgeschichte bleibt konstant (die Quelle ruhte am
    //                Startpunkt) und die Bewegung setzt schlagartig ein. Das
    //                ist bewusst unphysikalisch und als reproduzierbarer
    //                Testfall für den Überschallknall gedacht.
    enum class Start
    {
        Continuous,
        Abrupt
    };

    // listenerPos legt die Bezugsachse fest, heightMetres die Flughöhe (die
    // kommt vom vorhandenen Source-Z-Regler, der Generator erfindet keine
    // zweite Höhe). distanceMetres ist der Abstand, in dem die Bahn am Hörer
    // vorbeiläuft - senkrecht zur Flugrichtung. approachMetres ist davon
    // unabhängig die Anflug-/Abflugstrecke (halbe Bahnlänge, siehe
    // halfLength()) - zwei verschiedene Größen, die sich nicht in einen
    // Regler verschmelzen lassen, ohne dass einer von beiden ungewollt
    // mitläuft.
    void configure (Kind kind, Start start, double distanceMetres, double approachMetres,
                    Vec3 listenerPos, double heightMetres);

    // Setzt den Flug an den Anfangspunkt und startet ihn.
    void start();
    void stop() { running = false; }
    bool isRunning() const { return running; }

    Start startMode() const { return startVariant; }

    // Live veränderbar, in m/s. Negative Werte drehen die Flugrichtung um.
    void setSpeed (double metresPerSecond) { speed = metresPerSecond; }

    // Anfangspunkt und Anfangsgeschwindigkeit - beides braucht der Aufrufer,
    // um die Vorgeschichte passend zu füllen (siehe Start).
    Vec3 startPosition() const;
    Vec3 startVelocity() const;

    // Ein Schritt um dt Sekunden. Liefert die Zielposition; nach dem Ende der
    // Strecke bleibt sie am Endpunkt stehen und isRunning() wird false.
    Vec3 tick (double dt);

    // Aktuelle Zielposition, ohne die Bahn weiterzudrehen.
    Vec3 currentPosition() const { return positionAt (travelled); }

    // Ende der geplanten Bahn (@dpa: "voraussichtlichen Weg einzeichnen") -
    // für die Restanzeige zusammen mit currentPosition().
    Vec3 plannedEnd() const { return positionAt (2.0 * halfLength()); }

    // Der Punkt auf der GESAMTEN Bahn mit dem kürzesten Abstand zum Hörer,
    // geschlossene Formel statt Suche: die Bahn ist gerade und liegt per
    // Konstruktion in konstantem Versatz zum Hörer (siehe positionAt()) -
    // der naechste Punkt ist exakt die Mitte der Strecke, auf Hoererhoehe in
    // Flugrichtung. @dpa: "jeweiligen kuerzsten Abstand des gesamten laufs".
    Vec3   nearestPoint() const { return positionAt (halfLength()); }
    double nearestDistanceMetres() const { return (nearestPoint() - listener).length(); }

private:
    // Einheitsvektor der Flugrichtung.
    Vec3 direction() const;

    // Versatzvektor senkrecht dazu, in der Waagerechten.
    Vec3 offset() const;

    // Halbe Streckenlänge = die Anflug-/Abflugstrecke (Params::flyApproach),
    // eigenständig und NICHT aus distance abgeleitet - distance ist allein
    // der seitliche Versatz zu L, siehe offset(). Untergrenze verhindert eine
    // entartete Bahn bei einem sehr klein automatisierten Wert.
    double halfLength() const;

    Vec3 positionAt (double distanceAlongPath) const;

    Kind   kind         = Kind::ThroughScreen;
    Start  startVariant = Start::Continuous;

    Vec3   listener;
    double distance  = 20.0;
    double approach  = 300.0;
    double height    = 0.0;
    double speed     = 50.0;

    // Beim Start aus dem Vorzeichen der Geschwindigkeit festgelegt und danach
    // unveränderlich, siehe direction() in der .cpp.
    double directionSign = 1.0;

    // Zurückgelegte Strecke ab dem Anfangspunkt, in Metern.
    double travelled = 0.0;
    bool   running   = false;
};
