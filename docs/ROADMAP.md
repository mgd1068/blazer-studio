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
- Jobübertragung, Pause/Fortsetzen soweit vom Gerät unterstützt und Abbruch.

## 5. Alltagstauglichkeit

- Rastergravur und weitere Importformate nach Bedarf.
- Laufzeitschätzung anhand echter Messungen verbessern.
- Linux-Paketierung, nachvollziehbare Fehlerberichte und Dokumentation.

Keine Termine oder vollständige LightBurn-Funktionsparität zugesagt.
