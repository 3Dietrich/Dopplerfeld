# Test-Fixtures

Dateien, die die Tests zum Laufen brauchen und die deshalb im Repo liegen -
im Gegensatz zu `presets/test/`, das Arbeitsmaterial aus Hoerdurchgaengen ist
und per `.gitignore` lokal bleibt.

Was ein Test oeffnet, gehoert hierher. Sonst ist er auf dem eigenen Rechner
gruen und auf einem frischen Klon rot - genau daran ist die CI vom 22. bis
28.08.2026 bei jedem Push gescheitert.

Unter `Tests/` liegen sie ausserdem ausserhalb von `presets/`, das der
Release-Schritt in das Nutzer-Zip kopiert.

| Datei | gebraucht von |
| --- | --- |
| `mach2.5 vorbeiflug` | `load_check`: EIN Vorbeiflug = EIN Knall (der Zweitknall aus @dpas Aufnahme), und der Klappzustand der Panelspalte bei einem Preset ohne Maske |
| `peitschentest` | `load_check`: die Knall-Sperre nimmt Doppelhiebe weg (@dpa 20260830, die zwei Fronten eines Ueberschall-Wacklers ueber verschiedene Hoerwege); von Hand auch `whip_probe` |
