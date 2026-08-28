#!/usr/bin/env python3
"""Oeffnen die Tests nur Dateien, die auch im Repo liegen?

Ein Test, der eine Datei anfasst, die es nur auf dem eigenen Rechner gibt,
ist hier gruen und auf einem frischen Klon rot - und rot wird er erst
draussen, bei GitHub, als Mail. Genau daran ist die CI vom 22. bis
28.08.2026 bei jedem Push gescheitert: load_check oeffnete ein Preset aus
presets/test, und dieser Ordner bleibt per .gitignore lokal.

Diese Pruefung holt den Befund nach vorne: sie liest die Quellen der
ctest-Tests, sammelt jeden Pfad, den sie ueber DOPPLERFELD_SOURCE_DIR
oeffnen, und vergleicht ihn mit dem, was git tatsaechlich fuehrt. Sie
laeuft in Sekunden und braucht keinen Build.

Nur die Tests, die in ctest stehen (siehe add_test in CMakeLists.txt).
Die uebrigen Werkzeuge unter Tests/ sind Diagnose von Hand und duerfen auf
Arbeitsmaterial in presets/test zeigen.
"""

import codecs
import os
import re
import subprocess
import sys

# Die Quellen der Tests aus add_test - wer hier dazukommt, gehoert dazu.
CTEST_SOURCES = [
    "Tests/load_check.cpp",
    "Tests/solver_check.cpp",
]

# DOPPLERFELD_SOURCE_DIR, gefolgt von einem oder mehreren aneinander
# gereihten String-Literalen (der Praeprozessor klebt sie zusammen; im Code
# stehen sie oft ueber zwei Zeilen verteilt).
PATTERN = re.compile(r'DOPPLERFELD_SOURCE_DIR\s*((?:"(?:[^"\\]|\\.)*"\s*)+)')
LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')


def decode_literal(raw):
    """Loest die C-Escapes auf. '\\xc2\\xb2' sind die zwei UTF-8-Bytes von '²'."""
    as_chars = codecs.decode(raw, "unicode_escape")
    return as_chars.encode("latin-1").decode("utf-8", errors="replace")


def tracked_files(repo):
    out = subprocess.run(["git", "-C", repo, "ls-files", "-z"],
                         capture_output=True)

    if out.returncode != 0:
        return None

    return set(p.decode("utf-8", errors="replace")
               for p in out.stdout.split(b"\0") if p)


def main():
    repo = sys.argv[1] if len(sys.argv) > 1 else os.getcwd()

    tracked = tracked_files(repo)

    if tracked is None:
        # Ausgepackter Tarball statt Klon: hier ist nichts zu vergleichen.
        print("Repo-Pruefung         uebersprungen (kein Git-Repo)")
        return 0

    findings = []
    checked = 0

    for source in CTEST_SOURCES:
        path = os.path.join(repo, source)

        if not os.path.exists(path):
            findings.append((source, source, "Quelldatei fehlt"))
            continue

        text = open(path, encoding="utf-8", errors="replace").read()

        for match in PATTERN.finditer(text):
            joined = "".join(decode_literal(lit)
                             for lit in LITERAL.findall(match.group(1)))
            relative = joined.lstrip("/")

            if not relative:
                continue

            checked += 1
            line = text.count("\n", 0, match.start()) + 1

            if relative not in tracked:
                where = "%s:%d" % (source, line)

                if os.path.exists(os.path.join(repo, relative)):
                    # Der gefaehrliche Fall: hier da, im Klon nicht.
                    findings.append((where, relative,
                                     "liegt nur lokal, git fuehrt sie nicht"))
                else:
                    findings.append((where, relative, "gibt es gar nicht"))

    print("Repo-Pruefung          %d Pfade aus %d Testquellen, %d beanstandet"
          % (checked, len(CTEST_SOURCES), len(findings)))

    if not findings:
        return 0

    for where, path, reason in findings:
        print("FEHLGESCHLAGEN: %s oeffnet \"%s\" - %s" % (where, path, reason))

    print("\nWas ein Test oeffnet, gehoert ins Repo. Test-Fixtures nach")
    print("Tests/fixtures legen und dort einchecken (siehe Tests/fixtures/README.md);")
    print("presets/test ist Arbeitsmaterial und bleibt ignoriert.")

    return 1


if __name__ == "__main__":
    sys.exit(main())
