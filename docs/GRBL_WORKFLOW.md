# Berechnete Jobs und GRBL

Blazer Studio startet im **Simulator**. Eine echte Verbindung entsteht erst im Bereich **Gerät** durch Auswahl des seriellen Anschlusses und **GRBL verbinden**. Es gibt keine automatische Verbindung, Referenzfahrt oder Jobausführung beim Start.

## Projekt → Pfade → Vorschau → Ausgabe

- Rechtecke, Ellipsen, Textkonturen und unterstützte SVG-Vektoren werden in geometrische Pfade umgewandelt. Text verwendet die gespeicherte Schriftwahl und dieselbe Pfadgeometrie wie der Editor.
- **Gravieren:** gerichtete Fülllinien mit dem eingestellten Intervall; Winkel, Kreuzschraffur und Überlauf werden im Pfadsatz berücksichtigt. Innenräume bleiben nach der Füllregel frei.
- **Schneiden:** linearisierte Konturen mit eingestellten Durchgängen. Kleinere Begrenzungen werden vor größeren ausgegeben; dies ist noch keine vollständige Schnittreihenfolgenoptimierung.
- Die Vorschau zeichnet die berechneten Arbeits- und Leerfahrten. GCode und Live-Übertragung verwenden denselben eingefrorenen Pfadsatz. Die erste Leerfahrt wird ab Arbeitsnull dargestellt; beim realen Start kann die aktuelle Maschinenposition davon abweichen.
- Die Zeitangabe ist eine geometrische Schätzung aus Strecken und Vorschüben, mit angenommenen 3000 mm/min für Leerfahrten. Die angezeigte Dauer ergänzt einen konservativen Start-/Stopp-Ansatz mit 240 mm/s²; die reine Vorschubzeit bleibt als Untergrenze verfügbar. Tatsächliche Eilganggeschwindigkeit, Lookahead und Kommunikationspausen sind nicht gemessen.
- **GCode** exportiert einen GRBL-Job mit Millimetern, absoluten Koordinaten, G54, M4, der Leistungsskala des Projektprofils (beim M3 S-Max 1000) und abschließend M5/S0. Die Datei enthält diese Voraussetzungen als Kommentar. Sie darf nicht ungeprüft auf anders konfigurierten Controllern ausgeführt werden.
- Die Simulation spielt die berechneten Bewegungen in 16 Sekunden ab. Ihr Zeitraffer ist keine Messung der tatsächlichen Jobdauer.

Aktive Objekte außerhalb der Fläche des Projektprofils (beim M3 200 × 150 mm), nicht auswertbare Inhalte oder zu komplexe Jobs verhindern die gesamte Ausgabe. Es werden keine stillschweigend unvollständigen Jobs exportiert. Grenzen: 500000 Bewegungssegmente, begrenzte SVG-/Bildgröße. Ein globaler SVG-Clip in Benutzerkoordinaten ist unterstützt. Mehrere/gemischte Clips, Bounding-Box-/CSS-Clips und eingebettete Rasterinhalte werden noch abgelehnt. SVG-Dateien bleiben im Editor zusammenhängende Objekte. Farben werden nicht automatisch zu getrennten Laseroperationen.

## Bilder und Materialtests

**Motiv laden** (Basic) beziehungsweise **Bild** (Advanced) importiert unterstützte Rasterformate mit Originalauflösung bis 4096 × 4096 Pixel innerhalb der [Speichergrenzen](RASTER_QUALITY.md). Unter **Werkzeuge → Bild** stehen Korrekturen, Zuschnitt, Masken, zwei Ditherverfahren, Schwellenwert und Graustufen-Leistungsmodulation zur Verfügung. Anfangsmaßstab: höchstens 100 mm lange Seite, bei kleinen Bildern 0,1 mm je Pixel; Maße und Ausgabe-Raster sind anschließend einstellbar.

**Materialtest** erzeugt ein neues bearbeitbares Projekt: 2–10 Stufen je Achse, 5-mm-Gravurfelder mit 8 mm Abstand ab X10/Y10. Zeilen von unten nach oben ändern die Geschwindigkeit; Spalten von links nach rechts die Leistung. Die einzelnen Zellen besitzen eigene Leistungs-/Geschwindigkeitswerte, die rechts sichtbar und editierbar sind. Erzeugung allein startet keinen Laser. Vorschau, Projektdatei und GCode-Export funktionieren wie bei anderen Projekten.

Der Entwicklungsstand nach beta.2 speichert **Projektformat 8** mit [Projektnotizen](PROJECTS.md), eigenen Objektwerten und [Relief-Tiefenkarten](RELIEF.md). Formate 1–7 bleiben lesbar. Neu gespeicherte Dateien benötigen beta.3-dev oder neuer; beta.2 kann sie nicht öffnen. Ein laufender übertragener Job bleibt unverändert, auch wenn der Editor danach geändert wird.

![Bearbeitbare Materialtestmatrix](../assets/material-test.png)

## Echte Verbindung

Jeder berechnete Auftrag enthält sein eingefrorenes Geräteprofil. Vor der Übertragung muss dessen Ausgabe-Fingerprint zur geprüften Verbindung passen. Änderungen an Arbeitsraum, Leistungsskala, Kopf, Controllerparametern oder erklärten Fähigkeiten verhindern die Übertragung eines alten Auftrags vor dem ersten Befehl. Anzeigename und Herkunftsnotizen sind davon ausgenommen. Vorberechnete Befehle werden ausschließlich aus dem eigenen Auftragsprofil erzeugt; ein Hintergrund-Ergebnis für ein anderes Profil wird verworfen. Aktuell bleibt das M3 das einzige an die Oberfläche und reale Verbindung angebundene Profil.

Die aktuelle echte M3-Anbindung prüft GRBL 1.1 beziehungsweise den am Blaze X gefundenen Grbl-ESP32-Fork 1.3a, `$30=1000`, `$31=0`, `$32=1`, Statusausgabe in mm (`$13=0`), G54 und die hinterlegten 200 × 150 mm (`$130/$131`). Sie schreibt diese Firmwareeinstellungen nicht automatisch um. Erst ein passender Leerlauf mit gültiger Arbeitsposition, bestätigter Nullleistung und ohne aktive gemeldete Eingänge erlaubt einen Job. Ein Status mit fehlender oder ungültiger Position/Leistung übernimmt keine frühere Freigabe.

Jobübertragung verwendet Zeichenkredite: höchstens 100 Bytes stehen gleichzeitig ohne `ok` im RX-Puffer. Jede Zeile bleibt ihrer Bestätigung zugeordnet; einzelne manuelle Aktionen und die Konfigurationsabfrage bleiben sequenziell. Zusätzliche Status- und Meldungszeilen gelten nicht als Bestätigung. Nach der letzten Bestätigung wartet der Auftrag noch auf eine neue **Idle**-Meldung. Pause benutzt Feed Hold, Fortsetzen Cycle Start; Stop benutzt Feed Hold und Soft-Reset und setzt keinen Auftrag automatisch fort. Fehler, Alarm oder Kommunikationsausfall brechen die Host-Übertragung ab.

Jog verwendet relative `$J`-Bewegungen und begrenzt die Zielposition im Arbeitsraum. [Rahmenfahrt](FRAMING.md) benutzt Laser-Aus und Eilgänge; ihr Ziel kann der berechnete Auftrag, die sichtbare Auswahl oder T1/T2 sein. Referenzfahrt ist eine ausdrücklich ausgelöste Aktion bei aktiviertem Homing; **Arbeitsnullpunkt hier setzen** setzt X/Y in G54 am aktuellen Ort.

Beim M3 fordert der Hardware-Fokusknopf einen **0,2-s-Puls mit S20 (2 % Sollwert)** an. Puls, Verweilen und M5/S0 werden gemeinsam in den RX-Puffer geschickt; Loslassen fordert einen Abbruch an, zusätzlich besteht ein Host-Zeitlimit. Ein beschädigter oder ausgefallener Kommunikationsweg kann keinen garantierten Laser-Aus-Befehl liefern. Die optische Wirkung und geräteseitige Abschaltung dieses Pulses wurden am realen Gerät noch nicht abgenommen. Absaugung und Lasermodulkühlung werden nicht als austauschbare Funktionen behandelt; keine ungeprüften Lüfterparameter werden umgeschrieben.

## Tatsächlich geprüfter Geräteumfang am 10.11.2026

Controller: **Blaze X M3_C_V01**, **Grbl-ESP32 1.3a.20211103**, 115200 Baud. Ausgelesen: S-Max 1000, Lasermodus an, mm, Homing an, Arbeitsraum 200 × 150 mm. Der eigene C++-Treiber hat Referenzfahrt, relative Fahrt nach X5/Y5 und Rückfahrt nach X0/Y0 controllerseitig bestätigt. Währenddessen wurden nur Laser-Aus-/Abfrage-/Bewegungsbefehle gesendet. Zusätzlich wurde ein vollständiger berechneter 5-mm-Quadratjob bei X10/Y10 mit jeder Laserfreigabe durch M5/S0 ersetzt übertragen, abgearbeitet und anschließend nach X0/Y0 zurückgefahren. Endzustand Idle und S0; keine unabhängige optische Leistungs- oder Maßmessung.

Der Nutzer hatte kein Material eingelegt und hat den Gehäuselüfter für Tests ohne Laseremission ausschalten lassen; Lüfter-Aus wurde von ihm bestätigt. Es wurde keine reale Gravur, kein Schnitt und kein Fokus-Laserpuls ausgeführt. Vor einer Bearbeitungsprobe sind passendes Material und betriebsbereite Absaugung erforderlich. Die Herstellerwerte für M3 Pro sind weiterhin keine abgeschlossene Materialvalidierung des vorhandenen M3.

Reproduzierbare Entwicklerprüfung, **mit mechanischer Bewegung nur bei ausdrücklich gewähltem `--motion` oder `--dry-job`**:

```bash
./build/blazer-grbl-probe /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
./build/blazer-grbl-probe /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 --motion
./build/blazer-grbl-probe /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 --dry-job
```

Das Prüfwerkzeug ist für diesen eingerichteten M3 bestimmt. Es setzt Homing und freien Bewegungsraum voraus; es enthält keine M3-/M4-Laserfreigabe. Es ersetzt keine Material- und Emissionsabnahme.

Protokollquellen: [GRBL Interface](https://github.com/gnea/grbl/blob/master/doc/markdown/interface.md), [GRBL Laser Mode](https://github.com/gnea/grbl/blob/master/doc/markdown/laser_mode.md), [Grbl-ESP32](https://github.com/bdring/Grbl_Esp32). Die Implementierung ist eigenständig; keine Firmwarequellen werden mitgeliefert.

Zusätzlich in 0.6: freie weitere Operationen, Schnittfuge/Haltestege, Scanwinkel/Kreuzschraffur/Überlauf, Live-Overrides mit `Ov`-Status und deutsche Fehlerzusätze. [Details und Grenzen](EXPANSION.md). Erweiterte GRBL-Funktionen wurden mit Pseudoterminal-Controller geprüft; keine neue Geräteabnahme mit Emission.


## Kommunikationsprüfung vom 12.11.2026

Während der Einstellungsabfrage pausiert das Statuspolling. Werden Status und Einstellungszahl dennoch verschachtelt empfangen, wird die betroffene Konfiguration einmal vollständig neu abgefragt. Der möglicherweise abgeschnittene Wert wird nicht benutzt; ein wiederholter Konflikt trennt die Verbindung. Alarm/Door während eines aktiven Auftrags und USB-Verlust verwerfen den alten Auftrag. Nach einer neuen Verbindung erfolgt keine automatische Fortsetzung. Die Software kann bei einem unterbrochenen Kommunikationsweg keine physische Abschaltung bestätigen.

Die erweiterten PTY-Prüfungen und ihre Grenzen stehen im [Prüfbericht](QA_REPORT.md); reale Ergebnisse im [Geräteprotokoll](HARDWARE_ACCEPTANCE.md). Der Laser blieb während dieser Entwicklungsrunde ausgeschaltet.


Arbeitskoordinaten werden vor Ausgabe mit dem bestätigten G54-Versatz in Maschinenkoordinaten geprüft. Ein im Editor gültiger Job kann nach Verschieben des Arbeitsnullpunkts physisch außerhalb des 200×150-mm-Raums liegen; dann wird der ganze Job vor dem ersten Befehl abgelehnt. Dieselbe Prüfung gilt für Schrittbewegung, Rahmenfahrt und Fokusposition. WPos allein ohne bestätigten Versatz gibt keine Bewegung frei.

Nach **Arbeitsnullpunkt hier setzen** muss ein neuer Offset nach Abschluss der G10/G54-Befehle gemeldet sein. Ein während der Befehlsfolge eingetroffener alter WCO-Wert gibt keine Folgebewegung frei.
