# Paketbau – Version aus cmake/Version.cmake

Ziel: Ubuntu 26.04 amd64. Das DEB enthält die Anwendung, eigene Ressourcen, Hilfsprogramme, vollständige Dokumentation mit Abbildungen und Lizenztexte. Qt, Schriften und andere Laufzeitbibliotheken werden von Ubuntu installiert; keine Qt-Bibliotheken oder KI-Modelle sind gebündelt. Das Paket verlangt weder root zum Programmstart noch einen Compiler.

## Bauen

Aus einem **sauber committeten** Checkout:

```bash
docker build -f packaging/builder.Dockerfile -t blazer-builder .
mkdir -p dist
docker run --rm -v "$PWD:/source:ro" -v "$PWD/dist:/output" blazer-builder \
  bash -c 'git config --global --add safe.directory /source; bash /source/tools/package-linux.sh /output'
```

Das Skript baut aus einem temporär ausgepackten `git archive` desselben Commits. `SOURCE_REVISION` bewahrt die genaue Revision auch ohne `.git`. Der Compilerlauf verwendet ausschließlich diesen Quellstand. Das Quellarchiv enthält die Anwendungslizenz und Drittanbieterhinweise; Drittbibliotheken verbleiben unter ihren eigenen Lizenzen. Ein schmutziger Arbeitsbaum wird abgelehnt. Ein neues Ausgabeverzeichnis für einen neuen Kandidaten verwenden.

Ausgabe: `.deb`, passendes `-source.tar.gz`, `BUILD-MANIFEST.json`, Anleitung, Grenzen, Testprotokoll und `SHA256SUMS`. Der Manifest nennt Quellrevision, Compiler, Qt und Paketabhängigkeiten. Hardware- und externe Bedienungsabnahme stehen ausdrücklich auf `pending`. Die Versionsnummer steht zentral in `cmake/Version.cmake`. Jeder Build ermittelt die aktuelle Git-Revision samt Dirty-Kennzeichen neu; ein Git-Worktree wird ebenso erkannt wie ein Quellarchiv mit `SOURCE_REVISION`. `run.sh` baut vor jedem Start und startet bei Buildfehler keine vorhandene alte Binärdatei.

Der Download enthält `docs/`, `assets/` und `LICENSES/` in derselben Struktur wie die installierte Dokumentation. Die drei Einstiegstexte im Wurzelverzeichnis verweisen auf die vollständigen Anleitungen unter `docs/beta/`. Prüfsummen erfassen rekursiv sämtliche ausgelieferten Dateien. Lokale Dokumentationslinks werden im Quellbaum, Download und installierten Paket geprüft.

## Unabhängige Laufzeitprüfung

**Nur im wegwerfbaren Container ausführen:** Das Skript installiert und entfernt Pakete und legt einen Testbenutzer an.

```bash
docker run --rm -e BLAZER_DISPOSABLE_CONTAINER=1 \
  -v "$PWD/dist:/packages:ro" \
  -v "$PWD/tools/test-installed-package.sh:/test-package.sh:ro" \
  ubuntu:26.04 bash /test-package.sh /packages
```

Mit `-e BLAZER_PREVIOUS_PACKAGE=/previous/ALTES_PAKET.deb` und einem zusätzlichen schreibgeschützten Mount lässt sich zuvor ein älterer Kandidat installieren und der Paketupgradepfad prüfen.

Das Prüfskript aus den zur Paketrevision gehörenden Quellen verwenden; neuere Skripte prüfen auch später hinzugekommene Funktionen.

Es werden Prüfsummen, exakte Übereinstimmung von Programmversion/Quellrevision mit dem Manifest, Installation auf einer frischen Laufzeitbasis, Lizenz-/Menü-/MIME-/Symboldateien, Nicht-root-Start, PNG/JPEG/SVG, Schriften, Projekt-/Vorlagenrundlauf, Vorlagen-Dateityp, Hintergrundplanung, Fensterdarstellung, GCode-Export, Neuinstallation und Entfernung geprüft. Compiler, CMake und Qt-Entwicklungspakete dürfen nicht installiert sein. Ein eigenes Format-7-Rechteck, eine Format-8-Vorlage und tatsächliche Benutzereinstellungen müssen bytegleich erhalten bleiben. Projekt und Vorlage werden gegen einen unabhängig festgelegten GCode geprüft, nach Neuinstallation erneut geprüft und nach Entfernung auf Erhalt kontrolliert. Bei gesetztem `BLAZER_PREVIOUS_PACKAGE` wird das Projekt bereits mit dem alten Programm gegen denselben Sollwert exportiert. Diese Prüfung ergänzt den Selbsttest und dessen temporäre Projekte. `--self-test` bleibt im Simulator und öffnet keine serielle Verbindung. Die Qt-Darstellung wird offscreen mit Software-Backend geprüft; das ersetzt keine Prüfung eines echten Wayland-/X11-Desktops, HiDPI oder eines menschlichen Einsteigers.

GitHub Actions führt die normalen und ASan/UBSan-Regressionsprüfungen aus. Eine zweite Pipeline baut und prüft das Paket und stellt die Dateien als Actions-Artefakt bereit. Ein Release bleibt zunächst Entwurf; eine öffentliche Hardware-Beta verlangt tatsächliche Hardware- und Einsteigerergebnisse gemäß Testprotokoll. Ein erfolgreicher Pakettest ist keine Materialfreigabe.
