# Manuelle Print-and-Cut-Ausgaberegistrierung

Softwareumfang: zwei ausdrücklich eingegebene Design-/Zielpunktpaare in kanonischen Millimetern, unveränderliche Quellbindung, Drehung/Versatz und optional ein gemeinsamer Maßstab. Der Dialog ist über **Print and Cut** erreichbar. Er prüft die tatsächlichen geplanten Ausgabewege, zeigt sie samt Zielmarken als Bild und verlangt anschließend eine ausdrückliche Übernahme. Es gibt keine automatische Gerätebewegung, Laser-/Fokusaktion oder Kameramessung.

## Bezug zur Referenz

Die [LightBurn-2.2-Dokumentation](https://docs.lightburnsoftware.com/2.2/Reference/PrintAndCut/) beschreibt zwei korrespondierende Marken, Zentren einzelner Vektoren/Gruppen, optional proportionale Skalierung und absolute Ausgabe. Werkzeugmarken dürfen ohne Ausgabe bleiben. LightBurns physischer Ablauf benötigt korrekte Referenz und Positionsmeldung; seine Anforderungen werden durch manuelle Zahleneingaben nicht nachgewiesen. Geprüfte Quelle: 21.11.2026.

Blazer definiert sein Verfahren ausdrücklich: Punkt 1 wird exakt zugeordnet, die Richtung Punkt 1→2 bestimmt die Drehung. Ohne Skalierung bleibt die Originalgröße erhalten und der Abstandsunterschied erscheint als Zweitpunktfehler. Mit Skalierung ist der einheitliche Faktor Zielabstand/Designabstand. Keine Spiegelung, Scherung oder projektive Verzerrung. Die offizielle Anleitung legt keine vollständige numerische Implementierung oder Fehlerschwelle offen; dieses Verfahren ist kein Nachweis bitgenauer LightBurn-Übereinstimmung.

Die lokale Mindestbasis beträgt 10 mm an beiden Punktpaaren. Der Nutzer begrenzt den zulässigen Zweitpunktfehler auf 0–10 mm (Standard 0,5 mm). Diese Grenzwerte sind Blazer-Softwarepolitik, keine Zusage physischer Genauigkeit. Weit getrennte Referenzen und eine unabhängige reale Kalibrierung bleiben erforderlich.

## Bedienung und wirksame Ausgabe

1. Ausgabegeometrie und Bearbeitungsparameter vorbereiten. Referenzmarken nach Bedarf als T1/T2 belassen. Eine aktive Auswahlmaske schränkt weiterhin nur die auszugebenden Grafiken ein.
2. Im Dialog zwei eindeutige Designpunkte und ihre absoluten Zielkoordinaten eingeben. Auswahlzentrum übernimmt das Zentrum einer sichtbaren Vektorgeometrie oder vollständig ausgewählten Gruppe; Bild-/Tiefenkarten sind dafür ungeeignet. Die Zielpunkte sind manuelle Daten im ausdrücklich benannten Offline-G54-Intent-Rahmen, keine gelesenen Maschinenpositionen.
3. Ohne Skalierung oder proportionale Skalierung wählen und **Registrierte Ausgabe prüfen** betätigen. Rohwerte unterstützen mm/in. Ungültige, nicht endliche, negative oder außerhalb der Profilfläche liegende Eingaben sperren die Prüfung; sie fallen beim Fokuswechsel nicht auf einen alten Wert zurück.
4. Ausgabewege, Zielmarken, Drehung, Maßstab und Fehler ansehen. Anschließend ausdrücklich übernehmen. Die Aktivierung wählt Absolute Koordinaten; Zeichnungsobjekte bleiben unverändert.
5. Vorschau/Simulation, normaler GCode-Export und Auftragsrahmen konsumieren denselben registrierten `Document.job()`-Plan. Nur die geplante Ausgabe wird transformiert. T1/T2, ausgeblendete/anderes-Blatt-Objekte und deaktivierte Bearbeitung werden nicht aktiviert. Auswahl-/Hilfsrahmen sind während aktiver Registrierung gesperrt, damit keine unregistrierten Editorgrenzen als Ausgabefläche erscheinen.

Ein ausdrücklich bekannter Vorschaukopf wird separat mit Leistung 0 an den ersten registrierten Ausgabepunkt geführt. Ohne Kopfstand bleibt die Anfahrt als unbekannt gekennzeichnet; der bestehende Encoder erreicht den ersten Ausgabepunkt mit abgeschalteter Leistung. Es wird kein bekanntes 0/0 erzeugt. Offline-Export benötigt keine Simulatorverbindung. Die explizite Punktprüfung und Übernahme berechnen einen begrenzten Quellplan derzeit synchron und sind bei Geräteaktion, laufender Simulation, Berechnung/Export oder aktivem SynchronousWork-Guard gesperrt. Die normale Ausgabeplanung verwendet anschließend die vorhandene Hintergrundpipeline. Es wird keine blockierende Controllerabfrage eingeführt.

Die Profilfläche wird für alle registrierten Endpunkte einschließlich leistungsfreiem Rasterüberlauf und bekannten Anfahrten geprüft. Geschwindigkeit, Leistung, Layer und Slice bleiben erhalten; Verbindungsgravuren werden abgewiesen. Vor einem gültigen Review oder Plan prüft die Registrierung zusätzlich den tatsächlichen ControllerProgram des Ausgabeadapters. Codierungsfehler einschließlich nach außen gerundeter Wire-Koordinaten sperren damit schon Review/Übernahme, nicht erst Dateischreiben oder Start. Der allgemeine Adapter definiert sein Ausgaberaster; eine nicht ausgegebene erste rapid.from-Koordinate wird dadurch nicht zum falschen Ablehnungsgrund.

## Bindung, Änderungen und Dateigrenzen

Leichte generische Document-/Kopfbenachrichtigungen prüfen nur unveränderliche COW-Identitäten, Blatt, Auswahl und Frame; sie lösen keine Geometrie-/Serialisierungsschleife in diesem Callback aus. Die verbindliche Inhaltsprüfung liegt im Commit und Planbau.

Der private SourceSnapshot bindet Ausgabeprofil, expliziten Offline-Koordinatenrahmen, vollständige geometrische/Processing-Eingabe einschließlich Hilfsmarken, aktive Seite und wirksame Auswahlmaske an den kanonischen Bewegungsplan. Ein sitzungsbezogener Frame-Zähler widerruft auch bei erneutem Öffnen identischer Bytes frühere Zuordnungen. Reviewtoken gehören genau zur geprüften Quelle und Anfahrt, werden bei Änderung ungültig und nach Übernahme verbraucht. Im Hintergrund gelangen nur Wertkopien in die begrenzte/coalescierte JobPreparation; alte Generationen dürfen keinen früheren oder unregistrierten Plan veröffentlichen.

Änderungen an Quelle, Auswahlmaske oder Profil lassen eine aktive Zuordnung **gesperrt** stehen; es gibt keinen stillen absoluten Ersatzplan. Neu prüfen/übernehmen oder ausdrücklich zurücksetzen. Ein Profil-/Koordinatenrahmen eines anderen Dokuments erfüllt die Bindung nicht. Gleichbleibende Metadaten oder ein bloßer bekannter Kopfstand ändern nicht automatisch die Registrierung.

Die Registrierung ist sitzungsbezogen und wird bewusst nicht in Projektdateien, Vorlagen, Recovery oder Undo-Snapshots gespeichert. Öffnen/Neues Projekt/Verlaufswechsel entfernen sie; ein Hinweis nennt die erneute Punktprüfung. Bei zuvor relativem Start ist die Wahl von Absolute ein normaler Undo-Schritt, der keine Laufzeitzuordnung wiederherstellt. Alte Projektformate 8–14 und ihre bisherigen Standarddaten bleiben unverändert.

CSV-Stapel- und Materialtest-Export werden bei aktiver Registrierung vor Dateiseiteneffekten ausdrücklich abgewiesen. Geometrie-/SVG-Exporte bleiben Zeichnungsdaten und enthalten diese sitzungsbezogene Ausgabeausrichtung nicht. Der Materialprüfungs-Key bindet die aktive Transformation plus eingefrorenen Processing-/Quellkontext. Eine andere Skalierung setzt die bestehende Bestätigung zurück; ein bloßer Kopfwechsel/`rapid.from` erzeugt keine neue Materialanforderung und keine automatische Freigabe.

## Nachweise und weiter offener Umfang

`manualregistration` prüft unabhängige Literalgeometrie und GCode für 90° und Skalierung, Feed/Power, gecachte alte Ausgabe, Fehler-/Abstands-/Bounds-/Überlaufgrenzen, exakte Pfadänderung bei gleichem BBox, Profil-/Framebindung, echten Offline-Export ohne Kopf, Source-/Tokenstaleness, Undo/Save/Open, aktive Auswahlmasken und veraltete asynchrone Ergebnisse. Ein echter HardwareDisabled-Machine-/Simulatorfall prüft registrierte Anfahrt und eingefrorenen laufenden Plan. Reale Main-Fälle normal/hell/mm und kompakt/dunkel/in prüfen Tastatur, Rohfelder, Einheitendraft, explizite Übernahme, Scale-Proofreset und stabilen Key bei Kopfwechsel.

Offen bleiben reale Positions-/Referenzaufnahme und Inbetriebnahme, automatische Bewegung zu Marken, Kopfkamera-Markensuche, Livekamera-/Linsen-/Arbeitsraumausrichtung, weitere Gantry-Controller/DSP-Ausgabe, vollständiger Workflow für größere Designs außerhalb der derzeitigen Editorfläche und unabhängige reale Genauigkeitsabnahme. Der manuelle Softwareabschnitt ist weder vollständiges F46 noch F44 und keine reale Maschinenfreigabe.
