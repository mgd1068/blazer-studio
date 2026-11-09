# Entwicklungsplan

## 1. Gerät und vorhandene Umgebung erfassen

- Exakte BlazerX-Identität, Arbeitsraum, Controller und Firmware dokumentieren.
- Vorhandene LightBurn-Gerätekonfiguration und Anschlussart untersuchen.
- Dokumentation und Kommunikationsprotokoll prüfen; keine ungeprüften Steuerbefehle senden.
- Einheiten, Koordinaten, Referenzfahrt, Leistungsskalierung und Schutzfunktionen erfassen.

## 2. Bedienbarer Prototyp

- Qt/QML-Projekt mit den Bereichen Vorbereiten, Vorschau und Gerät.
- Arbeitsfläche, Objektliste, Einstellungsbereiche und Profile.
- Simulierter Geräteadapter mit Status und Jobfortschritt.
- Direkte Bedienung einschließlich 2-%-Positionier-/Fokuspunkt mit Halten/Loslassen, Zeitlimit und sichtbarem Emissionsstatus im Simulator.
- Bediengefühl gemeinsam am OrcaSlicer-Vorbild prüfen.

## 3. Vorbereitung und Vorschau

- Klar definierten SVG-Teilumfang importieren; nicht unterstützte Elemente sichtbar melden.
- Verschieben, Skalieren, Drehen, Undo/Redo und Projektdatei.
- Schnitt-/Gravurebenen und Materialprofile.
- Werkzeugwege, Fahrbewegungen und Reihenfolge visualisieren.

## 4. Geräteintegration

- Adapter für das tatsächlich ermittelte BlazerX-Protokoll.
- Verbindung, Status, Verbindungsabbruch und kontrolliertes Wiederverbinden.
- Positionierung und Rahmenfahrt zunächst ohne Laseremission.
- Grenzen und Leistungsskalierung validieren, anschließend beaufsichtigte Materialtests.
- Manuellen 2-%-Positionier-/Fokuspunkt nach Prüfung der Leistungsskalierung, Verriegelungen und geräteseitigen Abschaltmöglichkeiten integrieren.
- Zustandswechsel, Loslassen, Zeitlimit, Jobkonflikte und Verbindungsfehler gezielt prüfen; nach Wiederverbindung keine erneute Emission.
- Schrittbewegungen, Arbeitsnullpunkt und Rahmenfahrt; weitere Funktionen nur nach ermittelten Gerätefähigkeiten.
- Jobübertragung, Pause/Fortsetzen soweit vom Gerät unterstützt und Abbruch.

## 5. Alltagstauglichkeit

- Rastergravur und weitere Importformate nach Bedarf.
- Laufzeitschätzung anhand echter Messungen verbessern.
- Linux-Paketierung, nachvollziehbare Fehlerberichte und Dokumentation.

Keine Termine oder vollständige LightBurn-Funktionsparität zugesagt.
