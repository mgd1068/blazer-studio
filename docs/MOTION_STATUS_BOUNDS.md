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

## Noch offene reale Arbeitsnullpunkt-Transaktion

Die tatsächliche M3-Nullpunktbedienung ist weiter nicht freigegeben. G10 ändert
absichtlich den WCO-Vertrag; der bestehende allgemeine Machine-Verlustwächter
kann diese derzeit noch nicht als eigenen erwarteten, begrenzten Ablauf
behandeln. Eine spätere Umsetzung benötigt einen expliziten Typ für
M5/G10/G54/ACK/neues WCO/Idle mit unabhängiger Regression und Fehlergrenzen.
Sie darf einen aktiven Job oder eine Fokus-/Absaugungslease niemals unter eine
solche Ausnahme fallen lassen. Das wird durch diese Bereichsprüfung nicht
implementiert oder kaschiert; Beta6 bleibt Entwicklung/offline.
