# Blazer Studio – Einstieg in beta.10.7-dev

Die [Anforderungen an den einfachen Standardworkflow](USABILITY_REQUIREMENTS.md) gelten für die weitere Entwicklung.

**Beta-Ziel:** ein nutzbarer M3-Grundworkflow vom Entwurf bis zum beaufsichtigten Auftrag. Die vollständige LightBurn-Parität bleibt ein weiteres Entwicklungsziel. beta.10.5 ergänzt gespeicherte Gerätekalibrierung mit automatischem Controllerabgleich. Die Maschinenposition wird bei jeder Verbindung neu referenziert. Der beaufsichtigte Gerätezyklus benötigt keinen externen Absaugsensor. Diese Softwarefunktionen sind keine bereits erfolgte physische Abnahme deines Geräts. Die veröffentlichte beta.7 und ältere Versuche erhalten dadurch keine nachträgliche Freigabe. Siehe [aktuelle Grenzen](KNOWN_LIMITATIONS.md).

Getestete Paketplattform: **Ubuntu 26.04, x86_64**. Andere Linux-Versionen sind noch nicht als Paketplattform bestätigt. Es handelt sich um einen Software-Testkandidaten; Geräte-/Materialabnahme und externe Bedienungstests sind offen.

Bei jedem normalen Programmstart erscheint ein zweisprachiger Sicherheitshinweis, auch nach abgeschlossener Einführung und vor einer Wiederherstellungsentscheidung. **Laser niemals unbeaufsichtigt betreiben / Never operate the laser unattended.** Ein Verbindungsabbruch oder hängender Controller kann den Laser eingeschaltet lassen; Software kann dann kein Laser-Aus garantieren. Während der gesamten Bearbeitung am Gerät bleiben und bereit sein, die Stromversorgung direkt am Gerät zu unterbrechen. Mit **Verstanden / I understand** bestätigen; eine dauerhafte Abschaltung dieses Hinweises gibt es nicht.

## Installation

Das Debian-Paket und die passende Quellcode-Datei gehören zum selben Kandidaten. Prüfsummen stehen in `SHA256SUMS`.

```bash
sha256sum -c SHA256SUMS
sudo apt install ./blazer-studio_*_amd64.deb
```

Die Paketverwaltung installiert die benötigten Laufzeitbibliotheken. Ein Compiler oder Qt-Entwicklungsumgebung ist nicht erforderlich. Starte anschließend **Blazer Studio** im Anwendungsmenü oder `blazer-studio` im Terminal. Beim Wechsel von einer alten Entwicklungsversion das alte Fenster regulär schließen; Benutzerdateien werden beim Installieren nicht gelöscht.

## Dateiauswahl unter Kubuntu / KDE

Ab beta.10.1 verwendet Blazer Studio mit installierter KDE-Integration den nativen KDE-Dateidialog. Er bietet die vertraute Orte-Seitenleiste, Pfadnavigation, Dateiansichten und die Vorschaufunktionen des Desktops. Das Qt-Quick-Hauptfenster bleibt erhalten. Auf anderen Desktops richtet sich die Dateiauswahl nach der vorhandenen Plattformintegration; ohne native Integration bleibt die Qt-Auswahl verfügbar. Für KDE stammt die Integration im Ubuntu-Paket aus `plasma-integration`.

Mit **Strg+L** lässt sich ein vollständiger Pfad direkt eingeben. Unter Linux zählt Groß- und Kleinschreibung: `Projekte/Laser` unterscheidet sich von `projekte/laser`. Nach einer Aktualisierung das laufende Programm regulär schließen und neu öffnen, damit die geänderte Desktopintegration aktiv wird.

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

Ab beta.10.2 zeigt „M3 einrichten“ abgelehnte Eingabeaktionen direkt im Dialog an. Ein Klick ohne gültige Eingabefreigabe löst weiterhin keine Bewegung aus; die sichtbare Fehlermeldung ersetzt keine Referenz- oder Fahrraumprüfung.

Beta.10.3 korrigiert die Erkennung von Mausklicks auf Geräteschaltflächen in Dialogen. Der Dialoghintergrund wurde zuvor fälschlich als Überdeckung des sichtbaren Knopfs behandelt. Tatsächlich überlagerte, deaktivierte oder abgeschnittene Bedienelemente bleiben gesperrt. Die Gerätefreigaben und notwendigen Prüfungen bleiben bestehen.

Beta.10.4 prüft den gemessenen Innenfahrraum zusammen mit dem bestätigten Referenzort und allen bestätigten Prüfwegen. Der nur für kleine Achsprüfungen vorgesehene ±2-mm-Raum wird nicht mehr pauschal auf beiden Seiten hinzugerechnet. Dadurch lassen sich beispielsweise 0–199 × 0–149 mm bei Home 0,0 und positiven 1-mm-Proben einrichten. Außerhalb liegende bestätigte Prüfwege werden weiterhin mitgeprüft. Die vier Eckfahrten und die Koordinatenprüfung bleiben erforderlich; auch Rasterüberlauf muss innerhalb des freigegebenen Fahrraums liegen.

Die zuletzt ausgewählte lokale Geräteinstanz und ihr Profil bleiben erhalten. Die Auswahl öffnet noch keinen echten Anschluss; eine Verbindung erfolgt ausdrücklich im Gerätebereich. Ohne gespeichertes Gerät bleibt der Simulator Standard. USB-Gerätezugriff muss für den Benutzer erlaubt sein; bei Berechtigungsproblemen die Zugriffsrechte beziehungsweise Mitgliedschaft in der Ubuntu-Gruppe `dialout` mit dem Systemadministrator prüfen und anschließend neu anmelden. Die Anwendung verlangt keine Ausführung als root.

### Automatisch beim Verbinden referenzieren

Ab beta.10.6 kann für jedes ausgewählte physische Gerät **Beim Verbinden automatisch referenzieren** aktiviert werden. Die Option wird für diese Gerätezuordnung gespeichert und wirkt beim nächsten bewusst bestätigten Verbindungsaufbau. Die Verbindungstaste heißt dann **Verbinden und referenzieren**; den Referenzweg freihalten und die Fahrt beobachten.

Blazer prüft zuerst Controllerkennung, Profil, Einstellungen und die gespeicherte Kalibrierung. Erst bei Übereinstimmung folgt eine Referenzfahrt ohne Laseremission. Ohne passende Kalibrierung, bei STOPP, Verbindungswechsel, Fehler oder abgelaufener Verbindungsautorisierung startet keine automatische Referenzfahrt. Ein eventuell erforderlicher Anlauf in einen Innenraum außerhalb von Home und ein Laserauftrag benötigen weiterhin ihre eigenen Startaktionen.

Die Automatik ersetzt die Referenzfahrt-Taste, nicht den Positionsnachweis: Kalibrierung speichert die Maschinengeometrie; Homing bestimmt die aktuelle Kopfposition nach einem Controllerneustart. Ein Positionszähler von 0/0 ist dafür allein kein Nachweis.

### Ein bekanntes M3 initialisieren

1. Das gespeicherte Gerät verwenden; nur zum Wechseln **Laser wechseln** öffnen. **GRBL verbinden** beziehungsweise **Verbinden und referenzieren** betätigen. Blazer liest Firmware, Controllerkennung, Einstellungen, Offsets und Startupblöcke automatisch. Dabei fährt der Kopf nicht los.
2. Passt eine gespeicherte Einrichtung zum Controller, **Referenzieren – ohne Laser** drücken und die freie Referenzfahrt am Gerät beobachten. Bei unverändertem Arbeitsursprung ist die Initialisierung damit abgeschlossen.
3. Liegt der Referenzort außerhalb des gespeicherten Innenraums, bietet Blazer eine zusätzliche langsame Fahrt **Sicheren Startpunkt anfahren – ohne Laser** an. Hat sich der Arbeitsursprung geändert, die neue Zuordnung im kleinen Ursprungsdialog prüfen und bestätigen. Es werden keine alten Offsets automatisch zurückgeschrieben.

Die gespeicherten Daten enthalten Fahrraum und Prüfparameter, keine gültige Position und keine Startfreigabe. Sie sind an Geräteinstanz, Controllerkennung, Profil und Einstellungen gebunden. Fehlt ein passender Datensatz, nennt Blazer den Grund und fordert die einmalige vollständige Einrichtung an. Beschädigte Dateien werden nicht übernommen. Ältere Versionen haben keinen wiederverwendbaren Datensatz gespeichert; alte Bildschirmbestätigungen werden nicht nachträglich als Datei importiert.

### Ein M3 erstmalig oder nach relevanten Änderungen einrichten

1. Vor dem Verbinden die Laseremission **am Gerät sicher deaktivieren**, den Fahrweg räumen und am Gerät bleiben. Schon das Öffnen des USB-Anschlusses kann die Firmware starten; der Verbindungsaufbau setzt den Controller zurück. Unter **Gerät & Positionierung → Zuordnung …** das tatsächliche Gerät mit seinem Anschluss auswählen, dann ausdrücklich **GRBL verbinden**. Eine Verbindung allein startet keinen Auftrag.
2. **M3 einrichten …** öffnen. Die Controllerdaten werden nach dem Verbinden automatisch gelesen. Bei einer Ablehnung den angezeigten Grund klären; die Anwendung löscht keine Startupbefehle und umgeht keine Schutzverriegelung.
3. Endschalter, Homingrichtung/Pull-off, freien Fahrweg und deaktivierte Laseremission tatsächlich prüfen und bestätigen. Erst **Referenzfahrt jetzt starten** fordert die Fahrt an. Anschließend den wirklichen Referenzort am Gerät bestätigen.
4. Je Achse einen freien **1-mm-Prüfschritt** wählen, Richtung und Strecke messen und einzeln bestätigen. Der Positionszähler ersetzt diese Beobachtung nicht. Zeichnungsbezug: Ursprung links unten, +X nach rechts, +Y nach oben.
5. Den gemessenen **Innenfahrraum in Maschinenkoordinaten** mit Abstand zu den mechanischen Grenzen eingeben. Negative Werte sind möglich; die nominellen 200 × 150 mm sind kein Messnachweis. Alle vier Ecken einzeln laserfrei anfahren und am Gerät bestätigen.
6. Die angezeigte Maschinen-/Arbeitskoordinaten-Zuordnung prüfen. Falls nötig die geprüfte Ursprungsecke ausdrücklich als Arbeitsnull setzen. Mit **Koordinaten bestätigen und Einrichtung abschließen** abschließen. Der Dialog wartet auf die Bestätigung und schließt anschließend selbst; er startet keinen Laserauftrag. Das Design muss vollständig innerhalb des geprüften Innenraums liegen.

Eine vollständig bestätigte Einrichtung wird lokal gespeichert, wenn eine eindeutige Controllerkennung vorhanden ist. Ohne diese Kennung gilt sie nur für die aktuelle Verbindung. STOPP, Reset, Trennung oder Fehler entwerten die aktuelle Positionsreferenz; ein passender gespeicherter Datensatz kann nach neuer Prüfung und bewusster Referenzfahrt erneut verwendet werden. Änderungen an Firmware, relevanten Einstellungen, Profil oder Controller erfordern eine neue vollständige Einrichtung. Ein Projekt speichert keine Gerätefreigabe. Bei falscher Richtung oder unerwartetem Verhalten sofort STOPP beziehungsweise den Not-Aus am Gerät verwenden. Siehe [Einrichtungsvertrag](../M3_SESSION_COMMISSIONING.md).

### Stattdessen ab Handposition starten

**Start ab Handposition …** bietet einen eigenen lokalen Ablauf: den Kopf bei tatsächlich freien Achsen von Hand setzen, vier reale Restwege messen, den lokalen Startpunkt übernehmen und alle geplanten Fahrwege einschließlich Anfahrt und Rasterüberlauf prüfen. Eine globale Maschinenreferenz wird daraus nicht abgeleitet. Die [Schritt-für-Schritt-Anleitung](../MANUAL_START.md) beschreibt Bestätigungen, Rahmenfahrt, Start und Grenzen. Die Absaugung bleibt separat erforderlich.

### Beaufsichtigter Gerätebetrieb und erster Auftrag

Ab beta.10.7 genügt nach erfolgreicher Verbindung und Initialisierung im normalen Gerätebetrieb **Am Laser starten** direkt aus der Bearbeitung. Dieser frische Klick bestätigt zugleich die eigene Anwesenheit für genau den vorbereiteten Auftrag. Eine zusätzliche Moduswahl, Vorschau oder Anwesenheitsbestätigung ist für diesen Start nicht nötig. Materialwerte bleiben sichtbar und müssen zum tatsächlichen Material passen. Eine echte vorherige Störung verlangt weiterhin die ausdrückliche Klärung des Gerätezustands; der Startklick löscht keine Fehlersperre. Fokus und Fortsetzen behalten ihre eigenen aktionsbezogenen Bestätigungen. Unter Advanced bleiben ausdrücklich gewählte Sensor-/externe Zubehörmodi mit ihrem eigenen Ablauf verfügbar.

Die externe Absaugung liegt in diesem Modus außerhalb der Programmsteuerung und wird nicht als Pflichtgerät oder Sensor abgefragt. Beide tatsächlichen Gerätelüfter müssen beim ersten realen Auftrag beobachtet werden. Die Software meldet keinen gemessenen Lüfterlauf und bietet keine unbelegten getrennten Lüfterausgänge an. Der dynamische Gerätezyklus bleibt bei internen G0-Leerfahrten aktiv; G0 unter geprüftem GRBL-Lasermodus ist laserfrei. Am Ende wird M5/S0 angefordert. Firmwareabhängige Lüfterwirkung und Nachlauf sind nicht durch ein ACK bewiesen.

### Optionale externe Absaugungsanlage

Der folgende ältere Zusatzmodus ist nur für eine ausdrücklich gewählte externe Anlage vorgesehen; er ist keine Voraussetzung für den oben beschriebenen Gerätebetrieb.

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
