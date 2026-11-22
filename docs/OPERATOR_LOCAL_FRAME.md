# Manueller Kopfpunkt: getrennte lokale Autorität

Diese Backend-Vertikale setzt keinen globalen Referenznachweis. Die vier
Restwege sind ausdrücklich gemessene Angaben der anwesenden Bedienperson,
keine aus alten MPos/Profilmaßen abgeleiteten Messwerte. Die Software zieht
unveränderlich je Richtung 2 mm Reserve ab; Gegensummen werden zusätzlich
gegen die nominellen Profilmaße auf Plausibilität geprüft.

`beginManualPlacement()` verweigert aktive/ausstehende Geräteaktionen, entwertet
vorherige globale Lage und liest ausschließlich $I/$$/$G/$#/$N. Der getrennte
Readback erfasst zusätzlich $1, interpretiert es aber nicht als physischen
Motorfreigabesensor. Tatsächliche freie Motoren, gemessene Achszuordnung und
freier Raum, Kopfstillstand und geschlossene Haube sind Operatorattestierungen.
Kein $SLP, keine Settingsänderung, kein $X und kein ~ zur Entsperrung.

Nach frischem Door:0 oder Idle/S0 kann ein eigener nativer localCapture-Intent
bei zuvor bewiesenen leeren Startupblöcken bewusst Reset und erneuten Readback
anfordern. Door:0 wird dabei niemals als Idle behandelt; bleibt die Firmware
nach Reset in Door/Alarm, wird abgebrochen. Ein erneutes Öffnen während der
Aufnahme entwertet sie auch während Reset/Handshake. Neue Firmware/Settings,
insbesondere globale Softlimits $20!=0, schließen den lokalen Weg.

Die anschließende G10-Transaktion bewegt keine XY-Achse. Sie verlangt sämtliche
ACKs und nachfolgend frisches Idle/S0 mit unverändertem rohem Zählerpunkt sowie
passenden WPos/WCO. Erst dieser Readback erzeugt den getrennten privaten
OperatorLocalPermit; `positionReferenced` bleibt false. Gewöhnliche globale
Jog/Home/G53/Zero/UserOrigin/Overrides-/Fokuspfade sind damit nicht freigeschaltet.

## Virtuelle positive G54-Koordinaten

Der interne Kopfpunkt wird als ceil((negativeRest-2)*1000)/1000 gewählt und
anschließend aus dem tatsächlichen G10-Readback übernommen. Diese reine
Translation legt den Nullpunkt an die sichere Innenseite und hält auch bei
Bruchteilen den Raum für den bestehenden positiven Encoder darstellbar.
Die physische Prüfung bleibt unabhängig davon ausschließlich der frische
Rohzähler-Delta im Intervall [-negativeRest+2,positiveRest-2]. Die Reserve wird
nicht weggerundet oder als globale Maschinengeometrie ausgegeben.

Nur für reine CurrentPosition-Planung mit neun Ankern erhält Document eine
explizit lokale PreviewPosition mit eigenem Framefingerprint. Ein anderes
Koordinatensystem, Benutzerursprung oder Print-and-Cut-Registrierung wird nicht
heimlich übernommen. Der bestehende begrenzte PrepareRunner berechnet das
immutable PreparedLocalJob außerhalb des I/O-Threads. Alle Raw-, lokalen und
TATSÄCHLICH absolut formatierten Wireziele einschließlich Anfahrt, Überlauf und
Frame-Rückkehr werden konservativ geprüft. Der Worker gleicht die Artefaktdaten
nochmals mit den eigenen ursprünglichen Restwegen und dem aktuellen lokalen
Kopf ab. Eine Datenartefakt-Validierung allein erzeugt keine Ausführungsfreigabe.

`available`/`usableAfterMargin` und `requiredActualWire` im Snapshot gehören bei
vorhandenem Candidate zu demselben unveränderlichen Planungsanker. Die zuerst
menschlich gemessenen Werte bleiben separat `measuredAvailable`; `planAvailable`
ist der aktuelle Recheninput nach ausschließlich lokal nachgeführter Bewegung.

## Start, Rahmen, Pause und Widerruf

Frame besitzt ein bereits vorbereitetes, separat validiertes, eingefrorenes
S0-Programm und eigenen NativeIntent. Es kann bei passendem Plan vor der
Jobapproval gefahren werden. Framefehler verhindern den Job nicht automatisch.
Jobstart benötigt genaue Approval-ID, passende immutable Quelle/Frameepoch,
frische separate Absaugungs-/Aufsichtswitness und einen neuen localStart-Intent.
Keine vorbereitete Berechnung oder Datenattestierung startet automatisch.

Die tatsächliche lokale Ausführung verwendet einen eigenen Autoritätstyp in
Core-Prewrite und Watchdog; globale reference-Gates werden nicht pauschal
ignoriert. Verlorene lokale Position/Settings/WCO/Haube/Verbindung/Zeitkontinuität
verwerfen die Autorität. STOPP hat die bestehende Priorität und keinen Retry.
Dokumentänderungen widerrufen noch nicht zum ersten Write gelangte lokale
NativeIntents. Bereits gestartete Aufträge bleiben unveränderlich eingefroren.

Pause darf niemals implizit Resume werden. Ein Jobresume braucht frisches echtes
Hold-Readback, unveränderte lokale Autorität, neue Aufsichtswitness und eigenen
localResume-Intent. Für den separat typisierten unveränderlichen S0-Frame ist
Resume ausschließlich bei frischem gemeldetem S0 ohne Outputlease erlaubt.
Eine absichtlich angeforderte Pause verwirft die alte Startlatenzdeadline;
explizites Resume setzt eine neue. Keine Türentriegelung durch Resume.

Alle schweren Schritte sind vor StartReady fertig. Die erste gemeldete
nichtnullige lokale Bewegung wird mit einer 10-s-Hostdeadline überwacht; dies
ist kein physischer Encoder- oder Hard-Realtime-Nachweis. Der unabhängige
**simulierte Plant-Prozess** misst nichtnullige virtuelle Bewegung über eigene
monotone Zeitstempel. Kein M5 oder G0 ohne Verfahrweg gilt als Bewegungsbeginn.

Der neue Test operatorlocalframe verwendet tatsächliche Machine/Document und
Worker mit eigenem synthetischem PTY, privat explizit synthetischen NativeProofs
und unveränderten Zählern trotz virtueller Handverlagerung. Es werden weder
reale Geräte noch reale OS-Eingaben, Laser oder physischer Luftstrom geprüft.
