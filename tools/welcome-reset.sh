#!/bin/bash
# Setzt das Begruessungsfenster zurueck, damit es beim naechsten Start wieder
# erscheint - auch dann, wenn "nicht mehr zeigen" gedrueckt wurde.
#
# Ohne das laesst sich der Ablauf nur einmal testen: der Merker liegt ausserhalb
# des Projekts in den Benutzereinstellungen und ueberlebt jeden Neubau.
set -u

SETTINGS="$HOME/Library/Application Support/Dopplerfeld/Dopplerfeld.settings"

if [ ! -f "$SETTINGS" ]; then
    echo "Kein Merker vorhanden - das Fenster erscheint ohnehin beim naechsten Start."
    echo "(erwartet unter: $SETTINGS)"
    exit 0
fi

echo "Vorher:"
grep -o 'welcomeSeen[^/]*' "$SETTINGS" 2>/dev/null || echo "  kein welcomeSeen-Eintrag"

rm -f "$SETTINGS"

echo "Merker geloescht. Beim naechsten Start ist das Begruessungsfenster wieder da."
echo
echo "Testablauf:"
echo "  1. Dopplerfeld beenden, falls es laeuft (der Merker wird beim Beenden geschrieben)"
echo "  2. dieses Skript ausfuehren"
echo "  3. Dopplerfeld starten -> Fenster erscheint"
echo "  4. OK/Enter/Escape -> Fenster zu, beim naechsten Start wieder da"
echo "  5. 'nicht mehr zeigen' -> weg, bis dieses Skript wieder laeuft"
