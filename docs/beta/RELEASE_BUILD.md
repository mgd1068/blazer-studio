# Paketbau 0.9.0-beta.1

Ziel: Ubuntu 26.04 amd64. Das DEB enthält die Anwendung, eigene Ressourcen, Hilfsprogramme und Lizenztexte. Qt, Schriften und andere Laufzeitbibliotheken werden von Ubuntu installiert; keine Qt-Bibliotheken oder KI-Modelle sind gebündelt. Das Paket verlangt weder root zum Programmstart noch einen Compiler.

## Bauen

Aus einem **sauber committeten** Checkout:

```bash
docker build -f packaging/builder.Dockerfile -t blazer-builder .
mkdir -p dist
docker run --rm -v "$PWD:/source:ro" -v "$PWD/dist:/output" blazer-builder \
  bash -c 'git config --global --add safe.directory /source; bash /source/tools/package-linux.sh /output'
```

Das Skript baut aus einem temporär ausgepackten `git archive` desselben Commits. `SOURCE_REVISION` bewahrt die genaue Revision auch ohne `.git`. Der Compilerlauf verwendet ausschließlich diesen Quellstand. Das Quellarchiv enthält die Anwendungslizenz und Drittanbieterhinweise; Drittbibliotheken verbleiben unter ihren eigenen Lizenzen. Ein schmutziger Arbeitsbaum wird abgelehnt. Ein neues Ausgabeverzeichnis für einen neuen Kandidaten verwenden.

Ausgabe: `.deb`, passendes `-source.tar.gz`, `BUILD-MANIFEST.json`, Anleitung, Grenzen, Testprotokoll und `SHA256SUMS`. Der Manifest nennt Quellrevision, Compiler, Qt und Paketabhängigkeiten. Hardware- und externe Bedienungsabnahme stehen ausdrücklich auf `pending`.

## Unabhängige Laufzeitprüfung

**Nur im wegwerfbaren Container ausführen:** Das Skript installiert und entfernt Pakete und legt einen Testbenutzer an.

```bash
docker run --rm -e BLAZER_DISPOSABLE_CONTAINER=1 \
  -v "$PWD/dist:/packages:ro" \
  -v "$PWD/tools/test-installed-package.sh:/test-package.sh:ro" \
  ubuntu:26.04 bash /test-package.sh /packages
```

Es werden Prüfsummen, Installation auf einer frischen Laufzeitbasis, Lizenz-/Menü-/MIME-/Symboldateien, Nicht-root-Start, PNG/JPEG/SVG, Schriften, Projektrundlauf, Hintergrundplanung, Fensterdarstellung, GCode-Export, Neuinstallation und Entfernung geprüft. Compiler, CMake und Qt-Entwicklungspakete dürfen nicht installiert sein. Benutzerdaten müssen erhalten bleiben. `--self-test` bleibt im Simulator und öffnet keine serielle Verbindung. Die Qt-Darstellung wird offscreen mit Software-Backend geprüft; das ersetzt keine Prüfung eines echten Wayland-/X11-Desktops, HiDPI oder eines menschlichen Einsteigers.

GitHub Actions führt die normalen und ASan/UBSan-Regressionsprüfungen aus. Eine zweite Pipeline baut und prüft das Paket und stellt die Dateien als Actions-Artefakt bereit. Ein Release bleibt zunächst Entwurf; eine öffentliche Hardware-Beta verlangt tatsächliche Hardware- und Einsteigerergebnisse gemäß Testprotokoll. Ein erfolgreicher Pakettest ist keine Materialfreigabe.
