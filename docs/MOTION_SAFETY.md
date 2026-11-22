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

Auch die Referenzfahrt selbst bleibt ohne einen **eigenen Homingvertrag**
gesperrt. `$22=1` und ein plausibler Idle-/Alarmbericht belegen weder korrekt
verdrahtete Endschalter noch richtige Homing-Richtung, Geschwindigkeiten oder
Pull-off-Verhalten. `homingSafetyError` meldet den fehlenden Nachweis im Backend
und an die Oberfläche. Ein zukünftiger separater Einrichtungsablauf muss diese
Evidenz an Geräteinstanz, Endpunkt, Controller und geprüfte Parameter binden;
gewöhnliche Produktionsbedienung darf keine solche Freigabe erfinden.
Unbestätigte reale Sessions erhalten keinen Homingvertrag und können kein `$H`
senden. Die [sessionbezogene Einrichtung](M3_SESSION_COMMISSIONING.md) kann nach
frischem vollständigem Readback und ausdrücklicher mechanischer
Operatorattestierung eine einzelne Einrichtungsreferenzfahrt zulassen. Reset, Trennen, Störung und Alarm/Door entwerten diesen Vertrag.
Befreundete PTY-Fixtures können unabhängig davon synthetische Homingevidenz
bereitstellen; sie ersetzt weiterhin keine Freigabe des G54-/Maschinenraums.

Zusätzliche Korrekturen:

- Vor Start werden tatsächliche Position und **beide** Endpunkte jedes Segments
  gegen den synthetisch/ künftig nachweislich definierten Maschinenraum geprüft.
  Ein `rapid.from` dient nur der Modellvorschau und wird nicht ausgegeben;
  hierfür zählt der tatsächlich gemeldete Kopfstand.
  Das schließt die vom Encoder ergänzte erste Anfahrt ein.
- Vollständige Positions-/Offset-/Ausgangsberichte verfallen nach 1,5 Sekunden.
  Alte Idle-Meldungen autorisieren keine neue Bewegung oder Emission.
- Ein gemeldeter Wechsel weg von G54 entwertet den Modalvertrag. Eine spätere
  Rückmeldung G54 stellt die alte Evidenz nicht wieder her; während eines
  laufenden Auftrags wird die Übertragung abgebrochen.
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

## Inbetriebnahmevertrag

Die sessionbezogene Freigabe entsteht als eigener, überprüfbarer Workflow,
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

`tests/grbl_test.cpp` prüft insbesondere die Homingsperre trotz `$22=1` und
Idle/Alarm, das Verfallen synthetischer Homingevidenz nach Stop, Trennen,
Neustart und Fehler, die Ausgabesperre trotz synthetisch erlaubtem Homing sowie unveränderte
Simulatorbedienung, veraltete Idle-Daten, negative Maschinenkoordinaten mit
explizitem synthetischen Rahmen, die erste Anfahrt, widersprüchliche Berichte,
unvermittelte Offsetänderung und Fehler bei gepuffertem Versand.
`tests/controlleradapter_test.cpp` prüft laserfreie erste Anfahrt, fehlende
Zubehörannahmen und die Ablehnung unterbrochener Bearbeitungspfade.
Diese Tests belegen Softwareverhalten unter den expliziten Fixtureannahmen;
sie bestimmen keine Koordinaten oder Verdrahtung eines realen Lasers.


## Unabhängige virtuelle Maschine

`grblplant` verwendet `tests/virtualgrblplant.h` als eigenen begrenzten
GCode-Interpreter. Physische XY-Position, gemeldete Zähler und G54-/G55-Versatz
sind getrennte Zustände. Der Interpreter übernimmt keine Encoder-, Planer-,
Geometrie- oder Grenzprüfungsfunktionen aus dem Produkt. Er modelliert G20/G21,
G90/G91, G0/G1, G54/G55, G10 L20 P1, den hier benutzten Jog-Teilumfang und
M3/M4/M5. Seine Fahrtereignisse enthalten tatsächliche Start-/Endposition und
Emissionszustand. Außerhalb liegende Ziele laufen nur bis zum ersten Schnitt
mit der harten physischen Grenze und lösen einen virtuellen Alarm aus.
Nicht unterstützte oder fehlerhafte Wörter werden ohne Teilwirkung abgewiesen.

Ein bewusst möglicher Gegenfall setzt den physischen Kopf auf (80,60), meldet
aber Zähler (0,0). `G0 X0 Y0` bewegt diesen Kopf nicht zum physischen Ursprung;
der erste 200-mm-Schnitt versucht (280,60) und erreicht die harte Grenze bei
(200,60). Das ist ein ausführbares Gegenbeispiel zur Sicherheitsannahme
„gemeldet null bedeutet physisch null“, **keine Rekonstruktion der tatsächlichen
M3-Firmware oder der gemeldeten Kollision**. Der simulierte Reset mit verlorenen
Zählern ist eine ausdrückliche Fixtureannahme.

Über einen echten Unix-PTY läuft der unveränderte Produktionsadapter gegen
diese virtuelle Maschine. Ohne Inbetriebnahmevertrag bleiben Job/Jog/Rahmen/
Arbeitsnull/Fokus ohne physische Pfadbewegung; auch explizites Homing ohne
separaten Homingvertrag verändert die virtuelle physische Position nicht.
Nur mit privat injizierter synthetischer Homingevidenz wird im positiven
Testfall virtuell referenziert; dies bleibt allein weiterhin unzureichend. Mit ausschließlich
privat injizierter Testevidenz folgen Rechtecke den unabhängig festgelegten
physischen Eckpunkten, einschließlich voller positiver und negativer
200×150-mm-Räume und eines verschobenen G54. Der tatsächliche Startpunkt liegt
jeweils außerhalb der Figur und wird laserfrei angefahren. Zusätzlich werden
160 deterministische Zufallssegmente auf Verlauf und Emissionszustand geprüft.
Fehlerhafte Statusberichte, unsichere Versätze und unerwartete Modalwechsel
geben keinen neuen Pfad frei; ein Wechsel während gepufferter Übertragung
verhindert weiteres Streaming.

Das Modell ist kinematisch und begrenzt. Es bildet weder Beschleunigung,
Mechanik, tatsächliche Endschalter, UART-Zeitverhalten, Planner-Latenz noch
optische Leistung ab. Es kann bereits vom Controller empfangene Befehle nicht
rückgängig machen und beweist keine Not-Aus-Wirkung. Kein reales Gerät und
keine reale serielle Schnittstelle wurden für diese Tests geöffnet.

## Active evidence loss

Machine now stops immediately when verified position/reference evidence is lost
while a stream, focus or accessory lease is active. The active flags must not be
cleared before deciding whether STOPP is required. Nested session notifications
observe the new evidence boundary first. The watchdog also checks an executing
session's motion evidence independently of accessory flags. This is software
fail-closed behavior, not proof of physical shutdown.

## Begrenzter Vorfallkontext

Der read-only [technische Controllerkontext](CONTROLLER_INCIDENT_DIAGNOSTICS.md)
zeichnet angeforderte Präambel/Erstanfahrt, Protokollquittierung, Resetgrenzen,
Vorprüfungsablehnungen und begrenztes numerisches Readback auf. Er bewahrt
keinen vollständigen Werkzeugweg und keine Anschluss-/Projekttexte. Diese
Instrumentierung verändert keine Freigabe und ist kein physischer
Bewegungsnachweis; bewusster lokaler Export bleibt eine eigene UI-Integration.

The adapter marks explicit homing and reset/handshake phases separately from
normal execution. Expected loss of reference during homing invalidates old
accessory evidence without interrupting that separately commissioned procedure.
A reset wait must never provoke repeated STOPP/reset writes every watchdog tick.
Active output leases and reported emission still require unconditional handling.

An adapter-initiated host reset faults and releases the Machine's active lease
flags without requesting another reset in its notification callback. A mere
handshake after an unsolicited startup is not a host-reset-pending boundary;
active output still requires protective handling there. The typed `resetting`
flag denotes the host reset wait, not the configuration-check phase.

Resume remains conservative about extraction, including a paused non-emitting
frame. An exception needs an explicit immutable non-emission operation type and
fresh reported zero output; missing output-lease flags alone must never grant
such an exception. Jog-cancel/resume semantics also need separate controller
validation. These are remaining workflow limitations, not new output permissions.

## Reset is not a physical stillstand proof

The host's refusal to generate movement does not prove that controller firmware
will remain stationary. Standard GRBL invokes stored startup blocks when its
startup path is not alarm-locked; see the official [protocol implementation](https://raw.githubusercontent.com/gnea/grbl/master/grbl/protocol.c).
This is a firmware behavior reference, not an observed M3 startup diagnosis.
Initial connection and protective soft reset therefore also require separately
verified boot/reset behavior and stored `$N` blocks during real commissioning.
No startup blocks were read or changed on the physical device in this work.
Software STOPP remains best effort, not a physical emergency stop or an optical
shutdown guarantee.
