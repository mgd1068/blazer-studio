# Funktionsumfang: Referenz LightBurn 2.2.01

Stand und Recherche: 09.11.2026. Verbindliches Produktziel aus Mathias’ Auftrag: den Funktionsumfang der aktuellen LightBurn-Version als Referenz übernehmen, mit eigener OrcaSlicer-artiger Linux-Oberfläche und BlazerX als erstem Gerät.

## Referenz und Geltung

Der offizielle [Release-Index](https://release.lightburnsoftware.com/LightBurn/Release/) verweist mit `latest` auf **2.2.01**; die [Downloadliste](https://release.lightburnsoftware.com/LightBurn/Release/latest/) nennt den 02.11.2026 für die Installationsdateien. Der [2.2-Changelog](https://docs.lightburnsoftware.com/2.2/NewFeatures/#2201) enthält die Änderungen seit 2.2.00. Die Ausgangsbasis umfasst Core und Pro, einschließlich neuer beziehungsweise experimenteller Funktionen. Native Linux-Unterstützung endete bei LightBurn nach 1.7; Blazer Studio wird nativ für Linux entwickelt. Quelle: [LightBurn 2.0 / Systemkompatibilität](https://lightburnsoftware.com/blogs/news/lightburn-2-0-00-new-shapes-edit-nodes-submenu-dark-mode-and-more).

Dies ist ein versionierter Zielkatalog, keine Behauptung bereits implementierter Gleichwertigkeit. Alle Einträge sind offen. Vor Implementierung einer Funktionsfamilie werden ihre Detailoptionen und Abnahmetests gegen die verlinkte Referenz spezifiziert. Veröffentlichungen nach dem Stichtag ändern den Umfang nicht stillschweigend; spätere Vergleiche werden separat dokumentiert.

Geräteunabhängige Funktionen gehören zum Zielumfang. Hardwarefunktionen werden auf dem BlazerX umgesetzt, soweit Controller und Zubehör sie ermöglichen. Unbekannte Fähigkeiten bleiben offen; nicht zutreffende Funktionen erhalten eine dokumentierte Begründung, statt aus dem Katalog zu verschwinden. Die Unterstützung sämtlicher anderer Laserhersteller ist ein gesonderter Ausbau und wird durch dieses erste BlazerX-Projekt nicht als bereits beauftragt oder vorhanden ausgegeben.

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
