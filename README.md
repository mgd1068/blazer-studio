# Blazer Studio

Geplante Linux-Anwendung zum Vorbereiten und Steuern von Laserjobs auf Mathias’ BlazerX. Die Oberfläche orientiert sich an Aufbau und Bediengefühl von OrcaSlicer.

**Status:** Projektentwurf. Noch keine ausführbare Anwendung und keine verifizierte Geräteanbindung.

## Ziel

Ein angenehmer, moderner Arbeitsablauf für Gravieren und Schneiden: Werkstücke vorbereiten, Werkzeugwege prüfen und Jobs am Gerät ausführen. Eigenständige Implementierung; keine Portierung von LightBurn und keine Übernahme seines Quellcodes oder seiner Assets.

## Bedienkonzept

- Arbeitsbereiche **Vorbereiten · Vorschau · Gerät**.
- Große maßstäbliche 2D-Arbeitsfläche mit Raster, Zoom und direkter Objektbearbeitung.
- Geräte- und Materialprofile sowie Objektliste links; Bearbeitungsparameter rechts.
- Einstellungen in den Stufen **Einfach / Erweitert / Experte**.
- Schnitt- und Gravurebenen mit Leistung, Geschwindigkeit und Durchgängen.
- Vorschau mit Bearbeitungsreihenfolge, Fahrbewegungen und später geschätzter Laufzeit.
- Helles und dunkles Design, skalierbare Darstellung und Tastenkürzel.

## Technischer Vorschlag

C++ mit Qt 6 / Qt Quick (QML), CMake als Buildsystem. Dieser Stack ist ein Vorschlag aus der Projektbesprechung; die konkrete Versionierung und Architektur werden bei Implementierungsbeginn geprüft. Rust bleibt eine mögliche Alternative.

Editor, Jobmodell, Werkzeugwegerzeugung und Geräteadapter sollen getrennt sein. Ein simulierter Geräteadapter ermöglicht die Entwicklung der Oberfläche vor der Hardwareanbindung.

## BlazerX-Unterstützung

Hersteller, genaue Modellvariante, Controller, Firmware, Transport und Befehlsprotokoll sind noch zu ermitteln. Insbesondere ist **GRBL-Unterstützung nicht bestätigt**. Keine Kompatibilitätszusage vor der Bestandsaufnahme und den Gerätetests.

## Erste Ausbaustufe

SVG-Import, Objekttransformationen, Ebenenparameter, Werkzeugwegvorschau und simulierter Jobablauf. Danach schrittweise echte Verbindung, Status, Positionierung, Rahmenfahrt, Jobübertragung und Abbruch.

Bestehende Schutzfunktionen müssen erhalten bleiben. Erste Kommunikation und Bewegung werden ohne Laseremission geprüft; reale Bearbeitung folgt nach Validierung der Einheiten, Grenzen und Leistungsskalierung. Software-Abbruch ersetzt keinen Hardware-Not-Aus.

Die nächsten Schritte stehen in [docs/ROADMAP.md](docs/ROADMAP.md).

## Lizenz

Noch nicht festgelegt. Das Repository wird zunächst privat geführt.
