# Blazer Studio – Software-Testkandidat

Getestete Paketplattform: **Ubuntu 26.04, x86_64**. Andere Linux-Versionen sind noch nicht als Paketplattform bestätigt. Es handelt sich um einen Software-Testkandidaten; Geräte-/Materialabnahme und externe Bedienungstests sind offen.

Bei jedem normalen Programmstart erscheint ein zweisprachiger Sicherheitshinweis, auch nach abgeschlossener Einführung und vor einer Wiederherstellungsentscheidung. **Laser niemals unbeaufsichtigt betreiben / Never operate the laser unattended.** Ein Verbindungsabbruch oder hängender Controller kann den Laser eingeschaltet lassen; Software kann dann kein Laser-Aus garantieren. Während der gesamten Bearbeitung am Gerät bleiben und bereit sein, die Stromversorgung direkt am Gerät zu unterbrechen. Mit **Verstanden / I understand** bestätigen; eine dauerhafte Abschaltung dieses Hinweises gibt es nicht. Diese Ergänzung gehört zum beta.5-Kandidaten und noch nicht zum unveränderten beta.4-Paket.

## Installation

Das Debian-Paket und die passende Quellcode-Datei gehören zum selben Kandidaten. Prüfsummen stehen in `SHA256SUMS`.

```bash
sha256sum -c SHA256SUMS
sudo apt install ./blazer-studio_*_amd64.deb
```

Die Paketverwaltung installiert die benötigten Laufzeitbibliotheken. Ein Compiler oder Qt-Entwicklungsumgebung ist nicht erforderlich. Starte anschließend **Blazer Studio** im Anwendungsmenü oder `blazer-studio` im Terminal. Beim Wechsel von einer alten Entwicklungsversion das alte Fenster regulär schließen; Benutzerdateien werden beim Installieren nicht gelöscht.

## Erster Auftrag im Simulator

1. Die Einführung lesen und das Übungsprojekt im Simulator öffnen.
2. Ein Objekt auswählen, seine Größe und Lage ändern und die Hervorhebung auf der Fläche prüfen.
3. Laseraufsatz und Material wählen. Die Werte stammen aus Profilen; eigene Materialversuche sind separat erforderlich.
4. Vorschau öffnen und **Am Bildschirm testen** wählen. Dies verwendet den Simulator.
5. Das Projekt als `.blazer` speichern, schließen und wieder öffnen.

Der Hintergrundauftrag lässt sich abbrechen und neu berechnen. **Berechnung abbrechen** und **Laser-Stop** sind unterschiedliche Aktionen.

Über **Projektname → Projektangaben** lassen sich Name und Notizen bearbeiten. Dasselbe Menü bietet eigene Vorlagen an; **Zuletzt** öffnet den lokalen Projektverlauf. Siehe [Projekte und Vorlagen](../PROJECTS.md). Format-8-Dateien mit Notizen benötigen mindestens beta.3 und lassen sich mit beta.2 nicht öffnen. Beta.4 ergänzt je nach verwendeten Daten die Formate 9–11; beta.3 kann diese nicht öffnen. Details und Grenzen: [Änderungen zu beta.4](CHANGES_BETA4.md).

## Hilfe und Fehlerberichte

Unter **Hilfe / Beta** findest du Versionsdaten, Einschränkungen und die Einführung. **Diagnosebericht speichern** erstellt lokal eine JSON-Datei mit ausgewählten technischen Daten. Projekte, Bilder, Dateipfade, Materialnotizen und rohe Geräteprotokolle sind nicht enthalten. Du kannst die Datei ansehen und selbst an einen GitHub-Fehlerbericht anhängen. Es erfolgt kein automatischer Versand.

Für einen hilfreichen Bericht: genaue Version, Schritte, erwartetes und tatsächliches Ergebnis sowie eine kleine unkritische Beispieldatei angeben. Das [Testprotokoll](TEST_PROTOCOL.md) hilft beim strukturierten Test.

## Geräteanschluss

Der Simulator bleibt Standard. Ein echter Anschluss erfolgt ausdrücklich im Gerätebereich. USB-Gerätezugriff muss für den Benutzer erlaubt sein; bei Berechtigungsproblemen die Zugriffsrechte beziehungsweise Mitgliedschaft in der Ubuntu-Gruppe `dialout` mit dem Systemadministrator prüfen und anschließend neu anmelden. Die Anwendung verlangt keine Ausführung als root.

Eine geschlossene Einhausung und laufende geeignete Absaugung gehören zur realen Prüfung. Die gerätespezifische Lüftersteuerung über M8 ist nicht nachgewiesen; M8 wird deshalb nicht als funktionierender Lüfterknopf angeboten. Bei den beaufsichtigten Kraftpapierversuchen waren Gehäuse und externe Absaugung betriebsbereit. Materialwirkung ist bestätigt, die vollständige Betriebs-, Schnitt- und Reliefabnahme bleibt offen. Details: [Geräte und Materialabnahme](../HARDWARE_ACCEPTANCE.md).

Eine USB-Verbindung über einen umschaltenden KVM kann beim Wechsel zum anderen PC verloren gehen. Der Auftrag wird bei Kommunikationsverlust verworfen. Nach erneutem Anschließen Konfiguration, Position und Materiallage neu prüfen; eine alte Ausgabe wird nicht automatisch wieder aufgenommen.

## Optionale Komponenten

PDF/AI/EPS-Konvertierung benötigt das separat installierte Inkscape. Lokale KI-Fototiefe benötigt einen zusätzlichen, ausdrücklich heruntergeladenen Laufzeit-/Modell-Cache; Grundfunktionen und Paketstart benötigen ihn nicht:

```bash
sudo apt install python3-venv
python3 /usr/share/blazer-studio/tools/setup_depth.py
```

Die anschließende Tiefenschätzung läuft lokal. Sie erzeugt eine relative Tiefenkarte, keine vermessene Oberflächengeometrie.

## Aktualisieren und entfernen

Ein neueres Paket mit `sudo apt install ./DATEINAME.deb` installieren. Entfernen: `sudo apt remove blazer-studio`. Eigene Projekte und Benutzerdaten bleiben getrennt von den installierten Programmdateien. Die Software ist Source Available mit kostenloser Weitergabe; gewerbliche Laserarbeiten sind erlaubt, Verkauf der Software ist ausgeschlossen. Siehe die mitgelieferte Lizenz.
