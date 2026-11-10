# Entwurf 0.8 ausprobieren

## Start

```bash
./run.sh
```

Auf Mathias’ Rechner wird zusätzlich ein Startmenüeintrag **Blazer Studio** eingerichtet. Start mit `--dark` öffnet das dunkle Design, `--compact` ein Fenster mit 1180 × 760 Pixeln. Die Anwendung startet im verbundenen Simulator. Eine echte Geräteverbindung wird im Bereich Gerät ausdrücklich ausgewählt. Siehe [GRBL-Workflow](GRBL_WORKFLOW.md).

Die Oberfläche startet in Basic. Oben wechselt **Basic → Advanced** zur vollständigen Ansicht. [Geführter Einstieg, automatische Profilwerte und Tooltips](BEGINNER.md).

## Bedienbarer Umfang

- Bereiche Vorbereiten, Layoutvorschau und Gerät.
- Arbeitsfläche 200 × 150 mm, Zoom, Einpassen und Verschieben mit mittlerer Maustaste.
- Rechtecke, Ellipsen und Text anlegen; auswählen, ziehen, kopieren, ausblenden und löschen.
- Objekte links einzeln auswählen, sichtbar hervorheben und Namen/Text bearbeiten; Gravieren/Schneiden direkt pro Objekt.
- X/Y, Breite/Höhe und proportionale Prozentskalierung numerisch ändern, auch für Mehrfachauswahl und Gruppen.
- Mehrfachauswahl per Häkchen oder Strg-/Umschalt-Klick, Gruppieren/Auflösen, gemeinsam Ziehen/Platzieren/Kopieren/Löschen; [Bedienung](OBJECTS_GROUPS.md).
- Zwei Bearbeitungen mit Geschwindigkeit, Sollleistung, Durchgängen und Ausgabeschalter.
- Toolkopf-Auswahl mit 5 W als Voreinstellung; Materialliste nach Modulleistung gefiltert.
- Persistente Materialbibliothek mit allen 64 Herstellervorgaben als Profilen: auswählen, bearbeiten, kopieren, anlegen und löschen; Gravurintervall, Modell, Modul, Stärke und Notizen. Profilstand bleibt im Projekt erhalten.
- Einheitliche Millimeterkoordinaten mit Ursprung unten links; Rahmenfahrt um aktive Objektgrenzen.
- Herstellerreferenz mit 64 Materialeinträgen für M3 Pro (5/10/1 W), getrennt von den aktiven Projektwerten; Quelle und offene Modellzuordnung sichtbar.
- Berechnete Schnittkonturen und Gravurfülllinien für Formen, Text und unterstützte SVGs; Vorschau und GCode aus demselben Pfadsatz.
- PNG-/JPEG-/BMP-Import als schwarzweiße Dither-Geometrie; Materialtest-Generator mit eigenen Zellparametern.
- Echte GRBL-/Blaze-X-ESP32-Verbindung mit Konfigurationsprüfung, Status, Jog/Home, Rahmenfahrt, Jobübertragung, Pause und Abbruch.
- SVG maßstäblich als zusammenhängende Grafik importieren. Physische mm-/cm-/inch-Einheiten werden berücksichtigt; Angaben ohne Einheit gelten als CSS-Pixel bei 96 dpi.
- Eigene `.blazer`-Projektdateien mit eingebettetem SVG und Parametern speichern/öffnen; Undo/Redo.
- Simulierte Positionierung, Referenzfahrt, Rahmenfahrt, zeitbasierter Demoablauf, Pause, Stop und Verbindungsfehler.
- 2-%-Fokusfunktion im Simulator per Gedrückthalten; Aus beim Loslassen, Seitenwechsel, Fokusverlust, Fehler oder spätestens nach drei Sekunden.

Strg+S: Speichern, Strg+O: Öffnen, Strg+I: Grafik oder Modell importieren, Strg+Z: Undo, Strg+Umschalt+Z: Redo, Escape: Simulation stoppen und Auswahl aufheben.

## Noch offen

Die Vorschau zeigt berechnete Schnitt- und Füllbewegungen sowie Leerfahrten. Der Simulator spielt sie im 16-Sekunden-Zeitraffer ab. Die Zeitangabe ergänzt eine konservative Beschleunigungsschätzung; Kommunikationspausen und reale Controllerplanung sind noch nicht gemessen. Vektoren werden linearisiert; SVGs bleiben im Editor zusammenhängend. Rasterbilder unterstützen Dithering, Schwellenwert oder variable Graustufenleistung.

Noch offen sind vollständige SVG-/DXF-/PDF-/LightBurn-Kompatibilität und die tatsächliche Laser-/Materialabnahme. Unterstützte Importvarianten, neue Editor-/Produktionsfunktionen und Grenzen stehen im [Ausbaupaket](EXPANSION.md). Undo bleibt auf 50 Schritte begrenzt. Projektformat 6 speichert eigene Zell-/Objektparameter und Tiefenkarten; Formate 1–5 bleiben lesbar. Einzelheiten und konkrete Grenzen stehen im [Geräteworkflow](GRBL_WORKFLOW.md).

SVGs mit DTD/Entities oder Bild-/Dateiverweisen werden in dieser ersten Version abgelehnt. Zwei Basisoperationen Gravur und Schnitt erhalten die Materialprofil-Zuordnung. Weitere benannte Operationen und Mehrfachbearbeitungen können frei ergänzt werden. Details: [Materialbibliothek](MATERIAL_LIBRARY.md).

## Bauen und prüfen

Getestete Umgebung: Ubuntu 26.04, GCC 15, Qt 6.10.2.

```bash
sudo apt-get install qt6-base-dev qt6-declarative-dev qt6-svg-dev qt6-serialport-dev libqrcodegencpp-dev \
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

Der aktuelle Prüfumfang steht im [Prüfbericht](QA_REPORT.md). Reale serielle Verbindung und Bewegung wurden mit Laser-Aus bestätigt; echte Laseremission ist bisher kein Bestandteil der Geräteprüfung.

Der zusätzliche Bereich **04 Relief** erzeugt geschichtete Gravurjobs aus Graustufen-Tiefenkarten. Bedienung, Kalibrierung und Hardwaregrenzen: [Relief](RELIEF.md). CLI: `--new --import-relief /pfad/depth.png --view 3`; kein automatischer Laserstart.

**05 Werkzeuge** bietet Pfade/Transformation, Operationen, Bildbearbeitung und Produktion. [Bedienung, optionale KI und konkrete Grenzen](EXPANSION.md).

Seit 0.8 laufen Job-/Rasterberechnung und CSV-Stapel im Hintergrund. Abbruch und Neuberechnung stehen im Arbeitsbereich zur Verfügung. [Verhalten, Stabilitätsprüfungen und verbleibende Grenzen](STABILIZATION_08.md).
