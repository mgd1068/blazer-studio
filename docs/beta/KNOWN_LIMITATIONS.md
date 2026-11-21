# Bekannte Einschränkungen des Testkandidaten

**Realer Fehler vom 20.11.2026 in der lokalen beta.5.2-local:** Ein Auftrag begann am tatsächlichen Kopfstandort statt am Figurenanfang und fuhr am Ende der ersten Linie in die Begrenzung. Der Gehäuselüfter blieb aus. Diese Fassung ist damit nicht als sicherer Hardwarebetrieb bestätigt. Die nachfolgende Referenzsperre ist zunächst nur offline getestet und behebt den ungeklärten Lüfter-/Koordinatenvertrag nicht vollständig. Keine Wiederholungsfahrt zur Fehlersuche.

- Im Entwicklungszweig beta.6-dev existiert der M3-Diagnoseadapter. Reale Bewegung einschließlich Homing, Fokus und Job ist ohne nachgewiesene Koordinaten-/Zubehörverträge gesperrt; ein produktiver Inbetriebnahmeworkflow fehlt. Abweichende bekannte GRBL-XY-Projekte sind offline nutzbar; eine Beschreibung ist keine Freigabe weiterer Laser.
- Neue Metadaten benötigen entsprechend neue Projektformate: Hilfslinien/Hilfsformen/Profile 9–11, Randgravur 12, Start-/Ursprungsabsicht 13, ausgewählte Ausgabe/Bezugswahl 14. Ältere Programmfassungen können neuere Formate nicht öffnen; Laufzeitbestätigungen werden nicht gespeichert.
- Erste reale Kraftpapier-Kontur-/Rasterversuche am M3 sind dokumentiert. Vollständige Betriebs-, Schnitt- und Reliefabnahme sowie externe Bedienungstests stehen aus; der fast durchgehende Papierschnitt benötigte mechanische Hilfe. Siehe [Geräteprotokoll](../HARDWARE_ACCEPTANCE.md).
- Die Herstellerprofile beziehen sich auf M3 Pro. Fehlende Gravur-Durchgänge und das anfängliche Raster enthalten ausdrücklich eigene Arbeitswerte.
- Die Steuerung des Gehäuselüfters ist nicht nachgewiesen; M8 führte trotz Quittierung zu keinem vom Nutzer bestätigten Lüfterlauf.
- Importierte Bilder/Tiefenkarten behalten höchstens 4096 Pixel je Seite. STL-/OBJ-Modelle werden derzeit auf höchstens 512 Pixel rasterisiert.
- Begrenzte Importvarianten, heuristisches Nesting und keine verlustfreie LightBurn-Projektmigration. Native Grafiken erfordern eine Verlustvorschau und werden zunächst als Vorlagen eingefügt.
- Grafikbibliothek: eigene einzelne Bibliothek mit Kontur-/PNG-Kopien. Farb-/Strichdarstellung und parametrisierte Text-/Filterbearbeitung werden ausdrücklich nicht erhalten. Mehrdatei-Workflow, Drag-and-drop und `.lbart`-Migration fehlen.
- Reale Live-Overrides benötigen zusätzlich einen separat belegten Echtzeit-Verarbeitungsvertrag; Standard-GRBL kann wiederholte Overridebytes zusammenfassen. Virtuelle Einzelbyte-Tests erteilen keine solche Freigabe.
- Job-/Rasterberechnung und CSV-Stapel laufen im Hintergrund. Import, Dateischreiben und einzelne Advanced-Editoraktionen können weiterhin die Oberfläche aufhalten.
- Höchstens 500000 Bewegungen pro Job, zusätzliche Raster-/Dateibudgets. Überschreitungen werden abgelehnt.
- Abbruch ist kooperativ; interne Qt-Geometrieaufrufe sind nicht mitten in ihrer Ausführung unterbrechbar.
- Eine feste FHD-Webcam wird extern für Prüfaufnahmen verwendet; ein integriertes abgenommenes Live-Kamerasystem fehlt. Rotary, Z-/Oberflächenvermessung und separate Absaugsteuerung sind nicht freigegeben.
- Ein Prozessabsturz während CSV-Veröffentlichung kann einen vollständigen Teilstapel hinterlassen. Bestehende Ausgaben werden beim Wiederholen nicht überschrieben.
- Die erste Paketplattform ist Ubuntu 26.04 x86_64. Unterstützung weiterer Systeme folgt erst nach Prüfung.

Der eingefrorene beta.4-Kandidat enthält noch eine nachträglich im virtuellen Controller nachgewiesene Verbindungslücke bei direkter Zerstörung einer aktiven Sitzung. Der beta.5-Kandidat führt diesen Fall durch die vorhandene Schließlogik; der [beta.4-Bericht](CANDIDATE_BETA4.md) nennt die genaue Grenze. Der neue Kandidat benötigt seinen eigenen Liefernachweis; reale Geräte- und Einsteigerabnahme bleiben erforderlich.
