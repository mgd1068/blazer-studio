# Blazer Studio – Einstieg in beta.8-dev

**Beta-Ziel:** ein nutzbarer M3-Grundworkflow vom Entwurf bis zum beaufsichtigten Auftrag. Die vollständige LightBurn-Parität bleibt ein weiteres Entwicklungsziel. beta.8-dev ergänzt eine geführte, nur für die aktuelle Verbindung geltende M3-Einrichtung und einen ausdrücklich bestätigten Absaugmodus ohne Sensor. Diese Softwarefunktionen sind keine bereits erfolgte physische Abnahme deines Geräts. Die veröffentlichte beta.7 und ältere Versuche erhalten dadurch keine nachträgliche Freigabe. Siehe [aktuelle Grenzen](KNOWN_LIMITATIONS.md).

Getestete Paketplattform: **Ubuntu 26.04, x86_64**. Andere Linux-Versionen sind noch nicht als Paketplattform bestätigt. Es handelt sich um einen Software-Testkandidaten; Geräte-/Materialabnahme und externe Bedienungstests sind offen.

Bei jedem normalen Programmstart erscheint ein zweisprachiger Sicherheitshinweis, auch nach abgeschlossener Einführung und vor einer Wiederherstellungsentscheidung. **Laser niemals unbeaufsichtigt betreiben / Never operate the laser unattended.** Ein Verbindungsabbruch oder hängender Controller kann den Laser eingeschaltet lassen; Software kann dann kein Laser-Aus garantieren. Während der gesamten Bearbeitung am Gerät bleiben und bereit sein, die Stromversorgung direkt am Gerät zu unterbrechen. Mit **Verstanden / I understand** bestätigen; eine dauerhafte Abschaltung dieses Hinweises gibt es nicht.

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

Unter **Hilfe / Beta** findest du Versionsdaten, Einschränkungen und die Einführung. **Diagnosebericht speichern** zeigt den vollständigen Inhalt vor dem lokalen Speichern. Die Standarddiagnose enthält keine Projekte, Bilder, Dateipfade, Materialnotizen oder rohe Geräteprotokolle. Nur nach ausdrücklicher Auswahl kommen begrenzte numerische Controllerdaten und eine typisierte Chronologie hinzu. Gespeichert wird genau die angezeigte Momentaufnahme. Du entscheidest anschließend selbst über eine Weitergabe; es erfolgt kein automatischer Versand.

Für einen hilfreichen Bericht: genaue Version, Schritte, erwartetes und tatsächliches Ergebnis sowie eine kleine unkritische Beispieldatei angeben. Das [Testprotokoll](TEST_PROTOCOL.md) hilft beim strukturierten Test.

## Geräteanschluss

Der Simulator bleibt Standard. Ein echter Anschluss erfolgt ausdrücklich im Gerätebereich. USB-Gerätezugriff muss für den Benutzer erlaubt sein; bei Berechtigungsproblemen die Zugriffsrechte beziehungsweise Mitgliedschaft in der Ubuntu-Gruppe `dialout` mit dem Systemadministrator prüfen und anschließend neu anmelden. Die Anwendung verlangt keine Ausführung als root.

### M3 für diese Verbindung einrichten

1. Vor dem Verbinden die Laseremission **am Gerät sicher deaktivieren**, den Fahrweg räumen und am Gerät bleiben. Schon das Öffnen des USB-Anschlusses kann die Firmware starten; der Verbindungsaufbau setzt den Controller zurück. Unter **Gerät & Positionierung → Zuordnung …** das tatsächliche Gerät mit seinem Anschluss auswählen, dann ausdrücklich **GRBL verbinden**. Eine Verbindung allein startet keinen Auftrag.
2. **M3 einrichten …** öffnen. Der Dialog liest Controller- und Einstellungsdaten. Bei einer Ablehnung den angezeigten Grund klären; die Anwendung löscht keine Startupbefehle und umgeht keine Schutzverriegelung.
3. Endschalter, Homingrichtung/Pull-off, freien Fahrweg und deaktivierte Laseremission tatsächlich prüfen und bestätigen. Erst **Referenzfahrt jetzt starten** fordert die Fahrt an. Anschließend den wirklichen Referenzort am Gerät bestätigen.
4. Je Achse einen freien **1-mm-Prüfschritt** wählen, Richtung und Strecke messen und einzeln bestätigen. Der Positionszähler ersetzt diese Beobachtung nicht. Zeichnungsbezug: Ursprung links unten, +X nach rechts, +Y nach oben.
5. Den gemessenen **Innenfahrraum in Maschinenkoordinaten** mit Abstand zu den mechanischen Grenzen eingeben. Negative Werte sind möglich; die nominellen 200 × 150 mm sind kein Messnachweis. Alle vier Ecken einzeln laserfrei anfahren und am Gerät bestätigen.
6. Die geprüfte Ursprungsecke ausdrücklich als Arbeitsnull setzen und anschließend die angezeigte Maschinen-/Arbeitskoordinaten-Zuordnung prüfen und bestätigen. Der Dialog startet danach keinen Laserauftrag. Das Design muss vollständig innerhalb des geprüften Innenraums liegen.

Die Einrichtung gilt nur für diese Gerätesitzung. STOPP, Reset, Trennung, Fehler oder Änderungen an Firmware/Einstellungen entwerten sie; anschließend erneut einrichten. Ein Projekt speichert keine Gerätefreigabe. Bei falscher Richtung oder unerwartetem Verhalten sofort STOPP beziehungsweise den Not-Aus am Gerät verwenden. Siehe [Einrichtungsvertrag](../M3_SESSION_COMMISSIONING.md).

### Absaugung bestätigen und ersten Auftrag starten

Nach erfolgreicher Einrichtung die Laserfreigabe am Gerät erst für die beaufsichtigte Materialprobe wiederherstellen. Material, Fokus, geschlossene Einhausung und freien Arbeitsraum prüfen; eigene Materialwerte beziehungsweise einen geeigneten kleinen Materialversuch verwenden.

Der Gehäuse-Fan-Port ist weiterhin ungeklärt: **M8 ist kein belegter Lüfterknopf.** Der neue Modus schaltet keine Absaugung ein. Er ist nur nutzbar, wenn eine geeignete Absaugung tatsächlich unabhängig läuft und am Gerät geprüft werden kann. Eine solche Anlage wird nicht als vorhanden vorausgesetzt. Wenn die vorhandene Absaugung erst mit Laseremission anläuft oder gar nicht läuft, die Bestätigung nicht setzen; dieser Modus löst das Fan-Port-Problem nicht.

1. Unter **Absaugung und Aufsicht** gegebenenfalls den tatsächlich rauchfreien und geklärten Zustand mit **Sicheren Zustand bestätigen** bestätigen. Dann **Beaufsichtigte Absaugung ohne Sensor wählen**.
2. Den konkreten Vorgang wählen: **Neuen Auftrag starten**, **Fokuspunkt prüfen** oder **Pausierten Auftrag fortsetzen**. Laufende Absaugung, eigene Anwesenheit sowie Material/Fokus/Gehäuse/Arbeitsraum wahrheitsgemäß bestätigen und **Für diesen Vorgang bestätigen** wählen.
3. Innerhalb der angezeigten Frist von höchstens **30 Sekunden** die zugehörige Start-, Fokus- oder Fortsetzen-Taste selbst betätigen. Die Absaugungsbestätigung startet nichts und ist einmalig an diesen Vorgang gebunden. Bei abgelaufener oder veralteter Bestätigung erneut prüfen und bestätigen.

Luftstrom wird **nicht gemessen**. Während der gesamten Bearbeitung am Gerät bleiben und die Absaugung weiterlaufen lassen, auch während einer Pause. Vor jedem Fortsetzen frisch bestätigen; nach STOPP, Auftragsende, Fehler oder Trennung gilt keine alte Bestätigung weiter. Bei entfallener Absaugung oder Aufsicht **Absaugung oder Aufsicht entfallen – STOPP** verwenden; bei fehlender Reaktion direkt am Gerät abschalten. Die Software schaltet die Absaugung nicht automatisch aus und kann ihren Zustand bei Strom-/Verbindungsverlust nicht garantieren. Live-Overrides bleiben gesperrt. Details: [Absaugvertrag](../OPERATOR_ATTENDED_EXTRACTION.md).

Eine USB-Verbindung über einen umschaltenden KVM kann beim Wechsel zum anderen PC verloren gehen. Ein verworfener Auftrag wird nach erneutem Anschließen nicht automatisch fortgesetzt; Geräteeinrichtung, Materiallage und Absaugung neu prüfen.

## Optionale Komponenten

PDF/AI/EPS-Konvertierung benötigt das separat installierte Inkscape. Lokale KI-Fototiefe benötigt einen zusätzlichen, ausdrücklich heruntergeladenen Laufzeit-/Modell-Cache; Grundfunktionen und Paketstart benötigen ihn nicht:

```bash
sudo apt install python3-venv
python3 /usr/share/blazer-studio/tools/setup_depth.py
```

Die anschließende Tiefenschätzung läuft lokal. Sie erzeugt eine relative Tiefenkarte, keine vermessene Oberflächengeometrie.

## Aktualisieren und entfernen

Ein neueres Paket mit `sudo apt install ./DATEINAME.deb` installieren. Entfernen: `sudo apt remove blazer-studio`. Eigene Projekte und Benutzerdaten bleiben getrennt von den installierten Programmdateien. Die Software ist Source Available mit kostenloser Weitergabe; gewerbliche Laserarbeiten sind erlaubt, Verkauf der Software ist ausgeschlossen. Siehe die mitgelieferte Lizenz.
