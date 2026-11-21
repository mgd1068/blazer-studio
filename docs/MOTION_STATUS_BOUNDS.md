# Gemeldete Maschinenposition und physischer Fahrraum

Der GRBL-Adapter unterscheidet angeforderte GCode-Ziele und empfangene
Positionsdaten. Ziele werden passend zum dreistellig erzeugten GCode geprüft.
Eine gemeldete Maschinenposition wird dagegen **nicht gerundet**: MPos ist
maßgeblich; fehlt MPos, wird der rohe WPos-Wert mit dem bekannten Gesamt-WCO
addiert. Auch ein Überstand kleiner als eine Ausgaberundungsstufe bleibt eine
Bereichsverletzung. Es wird kein positiver Maschinenursprung angenommen.

Die sofortige Statusprüfung gilt nur mit separat bestätigtem vorzeichenbehaftetem
Maschinenraum, Modalvertrag, mm-Berichten, bekannter aktueller XY-/WCO-Information
und referenzierter Position. Reset, Handshake und explizites Homing sind eigene
Phasen. Ohne diese privaten Verträge wird kein Koordinatensystem geraten;
Diagnosebereitschaft erteilt weiterhin keine Bewegungsfreigabe.

Ein unerlaubter frischer Status in Idle, Run oder Hold verwirft Auftrag und
Referenz vor weiterer ACK-/Hold-/Run-Verarbeitung, fordert bestmöglich Hold/Reset
an und schließt die Verbindung. Eine nachfolgende vermeintlich korrekte Position
im selben Eingabepaket stellt weder Verbindung noch Vertrauen wieder her.
Nach einem erlaubten Homing wird der Idle-Rückkehrpunkt geprüft, bevor daraus
mit vorhandenem Maschinenraum eine neue Protokollreferenz entsteht. Das
read-only Diagnoseereignis benennt `reportedPhysicalBounds`; es belegt keine
reale physische Grenzverletzung oder tatsächliche Abschaltwirkung.

Die PTY-Tests verwenden ausdrücklich synthetische positive und negative Räume,
verschobenes G54, MPos und WPos+WCO, exakte Grenzen und Überstände von 0,0001 mm.
Sie prüfen den Verbindungsabbruch, fehlende weitere G1/M4-Ausgabe trotz späterer
ACKs und die fehlende Wiederherstellung durch einen falschen In-Range-Bericht.
Kein realer Anschluss oder Laser wurde dafür geöffnet oder bedient.

## Reale Arbeitsnullpunkt-Freigabe bleibt offen

Das Backend besitzt jetzt eine ausdrücklich begrenzte
[WorkZero-Transaktion](WORK_ZERO_TRANSACTION.md) mit eingefrorenem rohem MPos,
ACK-/Modal-/Readbackgrenzen und fester Deadline. Das ist keine reale
M3-Inbetriebnahme oder allgemeine Ausnahme vom Machine-Verlustwächter.
Die Fassade darf ausschließlich diese kontrollierte Phase ohne tatsächlich
aktive Outputlease, Job-/Fokusflag oder Firing behandeln. Ein aktiver Job darf
niemals unter eine Koordinaten-Ausnahme fallen. Machine-/UI-Integration wird
separat geprüft; Beta6 bleibt Entwicklung/offline.

## Echtzeit-Overrides

`ControllerStatus.overridesReady` beschreibt ausschließlich den Session-Vertrag:
frische verifizierte Positions-, WCO- und Ausgangsrückmeldung, bestätigte
Referenz und private Fahrraum-/Modalbelege, gültige Ov-Rückmeldung und Zustand
Idle, Run oder Hold. Homing, angeforderter Reset und Handshake sperren diesen
Einstieg. Die Fassade muss ihre zusätzlichen Absaugungs-/Outputregeln weiter
prüfen; das Sessionbit ist keine Freigabe von physischem Fluss oder Emission.

Jeder Override-Aufruf prüft denselben Vertrag unmittelbar vor den Echtzeitbytes.
Vorschub bleibt auf 10–200 %, Leistung auf 10–100 % begrenzt. Eine empfangene
fehlerhafte Ov-Rückmeldung entwertet zuvor bekannte Overridewerte. Fehlendes Ov
in einem sonst gültigen Status ist zulässig, weil der Controller diese Felder
nicht mit jedem Status sendet; Positions- und Ov-Rückmeldung müssen beide
innerhalb von 1,5 Sekunden frisch sein. Sichere Hold-Anforderungen bleiben von diesem Ausgabegate getrennt.

Die Echtzeitfolge setzt zuerst den Leistungsfaktor mit 19 Minus-10-Befehlen
auf die separat belegte Controlleruntergrenze von 10 %. Erst danach setzt sie
den Vorschub auf 100 % und verändert ihn zum Ziel; zuletzt erhöht sie die
Leistung von 10 % monoton zum Ziel von höchstens 100 %. So hängt kein Schritt
von einem möglicherweise überholten Ov-Wert ab. Wiederholte Klicks und verspätete
alte Rückmeldungen können keine Erhöhungen aufsummieren; ein reduziertes Ziel
wird nicht zwischenzeitlich durch einen Power-Reset auf 100 % überschritten.

Diese Folge setzt eine separat bestätigte GRBL-Befehls- und Clampsemantik
(10–200 %) und geordnete, genau einmal ausgeführte Einzelinkremente voraus. Dafür existiert nur ein privater, standardmäßig falscher
`commissionedOverrideContract`, keine Runtime-, JSON- oder UI-Freigabe. Nur die
synthetische PTY-Fixture bestätigt ihre eigene virtuelle Clampsemantik. Reset,
Fault, Disconnect, neuer Handshake, Alarm und unerwartete Modaländerung entwerten
diesen Vertrag. Ein Firmwarebanner oder Ov-Wert kann ihn nicht erteilen.
Fokusimpulse sperren Overrides unabhängig vom übrigen Bereitschaftszustand.
`overridesSafetyError` und `overridesReady` verwenden dieselbe Prüfung.

Die unabhängige PTY-Bytepflanze begrenzt ihre virtuelle Overrideauswertung auf
10–200 % und prüft jede Zwischenstufe, Reihenfolge und das Endziel bei
wiederholten Anforderungen ohne neue oder mit absichtlich verspäteter alter
Ov-Rückmeldung. Jede angeforderte Leistungszwischenstufe bleibt höchstens beim
Maximum aus vorheriger und gewünschter Leistung (jeweils höchstens 100 %).
Das ist keine Hardwarevalidierung oder Aussage über tatsächliche optische
Emission; die reale Blazer-Ov-Semantik bleibt offen.

**Der Standard-GRBL-Flagpfad belegt diese Einzelverarbeitung nicht.** Seine
[Seriell-ISR](https://raw.githubusercontent.com/gnea/grbl/master/grbl/serial.c)
setzt Realtime-Overrideflags; die
[Protokollschleife](https://raw.githubusercontent.com/gnea/grbl/master/grbl/protocol.c)
verarbeitet jedes gesetzte Flag einmal und löscht anschließend das Flagpaket.
Daraus folgt, dass wiederholte Bytes vor der Verarbeitung zusammenfallen können.
Eine zweite unabhängige Scheduler-Regression zeigt die Gegenprobe: zusammen-
gefasste Minusbefehle mit später einzeln verarbeiteten Plusbefehlen können die
Floorannahme verletzen. Für dieses Modell sendet die Session ohne gesonderten
Vertrag keine Overridebytes. Die positive synthetische Bytepflanze ist kein
Beleg für Standard-GRBL oder die unbekannte reale Blazer-Firmware. Ein späteres
reales Protokoll benötigt verlässliche Einzelverarbeitungs-/Abgleichgrenzen;
Ov-Frische oder Zeitpausen allein erteilen diese Freigabe nicht.
