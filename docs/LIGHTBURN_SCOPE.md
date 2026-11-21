# Funktionsumfang: Referenz LightBurn 2.2.01

Stand und Recherche: 21.11.2026 (erneut gegen offizielle Quellen geprüft). Verbindliches Produktziel aus Mathias’ Auftrag: den Funktionsumfang der aktuellen LightBurn-Version als Referenz übernehmen, mit eigener OrcaSlicer-artiger Linux-Oberfläche und BlazerX als erstem Gerät.

## Referenz und Geltung

Der offizielle [Release-Index](https://release.lightburnsoftware.com/LightBurn/Release/) verweist mit `latest` auf **2.2.01**; die [Downloadliste](https://release.lightburnsoftware.com/LightBurn/Release/latest/) nennt den 02.11.2026 für die Installationsdateien. Der [2.2-Changelog](https://docs.lightburnsoftware.com/2.2/NewFeatures/#2201) enthält die Änderungen seit 2.2.00. Die Ausgangsbasis umfasst Core und Pro, einschließlich neuer beziehungsweise experimenteller Funktionen. Native Linux-Unterstützung endete bei LightBurn nach 1.7; Blazer Studio wird nativ für Linux entwickelt. Quelle: [LightBurn 2.0 / Systemkompatibilität](https://lightburnsoftware.com/blogs/news/lightburn-2-0-00-new-shapes-edit-nodes-submenu-dark-mode-and-more).

Dies ist ein versionierter Zielkatalog, keine Behauptung bereits implementierter Gleichwertigkeit. Die Familien bleiben bis zu ihrer vollständigen Detailabnahme offen; vorhandene Teilumfänge stehen in FUNCTION_ACCEPTANCE.md. Vor Implementierung einer Funktionsfamilie werden ihre Detailoptionen und Abnahmetests gegen die verlinkte Referenz spezifiziert. Veröffentlichungen nach dem Stichtag ändern den Umfang nicht stillschweigend; spätere Vergleiche werden separat dokumentiert.

Geräteunabhängige Funktionen gehören zum Zielumfang. Hardwarefunktionen werden auf dem BlazerX umgesetzt, soweit Controller und Zubehör sie ermöglichen. Unbekannte Fähigkeiten bleiben offen; nicht zutreffende Funktionen erhalten eine dokumentierte Begründung, statt aus dem Katalog zu verschwinden. Seit Mathias’ erweitertem Auftrag vom 12.11.2026 sind weitere Laser ausdrücklich Produktziel. Die konkrete Unterstützung wird pro Controller und Modell gemäß MULTI_DEVICE_PLAN.md nachgewiesen; das M3-Profil belegt keine andere Hardware.

## Umsetzungsetappen

- **P0:** Gerätebestand, Protokoll und Grundlagen.
- **P1:** Editor und reproduzierbare Kernjobs, Simulator, direkte Steuerung.
- **P2:** vollständiger Vektor-/Rasterworkflow, Kalibrierung, Bibliotheken.
- **P3:** erweiterte Gestaltung, Produktion und Integration.
- **P4/H:** hardwaregebundene Spezialfunktionen; Fähigkeiten und Aufwand prüfen.

P0 bis P4 sind Reihenfolge und Abhängigkeiten, keine Streichung späterer Funktionen. Hardwarefreigabe ist zusätzlich nötig, auch wenn ein Eintrag P1 trägt.

## Zielmatrix

### Projekte, Bedienung und Austausch

Referenz: [Dateiverwaltung](https://docs.lightburnsoftware.com/2.2/Reference/FileManagement/), [Funktionsübersicht](https://docs.lightburnsoftware.com/2.2/Reference/).

| ID | Zu liefernde Fähigkeit | Etappe |
|---|---|---|
| F01 | Projekt öffnen/speichern, Vorlagen, Notizen, zuletzt verwendete Dateien | P1 |
| F02 | Mehrere benannte Arbeitsblätter im Projekt; kopieren, umbenennen, aktive Ausgabe eindeutig wählen | P1 |
| F03 | Undo/Redo mit sichtbarem Verlauf; Autosave und Wiederherstellung | P1 |
| F04 | SVG, DXF, AI, PDF, HPGL/PLT; GCode-Geometrie und Austausch mit RDWorks, SignCut und MillMage nach Formatprüfung | P1–P3 |
| F05 | PNG/JPEG/BMP/GIF/TIFF/WebP; Export von Vektoren, Bildern und Jobs | P1–P3 |
| F06 | LightBurn-Projekte und Bibliotheken als Migrationsziel untersuchen; keine verlustfreie Kompatibilität vor Tests behaupten | P2–P3 |
| F07 | Auswahl, Zwischenablage, Transformation, mm/inch, Raster, Fangpunkte und Hilfslinien | P1 |
| F08 | Anpassbare Bedienfelder, Tastenkürzel, Themen, Sprache, kontextbezogene Hilfe, Druckausgabe | P1–P3 |

### Zeichnen und Konstruieren

Referenz: [Zeichenwerkzeuge](https://docs.lightburnsoftware.com/2.2/Reference/UI/CreationToolbar/), [Modifikationen](https://docs.lightburnsoftware.com/2.2/Reference/UI/ModifiersToolbar/), [Text](https://docs.lightburnsoftware.com/2.2/Reference/Text/).

| ID | Zu liefernde Fähigkeit | Etappe |
|---|---|---|
| F09 | Linien, Kurven und geometrische Grundformen zeichnen; exakt bemaßen und messen | P1–P2 |
| F10 | Knoten bearbeiten, Pfade teilen/verbinden/schließen, toleranzbasiert reparieren | P2 |
| F11 | Doppelte Geometrie erkennen, Pfade vereinfachen und Schnittgeometrie prüfen (Vector Troubleshooter als Referenz) | P2 |
| F12 | Vereinigung, Differenz, Schnittmenge, Versatz, Trim und Schnittwerkzeuge | P2 |
| F13 | Rundungen, Radien, Tangentialkreise, Konturhüllen und Slot-/Laschenanpassung | P2–P3 |
| F14 | Ausrichten, Verteilen, Spiegeln, Gruppieren, Sperren und Zeichenreihenfolge | P1–P2 |
| F15 | Rechteck-/Kreismuster, Kopien entlang eines Pfads, Zweipunkt-Skalierung/-Drehung | P2 |
| F16 | Verformen und Verzerren; aus Ausgabewegen wieder bearbeitbare Pfade erzeugen | P3 |
| F17 | Text mit Schriftwahl, Abständen, Ausrichtung, Pfadtext und Pfadkonvertierung | P2 |
| F18 | Variable Beschriftung, CSV-Daten, Seriennummern, Datum sowie Barcode-/QR-Ausgabe | P3 |
| F19 | Wiederverwendbare Grafikbibliothek mit Vorschau und Einfügen | P2 |

### Bilder, Layout und Verschachtelung

Referenz: [Bildoptionen](https://docs.lightburnsoftware.com/2.2/Reference/ImageOptions/), [Nachzeichnen](https://docs.lightburnsoftware.com/2.2/Reference/TraceImage/), [True Nest](https://docs.lightburnsoftware.com/2.2/Reference/Nesting/TrueNest/), [Neuheiten 2.2](https://docs.lightburnsoftware.com/2.2/NewFeatures/).

| ID | Zu liefernde Fähigkeit | Etappe |
|---|---|---|
| F20 | Bildkorrektur, Masken, Zuschneiden, Invertieren, Auflösung und verarbeitetes Bild exportieren | P2 |
| F21 | Umriss- und Mittellinien-Vektorisierung; mehrere Bilder verarbeiten | P2–P3 |
| F22 | Rechteckbasiertes und konturbasiertes Nesting einschließlich Abständen und Drehlimits | P2–P3 |
| F23 | Nesting über mehrere Platten und Nutzen von Innenräumen; Materialrichtung berücksichtigen | P3 |
| F24 | 3D-Modelle importieren und daraus Tiefenbilder erzeugen; STL/3MF/AMF/OBJ/OFF/PLY als Formatziele | P3 |
| F25 | Vorlagen/Generatoren und externe Gestaltungstools einbinden; Anbieterzugang separat prüfen | P3 |

### Bearbeitung und Werkzeugwege

Referenz: [Cut Settings](https://docs.lightburnsoftware.com/2.2/Reference/CutSettingsEditor/), [gemeinsame Parameter](https://docs.lightburnsoftware.com/2.2/Reference/CutSettingsEditor/SharedSettings/), [Ausgabeoptimierung](https://docs.lightburnsoftware.com/2.2/Reference/OptimizationSettings/), [Vorschau](https://docs.lightburnsoftware.com/2.2/Reference/Preview/).

| ID | Zu liefernde Fähigkeit | Etappe |
|---|---|---|
| F26 | Linien-, Flächen-, Konturfüllungs- und Bildgravurmodus | P1–P2 |
| F27 | Ebenen und Unteroperationen mit Sollleistung, Geschwindigkeit, Durchgängen und Ausgabeschaltern | P1–P2 |
| F28 | Schnittfugenkompensation, Haltestege, Perforation und Ein-/Auslaufoptionen | P2 |
| F29 | Scanwinkel/-abstand, Kreuzschraffur, Überlauf, Scanrichtung und Punktkorrektur | P2 |
| F30 | Rasterverfahren einschließlich Dithering, Schwellenwert und Graustufen; konkrete Algorithmen bei Spezifikation abgleichen | P2 |
| F31 | Reihenfolge nach Ebene, Gruppe und Priorität; Innenkonturen und Gravur vor Außenschnitt | P1–P2 |
| F32 | Startpunkte, Wegrichtung, Leerfahrten und Mehrfachbearbeitung optimieren | P2 |
| F33 | Animierte Ausgabevorschau mit Leerwegen, Zeitnavigation und berechneter Dauer | P1–P2 |
| F34 | Z-Versatz/-Schritte, Luftunterstützung und verzögerte Schaltvorgänge bei geeigneter Hardware | P4/H |

### Material, Kalibrierung und Maschinenbedienung

Referenz: [Materialbibliothek](https://docs.lightburnsoftware.com/2.2/Reference/MaterialLibrary/), [Materialtests](https://docs.lightburnsoftware.com/2.2/Reference/MaterialTest/), [Laserfenster](https://docs.lightburnsoftware.com/2.2/Reference/LaserWindow/), [Funktionsübersicht](https://docs.lightburnsoftware.com/2.2/Reference/).

| ID | Zu liefernde Fähigkeit | Etappe |
|---|---|---|
| F35 | Materialprofile anlegen, zuweisen/verknüpfen, bearbeiten und Bibliotheken zusammenführen | P1–P2 |
| F36 | Testmuster für Material, Fokus und Scanintervall mit reproduzierbaren Parametern | P2/H |
| F37 | Maßstab, Scanversatz, Nullpunkt und Maschinenprofil kalibrieren; Konfiguration sichern | P0–P2/H |
| F38 | Verbindung, Zustand, Alarme, Referenzfahrt, Schrittbewegung und gespeicherte Positionen | P0–P1/H |
| F39 | Absolute/relative Ausgabe, Arbeitsnullpunkt, Auswahlursprung, Rahmenfahrt und Auswahl anfahren | P1/H |
| F40 | Start, Pause/Fortsetzen, Stop, Fortschritt, Datenübertragung und Geräte-Dateiverwaltung soweit vorhanden | P1–P3/H |
| F41 | Geräteeinstellungen, Controllerinformationen, Konsole und Makros mit nachvollziehbarem Verlauf | P2/H |
| F42 | Netzwerktransport, Geräteerkennung und Bridge-Workflow abhängig vom BlazerX-Protokoll | P3/H |
| F43 | Direkter 2-%-Punkt für Position/Fokus gemäß README; diese Funktion stammt zusätzlich aus Mathias’ Auftrag | P1/H |

### Kameras, Zubehör und Schnittstellen

Referenz: [Kameras](https://docs.lightburnsoftware.com/2.2/Reference/Cameras/), [Rotary](https://docs.lightburnsoftware.com/2.2/Reference/RotaryMode/), [Neuheiten](https://docs.lightburnsoftware.com/2.2/NewFeatures/), [öffentliche REST-Spezifikation](https://github.com/LightBurnSoftware/LightBurn-REST/blob/main/docs/openapi.yaml), [Funktionsübersicht](https://docs.lightburnsoftware.com/2.2/Reference/).

| ID | Zu liefernde Fähigkeit | Etappe |
|---|---|---|
| F44 | Kameraauswahl, Linsenkalibrierung, Arbeitsraumausrichtung und Hintergrundaufnahme | P3/H |
| F45 | Mehrkamera-Overlay und Kopfkamera, zusammengesetzte Aufnahmen | P3–P4/H |
| F46 | Print-and-Cut mit manueller sowie kameraunterstützter Registrierung | P3/H |
| F47 | Rotary-Konfiguration und Tests, Zylinderkorrektur und konische Abwicklung | P4/H |
| F48 | Wiederholjobs, Kacheln/Split Marking und Materialvorschub bei geeigneter Mechanik | P4/H |
| F49 | Galvo-Linsenkorrektur, Pulse/Frequenz/MOPA, externe Achsen und Doppelquellen als Spezialmodule erfassen | P4/H |
| F50 | LiDAR-/Höhenkarten, Oberflächenkompensation und Autofokus nach Hardwarefähigkeit | P4/H |
| F51 | Gerber-/Bohrdaten für PCB-Arbeiten importieren | P3 |
| F52 | Lokale API für Status, Projekt-/Parameterzugriff und Werkzeugintegration | P3 |
| F53 | Einstellungen exportieren/importieren, Diagnosepaket und Fehlerberichte mit überprüfbarem Inhalt | P2–P3 |

## Änderungen in 2.2.01 berücksichtigen

Das aktuelle Update betrifft besonders Gerätebereitschaft, Konfigurationsübernahme und Kameraausrichtung. F40/F49 müssen Bereitschaftssignale, Quellenparameter, Leistungskennlinien und Eingangsbelegungen korrekt abbilden, wenn die jeweilige Hardware sie hat. Die Abnahme umfasst Geräteende während Upload, Kamerawiederverbindung und korrekte Ereignisflanken von Start-/Stop-Eingängen; daraus folgen keine neuen BlazerX-Hardwareannahmen. Referenz: [Änderungen 2.2.01](https://docs.lightburnsoftware.com/2.2/NewFeatures/#2201).

## Grenzen der Gleichwertigkeit

LightBurn-spezifische Lizenzen, Cloud-/Partnerzugänge und proprietäre Serviceanbindungen werden nicht als eigene Dienste ausgegeben. Ihr jeweiliger Arbeitsablauf wird als Integrationsziel erfasst. Blazer Studio erhält eigene Projektdateien und Einstellungen; ein Import vorhandener LightBurn-Dateien braucht einen separat getesteten Konverter und einen Verlustbericht.

Funktionsgleichheit wird je ID anhand konkreter Beispielprojekte, Import-/Exportprüfungen, Geometrie-/Werkzeugwegtests und Simulator-/Gerätetests beurteilt. Sichtbare UI-Schalter allein gelten nicht als Implementierung. Nicht ausführbare Hardwareoptionen bleiben im Fähigkeitenbericht erklärbar.

## Erneuter Windows-Abgleich am 21.11.2026

Der offizielle [latest-Index](https://release.lightburnsoftware.com/LightBurn/Release/latest/) liefert weiterhin **LightBurn 2.2.01**, Windows-Datei `LightBurn-v2.2.01.exe`, Zeitstempel **2026-11-02 22:32:05 UTC**. Verglichen wurde die öffentliche stabile Ausgabe, keine unveröffentlichte Beta. Die [2.2-Referenz](https://docs.lightburnsoftware.com/2.2/Reference/) ist der aktuelle Detailindex. Der Implementierungsvergleich bezieht sich auf **c1fc186**, nicht auf fremde uncommittete Gerätebibliothek-/Versionsarbeiten und nicht auf die installierte Anwendung.

Der Familienkatalog F01–F53 bleibt erhalten. Er ist keine 53-Punkte-Checkliste, die bereits durch jeweils einen Schalter erfüllt wäre. Für die priorisierte nächste Arbeit siehe [Abnahmematrix](FUNCTION_ACCEPTANCE.md#priorisierte-lücken-nach-abgleich-am-21112026); reale Controllerfamilien stehen im [Mehrgeräteplan](MULTI_DEVICE_PLAN.md#controllerfamilien-gegen-lightburn-2201).

Besonders leicht zu verwechselnde Referenzfunktionen:

- [Startmodi und Ursprung](https://docs.lightburnsoftware.com/2.2/Reference/CoordinatesOrigin/): absolute Koordinaten, aktuelle Position und Benutzerursprung mit neun Ankern; Galvo verwendet absolute Koordinaten. G90/G54 allein erfüllt F39 nicht.
- [Klassisches Print and Cut](https://docs.lightburnsoftware.com/2.2/Reference/PrintAndCut/): zwei reale Zielpositionen bestimmen Versatz, Drehung und optional Maßstab; korrekte Referenz/Positionsmeldung und absolute Ausgabe sind Voraussetzung. Ein transformiertes Hintergrundfoto ist keine solche Ausgabeausrichtung. Der manuelle Blazer-Teilumfang transformiert inzwischen echte Ausgabewege; Grenzen und Abnahme stehen im [manuellen Print-and-Cut-Vertrag](MANUAL_PRINT_AND_CUT.md).
- [Kamera-Print-and-Cut](https://docs.lightburnsoftware.com/2.2/Reference/CameraPrintAndCut/): kalibrierte Kopfkamera, mindestens drei nicht kollineare Marken, automatische Markensuche und Ausgabezuordnung. Ein manuelles Vierpunktfoto erfüllt dies nicht.
- [Barcodes](https://docs.lightburnsoftware.com/2.2/Reference/BarCode/): unter anderem Code 128, Code 93, Data Matrix, EAN, UPC, PDF417 und QR sowie variable Inhalte und formatspezifische Optionen. QR plus CSV ist ein Teilumfang, keine Barcode-Parität.
- [Migration](https://docs.lightburnsoftware.com/2.2/Guides/Migration/): `.lbzip`-User-Bundles enthalten Einstellungen, Hotkeys, Presets, Geräte und Bibliotheken. Projekte sowie SHX-/Systemschriften brauchen gesonderte Übernahme. Blazer Studio muss unterstützte Inhalte, Konflikte und Verluste erklären; fremde LightBurn-Lizenzdaten werden nicht zu einer Blazer-Lizenz.

Alle Quellen sind offizielle LightBurn-Primärquellen, geprüft am 21.11.2026. Die Quellen beschreiben das Zielprodukt; sie belegen weder die eigene Umsetzung noch reale Betriebssicherheit.
