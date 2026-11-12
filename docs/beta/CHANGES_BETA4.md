# Änderungen im Softwarekandidaten beta.4

Zielplattform bleibt Ubuntu 26.04 amd64. Dieser Stand wird als Softwarekandidat eingefroren. Genaue Quellrevision, Version und Paketabhängigkeiten stehen im mitgelieferten `BUILD-MANIFEST.json`; Prüfsummen in `SHA256SUMS`. Ein neuer Kandidat ist keine öffentliche Geräte- oder 1.0-Freigabe. Geräte-/Materialabnahme und beobachteter Einsteigerlauf bleiben nach dem [Testprotokoll](TEST_PROTOCOL.md) offen.

## Bedienung und Projekte

- Vollständige Blattkopien mit eigenen Kennungen und eindeutiger aktiver Ausgabe.
- Benannter Bearbeitungsverlauf mit atomaren Sprüngen, Undo/Redo und gespeichertem Inhaltsstand; höchstens 50 Schritte innerhalb der Sitzung.
- Native Zwischenablage einschließlich Bildern, Gruppen, Stil, Hilfsformen und eigenen Parametern. Einfügen an Originalposition oder mit Versatz; Textfelder behalten ihre eigenen Kürzel.
- Gespeicherte mm-/Zollbedienung für Längenfelder, Lineale und Fangraster. Geometrie und Ausgabe bleiben in Millimetern. Unverändert verlassene Felder behalten exakte Werte.
- Blattbezogene Hilfslinien und T1-/T2-Hilfsformen ohne Laserausgabe. Laserfreie Rahmenfahrt getrennt für Auftrag, Auswahl und Hilfsformen.

## Import und Bildexport

- Begrenzte HPGL/PLT-, nominale GCode-XY-, RDWorks-RD- und MillMage-/LightBurn-Geometrieimporte neben den vorhandenen Importwegen. Importierter GCode wird als Geometrie gelesen und niemals direkt ausgeführt.
- Native geteilte Punkt-/Primitivlisten, kompakte Linienzüge, korrigierte Bézier-Griffzuordnung und aufrechter Systemschrift-Ersatz. Native Projekte werden weiterhin nicht vollständig oder verlustfrei migriert.
- PNG/JPEG/BMP-Bildexport im Hintergrund, eingebettete Quelle oder verarbeitetes Bild. Atomare Speicherung und Abbruch bewahren bestehende Ziele. TIFF/WebP-Laufzeitmodule gehören zur Paketabhängigkeit.

## Gerätearchitektur und Ausgabe

Arbeitsraum, Profil und Ausgabe sind durchgängig verbunden. Neutrale XY-Bewegungspläne sind von registrierten Controller-Kodierern getrennt. Ausgabepuffer sind an Profil und Bewegungen gebunden; unbekannte Controller erhalten keinen GRBL-Ersatz. Der vorhandene M3-GCode bleibt unverändert. Herstellerdateien bleiben unverändert und erhalten keinen pauschalen Leistungs-Offset.

Bekannte abweichende GRBL-XY-Projektprofile sind offline bearbeitbar, simulierbar und exportierbar. **Echte Verbindungen bleiben ausschließlich für den bisherigen M3-Ausgabevertrag verfügbar.** Weitere Controller, Geräteverwaltung, Spezialachsen, Live-Kamera und deren reale Freigaben sind nicht geliefert. Die [Mehrgerätearchitektur](../MULTI_DEVICE_PLAN.md) bleibt ein Ausbauziel.

## Dateiformate und Update

Das unveränderte M3-Profil behält Projektformat 8. Hilfslinien verwenden Format 9, T1/T2 Format 10, abweichende Geräteprofile Format 11 einschließlich dieser Felder. Alte Dateien werden ausdrücklich als M3 gelesen, ohne automatische geometrische Skalierung. Vorlagen, Wiederherstellung und Verlauf bewahren neue Metadaten. Anschluss-/Zugangsdaten werden nicht in Projektprofile eingebettet.

Beta.3 kann Formate 9–11 nicht öffnen; beta.2 kann auch Format 8 nicht öffnen. Es gibt keinen verlustbehafteten automatischen Rückexport. Eigene Projektdateien und Benutzerdaten werden bei Paketupdate oder Entfernung nicht gelöscht. Vor einem Wechsel regulär speichern und das alte Fenster schließen.

Die neue Paketrevision benötigt eine eigene Installations-/Upgradeprüfung; frühere Nachweise des unveränderten beta.3-Kandidaten gelten nicht automatisch für beta.4. Verfahren: [Paketbau und Laufzeitprüfung](RELEASE_BUILD.md). Offene Funktionen und tatsächliche Abnahmen bleiben im [Produktionsplan](../PRODUCTION_READINESS.md) sichtbar.
