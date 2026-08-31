# Dopplerfeld

[![Build & Release](https://github.com/3Dietrich/Dopplerfeld/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/3Dietrich/Dopplerfeld/actions/workflows/build.yml)

Audio-Plugin (AU/VST3/Standalone) für macOS, das den akustischen
Dopplereffekt physikalisch nachstellt – inklusive Überschall und Mach-Kegel.
Eine Schallquelle **M** und ein Hörer **L** (mit Kopf-Orientierung) auf einer
Feldfläche, die eine reale Modellgröße von 1 bis 10.000 Metern darstellt. Der
Ton entsteht aus echter Laufzeitberechnung (retarded time), nicht aus einer
Doppler-Formel als Abkürzung.  

![Dopplerfeld: Feldanzeige mit Wellenfronten, Bewegungsaufzeichnung/-wiedergabe und Vorbeiflug-Generator](docs/screenshot.png)

## Download

Fertig gebaute (Mac) Standalone-App, VST3 und AU liegen unter
[Releases](../../releases) – herunterladen, entpacken, benutzen. Kein Xcode,
kein CMake nötig.

Wenn **macOS meldet beim ersten Start "kann nicht geöffnet werden, da der
Entwickler nicht verifiziert werden kann"** (die Builds sind unsigniert,
kein zahlungspflichtiges Apple-Entwicklerkonto dahinter)  
Abhilfe: im Finder mit Rechtsklick → Öffnen starten (statt Doppelklick), oder im Terminal:

```
xattr -dr com.apple.quarantine /Pfad/zur/Dopplerfeld.app
```

## Features

- Freie Bewegung von Quelle und Hörer per Maus, inklusive Höhe (z-Achse) in
  der perspektivischen Ansicht
- Bodenreflexion und bis zu zwei frei platzierbare Wände (Spiegelquellen),
  wahlweise mit einer zusätzlichen, stabilitätsgeprüften Reflexionsgeneration
- Zwei Vorbeiflug-Generatoren (geradlinig durch die Tiefe / waagerecht
  querend) mit einstellbarem Tempo und zwei Startarten
- Bewegungsaufzeichnung und -wiedergabe, mehrere Glättungsverfahren
- Bis zu 20 Klone der Quelle ("Schrot"-Schwarm), alle mit voller
  Löserphysik gerechnet, mit CPU-Balken und Notaus
- Optionale N-Wellen-Synthese für den Überschallknall, mit einstellbarer
  Sperrzeit gegen Doppelschläge
- Acht Abgriffpunkte im Feld: jeder hört an seiner Stelle - über Direktweg,
  Boden und beide Wände - und schickt das durch einen eigenen Hall (Diffusor,
  Schroeder, FDN oder "Draussen"), wahlweise einer in den nächsten verkettet
- Motor-Generator, Sample-Player und Live-Audioeingang als Klangquelle
- Motor abschaltbar, Fahrtwind und Rauschband bleiben stehen; die Drehzahl kann
  der Beschleunigung der Quelle folgen
- Zustandsstreifen über dem Feld: Presets laden, durchhören und sichern ohne
  Dateidialog; die Klappzustände der Panelspalte reisen im Preset mit
- Beim allerersten Start lädt ein mitgeliefertes Preset, danach wieder der
  zuletzt benutzte Zustand

## Bauen aus dem Quellcode

Für Entwicklung/Debugging, nicht nötig für reine Nutzung (siehe Download
oben).

```
git clone https://github.com/3Dietrich/JUCE.git ~/Documents/JUCE   # oder eigener JUCE-Checkout
git clone <dieses-repo>
cd Dopplerfeld
cmake -B build -DJUCE_DIR=~/Documents/JUCE
cmake --build build --config Release -j 4
cd build && ctest --output-on-failure
```

Fünf Tests: `solver_check` (Physik-Löser), `load_check` (Lasttest offline),
`reverb_check` (Hallbauarten), `scope_boom_probe` (Knall-Ansicht des Scopes)
und `repo_check`.

`-DJUCE_DIR` zeigt auf einen lokalen JUCE-Checkout (Version 8.0.6 getestet);
ohne die Angabe wird `~/Documents/JUCE` angenommen.

`repo_check` braucht keinen Build und prüft, ob die Tests nur Dateien öffnen,
die auch im Repo liegen. Was ein Test öffnet, gehört eingecheckt — sonst ist
er auf dem eigenen Rechner grün und auf einem frischen Klon rot (siehe
`Tests/fixtures/README.md`).

## Warum klingt es anders als erwartet

Nach dem Überschallknall wird es schlagartig sehr viel leiser, und ein Teil des
Klangs läuft zeitverkehrt. Beides ist echte Physik.
[docs/warum-klingt-es-anders.md](docs/warum-klingt-es-anders.md) erklärt, woher
das kommt: Kegelgeometrie, Pegel über acht Sekunden und die Hüllkurve, die den
langen Ausklang trägt.
Dieselbe Erklärung als gesetzte Seite:
[3dietrich.github.io/Dopplerfeld](https://3dietrich.github.io/Dopplerfeld/).

## Architektur

Siehe [ARCHITEKTUR.md](ARCHITEKTUR.md) für das Schichtenmodell, Kernklassen
und den aktuellen Stand. Die Herleitung der Physik - Retarded-Time-Gleichung,
Mach-Kegel, Fokussierungsfaktor, Löser-Algorithmus - steht in
[docs/physik.md](docs/physik.md).

## Lizenz

[GPLv3](LICENSE) – bedingt durch die Lizenz von [JUCE](https://juce.com) in
der kostenlosen Open-Source-Stufe.
