# Begrenzte Arbeitsnullpunkt-Transaktion (offline geprüft)

Das Backend setzt G54-X/Y auf null, ohne Bewegung anzufordern. Es erteilt weder
Homing-, Fahrraum-, Geräte- noch Hardwarefreigaben. Reale M3-Inbetriebnahme bleibt
offen; die positiven Tests verwenden ausschließlich explizite virtuelle Belege.

`ControllerStatus.coordinateUpdatePending` ist nur während dieses Ablaufs wahr.
Das ist keine allgemeine Ausnahme für verlorene Positionsnachweise. Die Fassade
muss vor Eintritt und währenddessen **jede tatsächlich aktive Outputlease**,
Job-/Fokusflags und gemeldetes/angefordertes Firing ausschließen. Die Session
kennt keine Zubehörlease. Machine-/UI-Integration wird separat geprüft.

| Phase | Vertrag |
| --- | --- |
| Eintritt | Frisches verified Idle/S0, Referenz, privater Motionvertrag, rohe Position innerhalb bestätigtem signed Fahrraum; keine Queue, Pause, Fokus, Homing, Reset oder ungeklärte Overridefolge. Auch nicht gesendete serielle Bytes sperren. |
| ACKs | Rohe MPos und vorheriger WCO werden eingefroren; Phase ist vor erstem Write/changed sichtbar. Sequenziell `M5 S0`, `G21`, `G90`, `G10 L20 P1 X0 Y0`, `G54`, `$G`. Nur eine Quittierung pro tatsächlich gesendetem Befehl. |
| Modal-Readback | Die angeforderte `$G`-Antwort muss vor ihrem ACK eindeutig G54/G21/G90 enthalten. Fehlende, doppelte oder unerwartete Modalantworten scheitern. |
| Positionsabgleich | Nach allen ACKs werden vorherige RX-Beobachtungen abgearbeitet. Ein alter Teilbericht am RX-Rand scheitert. Erst im nächsten Eventloop-Schritt wird eine eigene `?`-Abfrage gestellt; alte Idle-Berichte im letzten ACK-Paket bestätigen nichts. |
| Abschluss | Danach frisch beobachtetes Idle/S0 mit explizitem gültigem WCO, WPos 0 und konsistentem rohem MPos bzw. WPos+WCO. Rohe MPos bleibt gleich, WCO entspricht diesem Punkt. Erst dann Phase beenden, ready/verified wieder ermöglichen. |
| Fehler | Bewegung, anderer WCO, Run/Hold/Home, Ausgang ungleich 0, fehlende/ungültige Daten, Einheiten-/Konfigurations-/Firmwarewechsel, Zusatz-ACK, Fehler/Alarm, Timeout oder Disconnect entwerten Phase und Referenz. Kein Retry, Home oder Resume. Expliziter Stop nutzt den vorhandenen Resetpfad. |

Es gilt eine unabhängige Gesamtfrist von fünf Sekunden ab Eintritt. ACKs und
Statusverkehr können sie nicht verlängern. Ein Epochentoken verhindert, dass
ein bereits geplanter Readback-Callback nach Stop/Reset eine neue Transaktion
beeinflusst. Laufende Job-/Frame-/Jog-/Fokusaktionen werden durch Zero nicht
unterbrochen oder ausgenommen; der Zero-Eintritt wird verweigert. Während Zero
starten solche Aktionen nicht, und Pause/Resume/Overrides werden abgewiesen.

Die Positionsvergleiche verwenden rohe Berichtswerte mit lediglich 0,000001 mm
Rechentoleranz, keine Rundung auf GCode-Zielpräzision. Das ist eine Aussage über
**gemeldete Zahlen**, keine gemessene mechanische Stillstandstoleranz. Vor G10
ist nur alter WCO zulässig; nach dessen Dispatch nur alter oder erwarteter neuer
WCO bis zum Abgleich. Ein neuer WPos-Nullwert kann eine geänderte MPos nicht
legitimieren. Fehlt MPos, braucht die Prüfung WPos und expliziten WCO aus
demselben Bericht.

Akzeptierte Echtzeit-Overrides halten Zero gesperrt, bis ein frisches gültiges
Ov mit dem zuletzt angeforderten Ziel und leerem Host-Schreibpuffer beobachtet
wurde. Auch das setzt den bereits separat gesperrten privaten, geordneten
Overridevertrag voraus; es ist kein Beleg gegen die dokumentierte
Standard-GRBL-Flagzusammenfassung.

GRBL-Status besitzt keine Query-ID. Die RX-/ACK-Grenze beweist eine geordnete
Protokollbeobachtung nach der Abfrage, keine unabhängige Frische der realen
Sensorik oder vertrauenswürdige Firmwareausführung. Alte oder manipulierte
Controllerdaten können nicht durch Hostsoftware physisch verifiziert werden.
Ein Firmwarebanner, `$22=1`, ACK oder vorheriger Simulatorpunkt reicht nicht.

Die PTY-Tests prüfen signed/verschobenes G54, MPos und WPos+WCO, verzögerte ACKs,
alte/fehlende Readbacks, RX-Teilberichte, falsche Modalantworten, Totaldeadline,
Stop/Reset/Fault/Disconnect, laufende Aktionen und ausstehende Overrides.
Die unabhängige kinematische Pflanze prüft wörtliche physische Sollpunkte und
unveränderte Travelanzahl; G10 verändert nur den virtuellen WCO. Ihr extern
verschobener Kopf muss auch bei WPos0 faulten. Kein realer Anschluss, Laser,
Geräteprofilgrant oder Live-App-Zustand wurde verwendet.


## Machine integration evidence

The integrated facade rejects work-zero before dispatch while any output lease,
job/focus flag, firing or other controller transaction is active. The expected
coordinate-readback exception checks the actual accessory lease as well as both
facade flags; it cannot exempt an emitting stream. Work-zero is busy but does not
appear as a running/paused laser job. Independent Machine/PTy regressions in
`grbl` cover delayed positive readback, signed coordinates, facade denial,
raw-session misuse, lease acquisition/lost flags, errors, timeout, stop and startup.
The complete independent GRBL run on e3529a0 passed 199 cases; this is software
protocol/model evidence, not physical commissioning.
