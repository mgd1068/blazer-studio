# Koordinatenvertrag vor realer Ausgabe

Stand 21.11.2026 nach einer vom Nutzer gemeldeten Kollision. Alle hier
beschriebenen Prüfungen sind offline bzw. gegen synthetische PTYs erfolgt.
Es gibt keinen neuen Hardwaretest und keine Freigabe für reale Bearbeitung.

## Datenfluss und konkrete Lücken

Der Editor und `JobMove` verwenden Millimeter mit Ursprung links unten.
Der Encoder wählt G21, G90 und G54 und gibt absolute XY-Ziele aus. Ohne
initiale Leerfahrt fügt er eine laserfreie Fahrt zum ersten `from` ein.
Die tatsächliche aktuelle Kopfposition stammt dagegen aus dem Controller.
Sie kann vom Modellursprung und vom ersten `from` abweichen.

Ein GRBL-Status-WCO ist der Gesamtversatz des aktiven Arbeitskoordinatensystems,
einschließlich weiterer wirksamer Offsets. Es gilt bei konsistentem Zustand
`MPos = WPos + WCO`. Die bisherige Prüfung addierte WCO zum Modellpunkt und
verglich mit 0..200/0..150. Der positive Maschinenraum war aber nie nachgewiesen;
Homingquittierung beweist weder dieses Vorzeichen noch die G54-Zuordnung.
Ein anderer aktiver Koordinatenrahmen kann die Bedeutung von WCO ändern.
Eine einzelne Positionsmeldung beweist keinen mechanisch sicheren Fahrraum.

Die neue Sperre verlangt einen separaten Maschinen-/Modalvertrag. Reale
Sessions erhalten ihn derzeit nie; es existiert kein öffentlicher Setter,
kein Profilflag und kein aus einer Datei übernehmbarer Freigabewert. Nur die
befreundete PTY-Testklasse kann einen expliziten synthetischen Vertrag injizieren.
Die frühere positive Annahme ist damit aus dem realen Ausgabepfad entfernt.
Die Homing-Protokollbestätigung bleibt als eigenständiger Zustand erhalten.

Zusätzliche Korrekturen:

- Vor Start werden tatsächliche Position und **beide** Endpunkte jedes Segments
  gegen den synthetisch/ künftig nachweislich definierten Maschinenraum geprüft.
  Ein `rapid.from` dient nur der Modellvorschau und wird nicht ausgegeben;
  hierfür zählt der tatsächlich gemeldete Kopfstand.
  Das schließt die vom Encoder ergänzte erste Anfahrt ein.
- Vollständige Positions-/Offset-/Ausgangsberichte verfallen nach 1,5 Sekunden.
  Alte Idle-Meldungen autorisieren keine neue Bewegung oder Emission.
- Widersprüchliche MPos/WPos/WCO-Berichte und unerwartete WCO-Änderungen
  verwerfen Referenz und Auftrag. Ein ausdrücklich angefordertes G10 wird
  separat behandelt und verlangt anschließend neue Positions-/Offsetdaten.
- Normaler Befehlsversand ist privat; Aufrufer können die Bewegungsprüfung
  nicht mit beliebigen `commands()` umgehen.
- Fehler/STOPP verwerfen noch lokal gepufferte serielle Ausgabedaten vor dem
  bestmöglichen Feed-Hold/Reset. Bereits vom Controller empfangene Befehle
  lassen sich damit nicht rückwirkend zurückholen. Keine automatische Fortsetzung.
- Unterbrochene Bearbeitungssegmente ohne verbindende Leerfahrt werden schon
  beim Encodieren abgelehnt; sonst würde das nächste Ziel eine unerwünschte
  Verbindung gravieren. M8/M9 werden nicht aus einem Profilfingerprint geraten.

## Noch erforderlicher Inbetriebnahmevertrag

Eine zukünftige Freigabe muss als eigener, überprüfbarer Workflow entstehen,
gebunden an konkrete Geräteinstanz, Endpunkt, Controlleridentität, Firmware
und relevante Einstellungen. Ein identisches Profil allein verbindet keine
Evidenz mit einer anderen physischen Maschine.
Mindestens erforderlich sind nachgewiesene Achsrichtungen, vorzeichenbehaftete
physische Min-/Max-Grenzen mit Sicherheitsabstand, Homing-/Pull-off-Verhalten,
und die Zuordnung von Modell/G54 zum tatsächlichen Maschinenraum. Homing allein,
Status 0/0 oder identische Profilmaße reichen nicht.

Der Vertrag muss außerdem festlegen, wie aktives G54, G92 und weitere Offsets
vor einer Ausgabe frisch gelesen und gegen den verwendeten WCO abgeglichen
werden. Eine spätere Modusänderung darf keine bereits geprüfte Transformation
überleben. Mechanische Referenz, aktuelle Positionsfrische und geprüfter
Koordinatenvertrag müssen getrennte Zustände bleiben. Reset, Firmware- oder
Konfigurationswechsel müssen die entsprechende Evidenz entwerten. Zubehör- und
Absaugnachweise sind zusätzlich erforderlich und keine Koordinatenkalibrierung.

Erst ein implementierter, unabhängig geprüfter Vertrag kann die derzeitige
Sperre ersetzen. Keine frei importierbare Eigenschaft darf hierfür genügen.

## Offline-Nachweise

`tests/grbl_test.cpp` prüft insbesondere die Sperre trotz Homing, unveränderte
Simulatorbedienung, veraltete Idle-Daten, negative Maschinenkoordinaten mit
explizitem synthetischen Rahmen, die erste Anfahrt, widersprüchliche Berichte,
unvermittelte Offsetänderung und Fehler bei gepuffertem Versand.
`tests/controlleradapter_test.cpp` prüft laserfreie erste Anfahrt, fehlende
Zubehörannahmen und die Ablehnung unterbrochener Bearbeitungspfade.
Diese Tests belegen Softwareverhalten unter den expliziten Fixtureannahmen;
sie bestimmen keine Koordinaten oder Verdrahtung eines realen Lasers.
