# Sessionbezogene M3-Einrichtung

Der Worker hält den Zustand; GUI-Attestierungen sind ausdrücklich vom Operator
beobachtete Mechanikdaten, keine automatisch gemessenen Tatsachen. Die Evidenz
wird nicht als ausführbare Freigabedatei gespeichert/importiert. Profil, aktive
Verbindungsepoche, Firmware und Einstellungen binden sie an diese Sitzung.

Vor der ersten Verbindung muss die Oberfläche auf sichere Erstprüfung mit
deaktivierter Laseremission hinweisen: Serial-open kann Firmware starten und
Blazer sendet Hold/Reset. Ein späteres `$N` kann bereits ausgeführte Startupcodes
nicht rückgängig machen. Dieser Workflow bestätigt keine Firmwarefunktion im
Host-Suspend, bei Stromverlust oder bei Verbindungsverlust.

`Machine.commissioning` ist eine readonly Map. `beginCommissioning()` fordert
M5/S0 und G21/G90/G54 ohne EEPROM-Settingsänderung an, dann vollständig quittierte
$I/$$/$G/$#/$N-Readbacks. 22 relevante numerische Einstellungen, G54/G92/TLO,
G21/G90/M5/S0 und genau zwei leere Startupblöcke sind erforderlich. G92/TLO müssen
null sein und G54 muss zum frisch gemeldeten WCO passen. Keine automatische
Startupblocklöschung, kein $X oder ~ zur Umgehung eines Interlocks.

Nach Bestätigung von identifizierten Endschaltern, passender Homingrichtung,
freiem Fahrweg und deaktivierter Laseremission erlaubt `setupHome` genau eine
explizite Referenzfahrt. ACK und frisches Idle/S0 belegen nur das Protokoll;
`confirmCommissioningHome()` dokumentiert den tatsächlich beobachteten Homeort.
Die Attestierung umfasst freien ±2-mm-Probenraum dort. `setupProbe` erlaubt
jeweils höchstens 1 mm auf genau einer Achse innerhalb dieses Bereichs. Beide
Achsen müssen mit tatsächlicher Strecken-/Richtungsbeobachtung bestätigt werden.

`setCommissioningLimits` übernimmt endliche, gemessene signierte Grenzen mit
Sicherheitsabstand. Weder Vorzeichen noch nominale 200×150 mm werden als
physisch erwiesen angenommen. Der Operator bestätigt auch den freien Anfahrtsraum
von Home zum Innenrechteck. `setupCorner` fährt einzeln mit M5/S0, G21/G90/G54,
G1/F300/S0; jede physische Ecke wird separat bestätigt. Die Reihenfolge endet
bei minX/minY. Die exakte 0,001-mm-Wireformatierung darf keine Ecke aus dem
Innenraum verschieben. Vier Ecken allein sind keine automatische Kalibrierung.

In `workCoordinates` kann `setupZero` die bestehende bounded G10-Transaktion
nutzen. Erst deren vollständige ACK-/Fresh-Idle-/unveränderte-MPos-Prüfung kehrt
nach `workCoordinates` zurück. Die finale ausdrückliche G54-/Modellbestätigung
setzt `complete`. Snapshot zeigt tatsächliches WCO, rohe Maschinenposition,
Zielposition und nutzbare WorkBounds. Unpassende Prepared-Jobs bleiben durch die
bisherige vollständige Roh-/Wiregrenzprüfung abgewiesen. Spiegelung ist keine
G54-Translation; Richtungswiderspruch darf nicht als bestätigt übergangen werden.

Während Einrichtung können normale Geräteaktionen keine temporären
Probe-/Anfahrtsverträge nutzen. STOPP, Reset, Disconnect, Fault, Konfigurations-
oder Firmwarewechsel entwerten die Einrichtung. Kein Retry, Auto-Home oder
Auto-Start. Nach STOPP muss die Sitzung erneut eingerichtet/referenziert werden.
Absaugungs-/Materialfreigabe bleibt unabhängig. Overridevertrag bleibt gesperrt.

Test `controllercommissioning`: separater Pythonprozess mit eigenem PTY,
unabhängiger XY-/G54-/G90/G91-/G20/G21-Pflanze. Reset setzt Zähler, bewegt den
realen virtuellen Kopf aber nicht. Signed Maschinenraum und verschobenes G54
werden bis Referenz, Proben, vier Ecken, G10 und Grant durchlaufen. Negative
Readbacks/fehlende ACKs/Attestierungen/Konfigurationsänderungen und Versuch einer
normalen Bewegung während Setup werden unabhängig beobachtet. Keine echten
Ports, OS-Eingaben, Laser- oder Hardwarevalidierung.
