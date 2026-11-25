# Sessionbezogene M3-Einrichtung

Der Worker hält den Zustand; GUI-Attestierungen sind ausdrücklich vom Operator
beobachtete Mechanikdaten, keine automatisch gemessenen Tatsachen. Die Evidenz
wird nicht als ausführbare Freigabedatei gespeichert/importiert. Ab beta.10.5 können abgeschlossene Geometrie- und Kalibrierdaten wiederverwendet werden, wie unten beschrieben. Profil, aktive
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


## Gespeicherte Kalibrierung und automatische Initialisierung

Nach erfolgreichem Verbindungsaufbau wird ein vollständiger Readback automatisch
angefordert. Connect löst keine Referenzfahrt, G10-Offsetänderung oder Gravur aus.
Der lokale Datenspeicher bindet einen abgeschlossenen Kalibrierdatensatz an den
DeviceLibrary-Bindingdigest und authentifiziert den Inhalt mit HMAC-SHA256. Der
Schlüssel und die atomar geschriebenen Dateien sind nur für den lokalen Benutzer
lesbar. Dies schützt vor beschädigten oder versehentlich veränderten Datensätzen;
es ist keine Vertrauensgrenze gegenüber demselben Betriebssystembenutzer.

Der Worker prüft Schema, endliche Geometrie, Profil, Hardwarekennung aus der
geordneten frischen $I-Antwort und Firmware-/Settingsfingerprint erneut. Ein
Datensatz enthält niemals positionReferenced oder eine Ausgabe-/Zubehörfreigabe.
Ohne eindeutige Controllerkennung bleibt die alte vollständige Sitzungseinrichtung
verwendbar, wird aber nicht zur automatischen Wiederverwendung gespeichert.

`savedHomeReady` erlaubt genau einen frischen nativen `setupHome`-Intent. Erst
bestätigtes Homing mit frischem Idle/S0 und passender Maschinenposition stellt
die Referenz wieder her. Home außerhalb des Innenraums führt zu
`savedApproachReady`: ein eigener nativer `setupCorner(0)` fordert die gespeicherte
langsame S0-Anfahrt an. Normale Geräteaktionen bleiben dabei gesperrt. Nach der
Anfahrt gilt ausschließlich das gespeicherte Innenrechteck. Bei geändertem G54
verlangt `restoredOriginReview` die erneute Zuordnungsbestätigung; unverändertes
G54 erlaubt den Abschluss nach der Referenz. Kein automatisches Starten oder
Umgehen von STOPP, Alarm, Door, Reset, Handshake oder Parameterwidersprüchen.

Die letzte Bestätigung der vollständigen Einrichtung schließt den Dialog erst
nach Worker-complete. Während dieser asynchronen Bestätigung gibt es keinen
zweiten schließenden Abbruchknopf; STOPP bleibt möglich. Ein ausdrückliches
Verwerfen während einer unvollständigen Einrichtung widerruft weiterhin die
Positionsreferenz. Der Widerrufsgrund bleibt im Hauptfenster und beim Wiederöffnen
sichtbar, bis ausdrücklich neue Controllerdaten angefordert werden.

## Beaufsichtigte externe Absaugung

`Machine.extractionSupervision` veröffentlicht `mode` (`sensorVerified` oder
`operatorAttendedExternal`), `notice`, `reason`, `pendingAction` (leer oder
`start`/`focus`/`resume`), `remainingMs`, `activeLease` und `airflowMeasured=false`.
`selectAttendedExtraction()` ist eine ausdrückliche Wahl; sie sendet keinen
Lüfterbefehl. Nach einem Zubehörfault ist zuvor
`confirmExtractionClearance(true)` als echte menschliche Bestätigung erforderlich.
Das wird insbesondere nach Homing nicht automatisch übersprungen.

`attestExternalExtraction(action, extractionOn, operatorPresent)` erfasst eine
Operatorerklärung, niemals einen Sensorwert. Die 30 Sekunden beginnen bei der
Worker-Erfassung dieser Nichtbewegungsdaten. Anschließend bleibt ein separater
frischer nativer Ausgabeintent erforderlich. Start bindet die einmalige Witness
an Prepared-Quelle/Profil und einen konkreten Versuchsbezeichner; Fokus erhält
einen eigenen Versuch; Resume bindet dieselbe aktive Jobinstanz mit neuer
Witness. Eine falsche Quelle, ein anderer Koordinatenrahmen, ein abgelaufener
oder bereits verbrauchter Beleg kann keine Ausgabe starten.

Einmalig beim Hold-Eintritt wird die alte Witness verworfen. Für Resume sind
frisches echtes Hold-Readback und neue menschliche Erklärung erforderlich.
Abschluss/STOPP/Fault/Disconnect verwerfen Pending. Während einer ausdrücklich
beaufsichtigten aktiven Lease wird kein fiktiver 3-s-Flowwert erneuert.
`revokeExternalAttendance()` fordert STOPP und entwertet die Beaufsichtigung;
externe Absaugung wird dabei nicht ausgeschaltet. Live-Overrides bleiben in
diesem Modus gesperrt. Siehe [Vertragsdetails](OPERATOR_ATTENDED_EXTRACTION.md).
