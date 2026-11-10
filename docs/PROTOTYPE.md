# Entwurf 0.2 ausprobieren

## Start

```bash
./run.sh
```

Auf Mathias’ Rechner wird zusätzlich ein Startmenüeintrag **Blazer Studio** eingerichtet. Start mit `--dark` öffnet das dunkle Design, `--compact` ein Fenster mit 1180 × 760 Pixeln. Der Simulator ist standardmäßig verbunden; es werden keine Gerätebefehle gesendet.

## Bedienbarer Umfang

- Bereiche Vorbereiten, Layoutvorschau und Gerät.
- Arbeitsfläche 200 × 150 mm, Zoom, Einpassen und Verschieben mit mittlerer Maustaste.
- Rechtecke, Ellipsen und Text anlegen; auswählen, ziehen, kopieren, ausblenden und löschen.
- Position/Größe numerisch ändern, Textinhalt bearbeiten, Gravur/Schnitt zuweisen.
- Zwei Bearbeitungen mit Geschwindigkeit, Sollleistung, Durchgängen und Ausgabeschalter.
- Toolkopf-Auswahl mit 5 W als Voreinstellung; Materialliste nach Modulleistung gefiltert.
- Persistente Materialbibliothek mit allen 64 Herstellervorgaben als Profilen: auswählen, bearbeiten, kopieren, anlegen und löschen; Gravurintervall, Modell, Modul, Stärke und Notizen. Profilstand bleibt im Projekt erhalten.
- Einheitliche Millimeterkoordinaten mit Ursprung unten links; Rahmenfahrt um aktive Objektgrenzen.
- Herstellerreferenz mit 64 Materialeinträgen für M3 Pro (5/10/1 W), getrennt von den aktiven Projektwerten; Quelle und offene Modellzuordnung sichtbar.
- SVG maßstäblich als zusammenhängende Grafik importieren. Physische mm-/cm-/inch-Einheiten werden berücksichtigt; Angaben ohne Einheit gelten als CSS-Pixel bei 96 dpi.
- Eigene `.blazer`-Projektdateien mit eingebettetem SVG und Parametern speichern/öffnen; Undo/Redo.
- Simulierte Positionierung, Referenzfahrt, Rahmenfahrt, zeitbasierter Demoablauf, Pause, Stop und Verbindungsfehler.
- 2-%-Fokusfunktion im Simulator per Gedrückthalten; Aus beim Loslassen, Seitenwechsel, Fokusverlust, Fehler oder spätestens nach drei Sekunden.

Strg+S: Speichern, Strg+O: Öffnen, Strg+I: SVG importieren, Strg+Z: Undo, Strg+Umschalt+Z: Redo, Escape: Simulation stoppen und Auswahl aufheben.

## Noch offen

Die Vorschau ist eine **Layoutvorschau**: Reihenfolge der Objektliste, Begrenzungen und Verbindungswege. Für importierte SVGs gibt es noch keine Elementzerlegung, für Text und Füllgravur noch keine Laserpfadberechnung. Der Demoablauf ist eine unabhängige Animation und führt das Projekt nicht als Maschinenjob aus. Rahmenfahrt und Positionsanzeigen sind simuliert, keine Live-Telemetrie.

Noch nicht vorhanden sind echte Gerätekommunikation, GCode-Ausgabe, vollständige SVG-Kompatibilität, andere Importformate, mehrere Arbeitsblätter, Autosave oder Wiederherstellung nach einem Absturz. Der vorhandene Undo-Verlauf ist auf 50 Schritte begrenzt und wird nicht mitgespeichert. Die vollständige Roadmap bleibt unverändert das Entwicklungsziel.

SVGs mit DTD/Entities oder Bild-/Dateiverweisen werden in dieser ersten Version abgelehnt. Die zwei Bearbeitungen Gravur und Schnitt sind vorerst fest vorgegeben; ihre Parameter und Ausgabe lassen sich über Profile oder einzeln ändern. Details: [Materialbibliothek](MATERIAL_LIBRARY.md).

## Bauen und prüfen

Getestete Umgebung: Ubuntu 26.04, GCC 15, Qt 6.10.2.

```bash
sudo apt-get install qt6-base-dev qt6-declarative-dev qt6-svg-dev \
  qml6-module-qtquick qml6-module-qtquick-window \
  qml6-module-qtquick-controls qml6-module-qtquick-layouts \
  qml6-module-qtquick-dialogs qml6-module-qtqml-workerscript
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

Die Tests decken Projektzustände/Undo, Speichern und fehlerhaftes Öffnen, SVG-Maße und externe Referenzen, Parametergrenzen, simulierte Fokus-/Jobkonflikte, Zeitlimit und Verbindungsfehler ab. Ein zusätzlicher Qt-Quick-Test bedient die Oberfläche mit Maus/Tastatur und prüft die Fokusfreigabe beim Seitenwechsel.

Für reproduzierbare Ansichten:

```bash
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  ./build/blazer-studio --screenshot /tmp/blazer.png
```

Der aktuelle Prüfumfang steht im [Prüfbericht](QA_REPORT.md). Keine echte Laseremission ist Bestandteil dieser Prüfungen.
