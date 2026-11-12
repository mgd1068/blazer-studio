# Funktionsabnahme F01 bis F53

Stand: 12.11.2026. Der versionierte [Zielkatalog](LIGHTBURN_SCOPE.md) bleibt das Gesamtziel. Diese Bestandsaufnahme verknüpft vorhandene Teilimplementierungen mit konkreten Nachweisen und nennt die verbleibende Arbeit. Sie ist kein Nachweis vollständiger LightBurn-Gleichwertigkeit und kein Freigabevermerk für 1.0.

Ein Testname belegt nur seinen beschriebenen Fall. **Teilumfang** bedeutet vorhandene Implementierung mit begrenzter Prüfung; **offen** bedeutet eine noch zu liefernde Fähigkeit oder fehlende Detailabnahme. Hardwaregebundene Einträge bleiben mit dem Gerätebezug sichtbar. Das vorhandene M3-Profil ist XY/GRBL; nicht eingerichtete Achsen oder Quellen werden nicht durch simulierte Schalter ersetzt.

## Projekte und Gestaltung

| ID | Vorhandener Umfang und Nachweis | Verbleibende Arbeit |
|---|---|---|
| F01 | Projektdateien, Namen/Notizen, eigene Vorlagen, lokaler Dateiverlauf, Undo/Redo und Dateirundlauf: `projectMetadataHistoryAndRoundtrip`, `projectMetadataValidationIsAtomic`, `projectMetadataReadsLegacyWithoutNotes`, `recentProjectsPersistBoundAndDeduplicate`, `projectTemplateIsIndependentAndKeepsEmbeddedContent`, `workflow` | Softwarefälle geprüft; native Desktop-/externe Bedienung bleiben T08. Format 8 und Vorlagen sind im [geprüften beta.3-Kandidaten](beta/CANDIDATE_BETA3.md) enthalten |
| F02 | Benannte Blätter, aktive Ausgabe und Verschachteln über Blätter: `operationManagementAndMultiSheet` | Blattkopien und sämtliche blattübergreifenden Bearbeitungsfälle spezifizieren |
| F03 | Undo/Redo, atomare Sicherung und Recovery: `stability`, `modelAndCleanHistory` | Sichtbaren Verlauf und praktische Wiederherstellungsfälle am Desktop abnehmen |
| F04 | SVG, DXF-Teilmenge, PDF/AI/EPS über Inkscape: `vectorImportsAndExport`, `pdfLightBurnAndVectorClip` | HPGL/PLT, GCode-Geometrie und aufgeführte Austauschformate implementieren; Importkorpus verbreitern |
| F05 | Qt-Rasterimporte, SVG- und GCode-Export: `raster`, `workflow` | Pluginabhängige Rasterformate einzeln abnehmen; Export verarbeiteter Bilder vervollständigen |
| F06 | Begrenzte LightBurn-Geometrieübernahme: `pdfLightBurnAndVectorClip` | Bibliotheken und weitere Projektvarianten mit Verlustbericht prüfen; keine verlustfreie Migration zugesagt |
| F07 | Auswahl, numerische Transformationen und Einrasten: `groupsAndTransforms`, `selectionRevealAndNumericalSize` | Systemzwischenablage, Zollbedienung und Hilfslinien gegen Zielumfang prüfen/ergänzen |
| F08 | Basic/Advanced, Themen, feste Shortcuts, Einführung und Hilfe: `ui`, `workflow` | Anpassbare Bedienfelder/Shortcuts, weitere Sprache und Druckausgabe fehlen zur vollständigen Familie |
| F09 | Grundformen, Linien/Bézier und Maße: `transformationsAndNodes`, `geometryAndFrame` | Interaktives Messen und breiteren Zeichen-/Geometriekorpus abnehmen |
| F10 | Knoten, Verbinden/Schließen/Zerlegen/Vereinfachen: `transformationsAndNodes` | Toleranzbasierte Reparatur und schwierige offene/selbstschneidende Pfade prüfen |
| F11 | Pfadvereinfachung vorhanden: `src/editing.cpp` | Systematische Dubletten- und Geometrieprüfung fehlt zur vollständigen Familie |
| F12 | Vereinigung/Differenz/Schnitt und Versatz: `booleansAndAlignment` | Trim-/Schnittwerkzeuge und degenerierte Geometrien vervollständigen/abnehmen |
| F13 | Noch kein belegter vollständiger Umfang für diese Familie | Radien, Tangentialkreise, Konturhüllen und Slot-/Laschenanpassung spezifizieren und implementieren |
| F14 | Ausrichten, Verteilen, Spiegeln und flache Gruppen: `groupsAndTransforms`, `booleansAndAlignment` | Sperren und Zeichenreihenfolge vollständig gegen den Zielkatalog abnehmen |
| F15 | Rechteckmuster und Mehrfachkopien: `qrAndProduction` | Kreismuster, Kopien entlang eines Pfads und Zweipunkttransformation ergänzen |
| F16 | Pfadkonvertierung/Transformation als Grundlage: `src/editing.cpp` | Verformen/Verzerren und Rückwandlung von Ausgabewegen fehlen zur vollständigen Familie |
| F17 | Systemschrift, Stil, Zeichenabstand und Pfadtext: `src/job.cpp` | Schrift-/Ausrichtungs-Referenzkorpus und editierbaren Text nach den zugesagten Umformungen prüfen |
| F18 | CSV, Seriennummern und QR: `qrAndProduction`, `batchExportIsDetachedAndNeverOverwrites` | Barcode, Datums-/Platzhalterdetails und Seriengrenzen vollständig spezifizieren/abnehmen |
| F19 | Keine belegte wiederverwendbare Grafikbibliothek | Vorschau, Einfügen, Persistenz und Fehlerfälle implementieren |

## Bilder und Werkzeugwege

| ID | Vorhandener Umfang und Nachweis | Verbleibende Arbeit |
|---|---|---|
| F20 | Korrektur, Maskierung, Zuschnitt, Drehen/Spiegeln: `imageModesAndTrace` | Export des verarbeiteten Bildes sowie Referenzen für alle Korrekturen vervollständigen |
| F21 | Flächen-/Kontur-Vektorisierung vorhanden: `imageModesAndTrace` | Mittellinien-Verfahren und Mehrbildbedienung ergänzen |
| F22 | Heuristisches konturbezogenes Nesting: `qrAndProduction` | Komplexe Kollisions-/Abstands-/Drehfälle mit unabhängig geprüfter Geometrie abnehmen |
| F23 | Mehrere Platten/Blätter: `operationManagementAndMultiSheet` | Innenräume und Materialrichtung separat abnehmen; heuristische Suche bleibt gekennzeichnet |
| F24 | STL-/OBJ-Oberseite als Tiefenkarte: `src/modelimport.cpp` | Weitere Zielformate und höhere Modellrasterung als 512 Pixel fehlen |
| F25 | Einzelne Generatoren und externes Inkscape-/Fototiefenwerkzeug | Durchgängige Vorlagen-/Generator-/Werkzeugintegration spezifizieren |
| F26 | Konturen, schraffierte Flächen und Raster: `job`, `raster` | Konturfüllung und die Detailoptionen aller Modi gegen konkrete Referenzen prüfen |
| F27 | Zwei Basisoperationen und bis zu 64 Operationen/Mehrfachzuweisung: `operationPassesAndPages` | Ausgabeschalter, Grenzfälle und gespeicherte Reihenfolge anhand Produktionsdateien abnehmen |
| F28 | Schnittfuge, Haltestege und Ein-/Auslauf: `scanAndCutFeatures` | Perforation und vollständige Variantenabnahme ergänzen; reale Kerf-/Schnittqualität offen |
| F29 | Winkel, Intervall, Kreuzschraffur, Überlauf und manueller Rückrichtungsversatz | [Inline-S-Fix](QA_REPORT.md) geprüft; reale Scan-/Punktkorrektur und geometrische Genauigkeit offen |
| F30 | Floyd-Steinberg, Atkinson, Schwellenwert und Graustufen: `referenceDither`, `imageModesAndTrace` | Zielalgorithmen im Detail abgleichen und reale Bildqualität reproduzierbar prüfen |
| F31 | Gravur vor Schnitt, Prioritäten und heuristische Innenreihenfolge: `orientationAndCutOrdering` | Echte verschachtelte Konturen, Gruppen und Gegenbeispiele statt bloßer Begrenzungsgrößen abnehmen |
| F32 | Startpunkte und begrenzte Wegeoptimierung: `scanAndCutFeatures` | Richtungen, Leerfahrten und Mehrfachbearbeitung mit unabhängig erwarteten Pfaden vergleichen |
| F33 | Berechnete Pfadvorschau und animierter Simulator: `workflow` | Zeitnavigation und gemessener Dauervergleich fehlen zur vollständigen Vorschaufamilie |
| F34 | Keine freigegebene automatische Z-/Air-Assist-Steuerung | Im M3-Profil nur XY; Schalt-/Achsenfunktionen benötigen passenden Hardware-/Protokollnachweis |

## Materialien und Gerät

| ID | Vorhandener Umfang und Nachweis | Verbleibende Arbeit |
|---|---|---|
| F35 | Persistente Profile, Filter, Kopien und unveränderliche Projektübernahme: `completeManufacturerLibrary`, `appliedMaterialSnapshot` | Bibliotheken zusammenführen und Verknüpfungs-/Versionsfälle vervollständigen |
| F36 | Materialmatrix und manuelle Fokus-/Intervallproben | Reproduzierbare Qualitätsabnahme; Herstellerdurchgänge bleiben unvollständig, wenn die Quelle sie nicht nennt |
| F37 | Manuelle Maß-/Scanparameter, G54 und vorläufiges Referenzfoto | Maschinenkonfiguration sichern/wiederherstellen, unabhängige Maß- und Scanabnahme ergänzen |
| F38 | Serielle Verbindung, Alarme, Homing und Jog: `grbl` | Gespeicherte Positionen und kontrollierte reale Fehlerfallabnahme offen |
| F39 | G90/G54, Arbeitsnullpunkt und laserfreie Rahmenfahrt; physische Offsetgrenzen geprüft | Relative/auswahlbezogene Startursprünge und Auswahl anfahren vervollständigen |
| F40 | Eingefrorene Jobs, RX-Kredite, Pause/Stop und neue Idle-Bestätigung: `grbl`, `workflow` | Neuer Treiberstand am Gerät, USB-/KVM-Fälle und Gerätespeicher soweit vorhanden abnehmen |
| F41 | Ausgelesene Konfiguration und begrenzter Ereignisverlauf | Bedienbare Konsole/Makros sowie Settings-Rundlauf mit eindeutigen Zustandsregeln fehlen |
| F42 | Serielle Geräteerkennung vorhanden | Netzwerktransport und Bridge benötigen geklärtes Herstellerprotokoll; nicht eingerichtet |
| F43 | Expliziter begrenzter 2-%-Puls: `finiteFocusAndAlarm`, `focusInterlocks` | Optische Wirkung und Abschaltung am Gerät offen; Host kann Abschaltung bei verlorenem Transport nicht garantieren |

## Kamera und Schnittstellen

| ID | Vorhandener Umfang und Nachweis | Verbleibende Arbeit |
|---|---|---|
| F44 | Manuelles Referenzfoto/Vierpunktregistrierung: `registrationAndInvalidProjects`; externe Webcamversuche | Kameraauswahl, Live-Aufnahme, Linsenmodell und reproduzierbare persistente Ausrichtung integrieren/abnehmen |
| F45 | Keine integrierte Mehr-/Kopfkamera | Passenden Aufbau und Zusammensetzung spezifizieren; eine externe Webcam erfüllt diese Familie nicht |
| F46 | Projektive Bildregistrierung als Grundlage | Manuellen und kameragestützten Print-and-Cut-Ausgabeablauf implementieren und unabhängig prüfen |
| F47 | Rotary am vorhandenen XY-Profil nicht eingerichtet | Gerätefähigkeit/Zubehör zuerst belegen; Rotations-/Konusverfahren nicht als vorhanden ausgeben |
| F48 | Manuell wiederholbare Jobs und Serien-GCode | Kacheln/Split Marking und mechanischer Vorschub nicht eingerichtet; konkrete Anwendbarkeit klären |
| F49 | M3-Profil bietet keine Galvo-/MOPA-/Doppelquellenfunktionen | Für dieses Geräteprofil nicht freigegeben; weitere passende Hardware als eigener Ausbau |
| F50 | Keine Höhen-/LiDAR-/Autofokus-Messhardware eingerichtet | Manuell eingestellter Fokus ist kein Ersatz für Messung oder automatische Nachführung |
| F51 | Kein belegter PCB-Datenimport | Gerber-/Bohrdaten mit klar definierten unterstützten Varianten und atomarem Fehlerverhalten implementieren |
| F52 | Keine lokale Projekt-/Parameter-API | Lokale Schnittstelle einschließlich Zugriffsschutz, Zustandsregeln und Werkzeugtests implementieren |
| F53 | Lokaler Diagnoseexport mit fester Feldliste: `release` | Einstellungsimport/-export und überprüfter Wiederherstellungsablauf vervollständigen |

## Weiterarbeit und 1.0

Für eine 1.0-Zusage im gesamten Zielumfang sind die verbleibenden geräteunabhängigen Funktionen zu liefern und ihre Detailabnahmen zu schließen. Nicht vorhandene Hardware bleibt mit konkreter Begründung sichtbar. Die Tabelle wird pro abgeschlossenem Arbeitspaket mit Revision und Nachweis aktualisiert. Eine Bestandsaufnahme schließt keine Funktionslücke.

Die unmittelbare Produktionsreife-Arbeit beseitigt zuerst falsche Ausgabe, Zustandsfehler und Datenverlust, danach Bedienungs-/Auslieferungsprobleme. Die nächste Funktionsarbeit folgt dieser Tabelle in überprüfbaren Paketen. Reale [Betriebs- und Materialabnahmen](HARDWARE_ACCEPTANCE.md) und externe Bedienung bleiben zusätzliche Bedingungen, auch wenn ein Softwaretest besteht.
