# Entwicklungsplan

Stand: 09.11.2026. Zielumfang: **LightBurn 2.2.01 einschließlich Pro-Funktionen** als funktionale Referenz, native Linux-Oberfläche nach OrcaSlicer und BlazerX als erste Geräteintegration. Siehe [Zielmatrix F01–F53](LIGHTBURN_SCOPE.md) und [recherchierte Zusatzvorschläge Z01–Z13](USER_WISHES.md).

Die nachstehenden Etappen bestimmen die Reihenfolge. Der erste Prototyp ist keine Begrenzung des Gesamtziels. Entwurf 0.4 implementiert bereits Editor-Grundfunktionen, SVG-Layoutimport, Projektdateien und Simulator; keine Termine zugesagt. Zusätzliche Z-Funktionen bleiben Vorschläge bis zu ihrer Übernahme.

## P0 – Gerätebestand und Architektur

- Exakte BlazerX-Variante, Arbeitsraum, Controller, Firmware, Zubehör und Anschluss erfassen; bestehende LightBurn-Konfiguration lesen.
- Protokoll, Koordinaten, Einheiten, Referenzfahrt, Leistungsskalierung und Abschaltung prüfen.
- Fähigkeitenmatrix erstellen: unterstützt / nicht vorhanden / ungeklärt, jeweils mit Beleg.
- C++20, Qt 6/QML und CMake aufsetzen. Objekt-/Projektmodell, Berechnungen und Geräteadapter trennen.
- Früh berücksichtigen: Seiten, stabile Objekt-IDs, Gruppenhierarchie, versionierte Materialprofile und unveränderliche Jobstände. Parametrik als Erweiterung entwerfen.

## P1 – Bedienbarer Kern und direkte Steuerung

- Vorbereiten · Vorschau · Gerät; helle/dunkle Oberfläche, Arbeitsfläche, Auswahl, Transformationen und Objektbaum.
- Projektdatei, mehrere Arbeitsblätter, Undo/Redo, Autosave/Wiederherstellung.
- SVG-Kernimport, Grundformen, Ebenenparameter und erste Vektorwerkzeugwege mit Vorschau.
- Simulator für Verbindung, Positionierung, Status, Übertragung, Jobablauf und Fehlerfälle.
- Nach Protokollprüfung echte Verbindung und Bewegungen zunächst ohne Emission.
- Direkter 2-%-Positionier-/Fokuspunkt: Haltebedienung, Zeitlimit, Emissionsanzeige, Jobkonflikte und Wiederverbindungsfälle prüfen. Leistungsskalierung und geräteseitige Abschaltung müssen vor echten Tests geklärt sein.

## P2 – Vollständiger alltäglicher Laserworkflow

- DXF-/Rasterimport, erweiterte Pfad-/Knotenwerkzeuge, Text, Boolesche Operationen und Schnittfugen/Haltestege.
- Linien-, Füll-, Konturfüllungs- und Rastergravur, Scan-/Ditheroptionen und Unteroperationen.
- Reihenfolge, Startpunkte und Leerfahrten optimieren; Zeiten am realen Gerät vergleichen.
- Materialbibliothek, Material-/Fokus-/Intervalltests und Kalibrierungsassistenten.
- Migration vorhandener LightBurn-Projekte/-Bibliotheken untersuchen und mit Verlustberichten testen.
- Empfohlene Ergänzungen: Gruppen direkt bearbeiten, reichhaltige Profile, Bildmaßstab und nachvollziehbare Profilabweichungen.

## P3 – Erweiterte Gestaltung und Produktion

- Weitere Vektor-/Rasterformate, variable Texte, CSV-/Serienproduktion und Codes.
- Kontur-Nesting über mehrere Platten; Verformung, komplexe Muster, Vorlagen und Grafikbibliothek ausbauen.
- 3D-Import/Tiefenkarten und PCB-Datenimport.
- Kamerakalibrierung, Overlays und Print-and-Cut bei vorhandenem Zubehör.
- Lokale API, externe Tools und Netzwerk-/Bridge-Workflow abhängig vom Geräteprotokoll.
- Zusatzvorschläge getrennt einplanen: Parametrik/Generatoren, Warteschlange, Testbuch und Restflächenverwaltung.

## P4 – Hardwaregebundene Spezialmodule

- Rotary, Zylinder-/Konuskorrektur, Vorschub, Split Marking und zusätzliche Achsen.
- Galvo-/MOPA-/Mehrquellenfunktionen und Linsenkorrektur, LiDAR/Oberflächenkompensation und Autofokus.
- Je Funktion dokumentieren, ob sie beim BlazerX vorhanden, nachrüstbar oder nicht anwendbar ist. Keine stillschweigende Streichung aus der Vergleichsmatrix; weitere Controller erst als eigenen Ausbau planen.

## Abnahme und Auslieferung

- Jede F-ID mit Umsetzung, Einschränkungen und aussagekräftigen Nachweisen verfolgen; Zielgleichheit erst nach Prüfung beanspruchen.
- Importmaße, Geometrie, Raster-/Werkzeugwege, Koordinaten, Leistungswerte und Jobzustände mit unabhängigen Beispielen testen.
- Simulatorprüfungen um Verbindungsabbruch, Alarm, Stop und Wiederverbindung ergänzen; anschließend beaufsichtigte Gerätetests.
- Wayland/X11, HiDPI, Tastatur, Dezimalkomma und Linux-Paketierung abnehmen; Diagnose- und Wiederherstellungsweg dokumentieren.

Bestehende Schutzfunktionen bleiben erhalten. Ein Software-Stop ersetzt keinen Hardware-Not-Aus.

## Stand Entwurf 0.4

Erledigt: LightBurn-Profil gelesen (M3/GRBL, 200 × 150 mm, 115200 Baud, S-Max 1000, Fokuspunkt 2 %, Z aus), C++/Qt-Projekt gebaut, Editor mit einfachen Formen/Text und SVG-Grafiken, numerische Maße, zwei Bearbeitungen, Undo/Redo, eigene Projektdatei und Gerätebedienung im Simulator. Persistente bearbeitbare Materialbibliothek mit Toolkopffilter, einheitliche Koordinaten unten links, Mehrfachauswahl, Objektgruppen und numerische Auswahltransformationen sind ergänzt. Kern-, Qt-Quick-Bedien- und Katalogtests bestehen.

Ergänzt in 0.4: Controllerabfrage und Bewegung mit Laser-Aus am M3 bestätigt; Schnitt-/Füllpfade, Text-/SVG-Vektorgeometrie, Bild-Dithering, GCode und GRBL-Übertragung implementiert; Materialtest-Generator. Noch offen: reale Emissions-/Materialabnahme, erweiterte Import-/Zeichen-/Bildfunktionen, Kalibrierung, Autosave und Mehrseitenmodell. Die Übersicht ist kein Abschlussvermerk für ganze F-IDs. Details: [Prototyp](PROTOTYPE.md).

## Stand 0.5: geschichtete Reliefgravur

Eigener Reliefbereich mit Graustufen-Tiefenkarten, 1–256 Schichten, Umkehr/Gamma, Schichtmasken, relativer 3D-Vorschau und optionaler Abtragskalibrierung umgesetzt. Gleiche Pfade für GCode, Simulator und GRBL. Reale Eignung des M3 für fühlbare Kieselreliefs ist offen; keine automatische Foto-Tiefenrekonstruktion oder Z-Nachführung. Siehe [Relief](RELIEF.md).

## Stand 0.6: Editor, Produktion und Wiederherstellung

Großes Ausbaupaket nach dem Programmvergleich: Datensicherung, weitere Operationen und Mehrfachbearbeitung, Pfad-/Text-/Bildwerkzeuge, zusätzliche Importvarianten, Schnitt-/Scanoptionen, Nesting/Arbeitsblätter/CSV/QR, gepuffertes GRBL/Overrides sowie 16-Bit-/Modell-/Foto-Tiefenworkflow. Die genaue Tabelle [EXPANSION.md](EXPANSION.md) benennt verbleibende Format-, Qualitäts- und Hardwaregrenzen. Sie ersetzt keine vollständige Abnahme der F01–F53-Zielmatrix. Tatsächliche Emissions-/Materialprüfung und geeignete zusätzliche Hardware bleiben offen.


## Stabilisierung 0.8 und Beta-Ziel 0.9

0.8 ergänzt Hintergrundberechnung mit Abbruch/Generationenprüfung, bildbasierte Vorschau aus dem Rechenergebnis, eingefrorene CSV-Stapel, Dateisperren, atomare Backups, abgesicherten Autosave-Rückfall und zusätzliche Fehlerfalltests. [Implementierung und Grenzen](STABILIZATION_08.md). 0.9 benötigt anschließend reale Geräte-/Materialabnahme, ein geprüftes Downloadpaket und externe Bedienungstests; keine Spezialisierung auf Stein, sondern allgemeine Reliefgravur.
