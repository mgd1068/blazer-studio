# Stabilisierung 0.8

Version 0.8 bereitet eine spätere, klar begrenzte Beta vor. Sie umfasst Hintergrundberechnung, Abbruch, Schutz gespeicherter Projekte und zusätzliche Fehlerfalltests. Die reale Material-/Emissionsabnahme und ein fertiges Downloadpaket bleiben Voraussetzungen der geplanten 0.9-Beta.

## Aufträge im Hintergrund

Die normale Anwendung berechnet Werkzeugwege, Rasterbilder und die zugehörigen GCode-Zeilen in einem eigenen Threadpool. Der Arbeitsbereich verwendet das aufbereitete Bild aus diesem Ergebnis und berechnet es nicht bei jedem Neuzeichnen erneut. Während der Vorbereitung kann dort vorübergehend das Originalbild erscheinen.

Ein Auftrag erhält einen unveränderlichen Projektstand. Projektänderungen und Blattwechsel entwerten das bisherige Ergebnis sofort; Start und GCode-Ausgabe verwenden ausschließlich ein fertiges Ergebnis des aktuellen Stands. Ein bereits gestarteter Maschinenauftrag behält wie bisher seinen übergebenen Stand.

Der Dienst hält höchstens einen laufenden und einen neuesten wartenden Berechnungsstand. Mehrere schnelle Änderungen werden zusammengefasst. Veraltete Ergebnisse werden verworfen. SVG-Snapshots übertragen die Quelldaten und keine im GUI erzeugten Renderer-Objekte. Qt beschreibt diese Trennung eigener Instanzen und Wertkopien in seiner [Dokumentation zur Thread-Unterstützung](https://doc.qt.io/qt-6.10/threads-modules.html).

**Berechnung abbrechen** beendet die Vorbereitung kooperativ und lässt das Projekt erhalten. **Neu berechnen** verwendet den dann aktuellen Stand. Die Berechnung startet keinen Laser. Der **Stop**-Knopf bleibt die separate Geräteaktion. Bei Berechnungsfehlern zeigt die Oberfläche die Ursache und bietet einen neuen Versuch an.

Der Abbruch verwendet ein eigenes atomisches Signal mit Prüfpunkten in Rasterzeilen, Schichten, Geometriephasen und GCode-Erzeugung. Ein bloßes `QFuture::cancel()` würde den verwendeten einfachen `QtConcurrent::run`-Aufruf nicht anhalten; siehe [Qt Concurrent](https://doc.qt.io/qt-6/qtconcurrentrun.html). Einzelne Qt-Geometrie-/SVG-Aufrufe sind nicht während ihrer internen Ausführung unterbrechbar.

Explizite Offline-Aufrufe und die bestehenden Rechenprüfungen können weiter synchron berechnen. `--export-gcode` verwendet diesen Offline-Modus; die normale grafische Anwendung verwendet die Hintergrundberechnung.

## CSV-Stapel

CSV-Aufträge werden aus einem eingefrorenen Projektstand im Hintergrund berechnet, ohne das geöffnete Dokument vorübergehend umzubauen. Alle Dateien werden zunächst vollständig in einem temporären Unterverzeichnis des Ziels vorbereitet. Die Veröffentlichung verwendet unter Linux ein atomisches Umbenennen ohne Überschreiben. Auch eine erst nach der Vorprüfung erschienene Zieldatei bleibt erhalten.

Bei regulärem Abbruch oder Fehler werden bereits veröffentlichte Dateien dieses Laufs zurückgenommen. Ein Prozessabsturz während der Veröffentlichung kann einen vollständigen Teilstapel und ein temporäres Verzeichnis hinterlassen; eine dateiübergreifende Absturztransaktion wird nicht behauptet. Vorhandene Dateien werden beim nächsten Versuch nicht überschrieben. Der Stapel bleibt auf 64 MiB begrenzt. Es erfolgt keine automatische Maschinenausführung.

## Projekte und Wiederherstellung

Projektdatei und Backup werden jeweils über temporäre Dateien und atomaren Austausch geschrieben. Ein fehlgeschlagenes Speichern lässt die bisherige Projektdatei bestehen und markiert das Dokument nicht als gespeichert. Eine Dateisperre verhindert gleichzeitiges Speichern desselben Zielpfads durch kooperierende Instanzen.

Das Autosave-Verzeichnis erhält eine Sitzungssperre. Eine zweite 0.8-Instanz mit denselben Anwendungsdaten startet den Editor und die Geräteanbindung nicht; sie zeigt einen Hinweis. Nach einem Prozessabsturz erkennt Qt die Sperre des beendeten Prozesses als verwaist. Ältere Versionen ohne diese Sperre müssen beim Umstieg geschlossen werden.

Ist die neueste automatische Sicherung beschädigt, versucht die Wiederherstellung die vorherige Kopie. Nach dieser Rückkehr wird die gute ältere Kopie beim nächsten Speichern nicht durch die beschädigte Datei ersetzt. Fehlgeschlagenes Öffnen oder Wiederherstellen verändert das aktuelle Projekt nicht.

Das Projektformat bleibt **7**; die Programmausgabe lautet **0.8.0**. Die Dateigrößen- und Jobgrenzen aus [Rasterqualität](RASTER_QUALITY.md) bleiben erhalten.

## Prüfung

Die neue Stabilitätsprüfung deckt ab:

- identische Wege/GCode zwischen synchronem und asynchronem Aufbau sowie unabhängig bleibende Snapshots;
- bedienbare Ereignisschleife, Abbruch, Verwerfen veralteter Generationen, Fehler im Worker und Beenden während einer Berechnung;
- Blattwechsel, Undo und gesperrten Start/Export bei ausstehendem Ergebnis;
- reale 2048-Pixel-Rasterberechnung und SVG-Verarbeitung im Hintergrund;
- Datei-Sperren, einen vom Betriebssystem erzwungenen Schreibfehler über Dateigrößenquota und unveränderte vorhandene Dateien;
- konkurrierende Recovery-Instanzen, beschädigtes Autosave mit Rückfall und Wiederherstellung nach einem tatsächlich getöteten Hilfsprozess;
- abgewiesene beschädigte Projekte und unveränderten Live-Projektstand beim CSV-Export.

Der UI-Test bedient zusätzlich den wirklichen Abbruch-/Neuberechnen-Knopf während einer großen Rasterberechnung. Die bisherigen acht Testprogramme bleiben erhalten; zusammen sind es neun CTest-Programme.

`tools/ci-linux.sh` ist der gemeinsame Einstieg für lokale Container und GitHub Actions. Die Workflow-Matrix baut und prüft mit sowie ohne AddressSanitizer/UndefinedBehaviorSanitizer. Die Testumgebung verwendet Ubuntu 26.04 im Container, Softwaredarstellung und explizite QML-/Python-/Schriftabhängigkeiten. Leckprüfung ist dabei deaktiviert (`detect_leaks=0`); daraus folgt keine Leckfreiheitszusage. Einzelheiten zum tatsächlich abgeschlossenen Lauf stehen im [Prüfbericht](QA_REPORT.md).

## Weitere Schritte zur 0.9

Reale Gravur-, Schnitt- und allgemeine Reliefaufträge sowie Abschalt-/Verbindungsfälle am vorbereiteten M3, installierbares Downloadpaket, externe Testpersonen und dokumentierte Gerätefreigabe bleiben offen. Import/Dekodierung, Projektserialisierung/-schreiben und einzelne Advanced-Editorbefehle laufen weiterhin synchron und benötigen weitere Messungen. Die 512-Pixel-Rasterung importierter STL-/OBJ-Modelle bleibt eine ausgewiesene Einschränkung. Kamera, Rotary und zusätzliche Achsen erhalten durch 0.8 keine neue Hardwarefreigabe.
