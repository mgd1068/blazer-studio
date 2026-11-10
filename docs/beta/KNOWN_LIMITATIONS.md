# Bekannte Einschränkungen des Testkandidaten

- Vollständige Geräte-/Materialabnahme am vorhandenen Blaze X M3 und externe Bedienungstests stehen aus. Ein erfolgreicher Simulatorlauf ist keine reale Ergebnisprüfung.
- Die Herstellerprofile beziehen sich auf M3 Pro. Fehlende Gravur-Durchgänge und das anfängliche Raster enthalten ausdrücklich eigene Arbeitswerte.
- Die Steuerung des Gehäuselüfters ist nicht nachgewiesen; M8 führte trotz Quittierung zu keinem vom Nutzer bestätigten Lüfterlauf.
- Importierte Bilder/Tiefenkarten behalten höchstens 4096 Pixel je Seite. STL-/OBJ-Modelle werden derzeit auf höchstens 512 Pixel rasterisiert.
- Begrenzte Importvarianten, heuristisches Nesting und keine verlustfreie LightBurn-Projektmigration.
- Job-/Rasterberechnung und CSV-Stapel laufen im Hintergrund. Import, Dateischreiben und einzelne Advanced-Editoraktionen können weiterhin die Oberfläche aufhalten.
- Höchstens 500000 Bewegungen pro Job, zusätzliche Raster-/Dateibudgets. Überschreitungen werden abgelehnt.
- Abbruch ist kooperativ; interne Qt-Geometrieaufrufe sind nicht mitten in ihrer Ausführung unterbrechbar.
- Kamera, Rotary, Z-/Oberflächenvermessung und separate Absaugsteuerung sind ohne nachgewiesene Hardware nicht freigegeben.
- Ein Prozessabsturz während CSV-Veröffentlichung kann einen vollständigen Teilstapel hinterlassen. Bestehende Ausgaben werden beim Wiederholen nicht überschrieben.
- Die erste Paketplattform ist Ubuntu 26.04 x86_64. Unterstützung weiterer Systeme folgt erst nach Prüfung.
