# beta.8-dev – Software-Liefernachweis

Veröffentlichtes Paket und lokale Installation: **0.9.0-beta.8-dev**, Quellrevision `de274e8ae9c8ec291668e499a1099fe7854871d2`, Ubuntu 26.04 amd64. [Release und Downloads](https://github.com/mgd1068/blazer-studio/releases/tag/v0.9.0-beta.8-dev). Dies dokumentiert Softwareprüfungen; die physische Abnahme des M3 steht aus.

## Geprüfter Grundablauf

Die geführte Einrichtung liest aktuelle Controllerdaten und führt über ausdrücklich bestätigte Mechanik, Referenzfahrt, gemessene Achsproben, Innenfahrraum, vier Ecken und Arbeitsnullpunkt. Der durchgehende Test verwendet echte QML-Komponenten, Machine und Worker an einem unabhängigen eigenen PTY-Controller. Er prüft zusätzlich die Auftragsbindung, Start, Hold, eine neue Resume-Bestätigung und Aufsichts-STOPP. Private synthetische Eingaben im Test sind keine echten Betriebssystem- oder Hardwarebeobachtungen. Es wurden keine realen Gerätebefehle ausgeführt.

## Validierung und Grenzen

- Vollständiger lokaler Normallauf: **64/64 Testgruppen**, keine QtTest-Skips. Dieser Lauf erfolgte auf `34213ad`; der Lieferstand `de274e8` unterscheidet sich ausschließlich in einem Test und dessen PTY-Helfer, nicht im Anwendungscode.
- Kritischer ASan/UBSan-Lauf: 22 Gruppen zunächst erfolgreich; der Controller-Einrichtungstest bestätigte den Nullpunkt zu früh, weil er alten Phasenzustand und neuen Positionszähler getrennt auswertete. Der Produktionscode verweigerte diese verfrühte Bestätigung korrekt. Der korrigierte Test hält die letzte Controllerquittierung gezielt zurück und verlangt anschließend einen zusammenhängenden fertigen Zustand. Betroffene Controllerfamilie und echte Machinebridge wurden auf `de274e8` normal **2/2** sowie instrumentiert **2/2** erfolgreich erneut geprüft. Keine ASan-/UBSan-Fehlermeldungen; Leakerkennung war abgeschaltet.
- Neues Paket aus isoliertem, gepinntem Builder: 102/102 Prüfsummen und alle 351 getrackten Quelldateien im Sourcearchiv unabhängig bytegeprüft. Keine Qt-Bündelung.
- Neuinstallation und tatsächliches Upgrade beta.7→beta.8 bestanden jeweils mit Datenerhalt, Reinstallation, Entfernung, Selbsttest und Offlineexport auf einem System ohne Entwicklungswerkzeuge. Screenshots unabhängig geprüft.
- Installiertes Programm bytegleich zum geprüften DEB; installierter Selbsttest und Original-Umrandungsorakel bestanden ohne Warnungen. Einstellungen und Originalprojekt blieben unverändert. beta.7-Paket und vorheriger Datenstand sind lokal als Rückweg gesichert.
- Das Originalprojekt „Nur Umrandung“ erzeugt vier Kanten, 700 mm Gesamtweg, keine Füllung und zuerst eine ausgeschaltete Anfahrt zum Figurenanfang. Seine gespeicherten 100 % Leistung sind unveränderte Projektdaten und keine Materialempfehlung. Die nominellen 200 × 150 mm müssen weiterhin in den tatsächlich eingerichteten Innenraum passen.

Auf GitHub bestanden der vollständige ASan/UBSan-Lauf mit 64/64 Gruppen und der Paketjob für den Lieferstand. Der normale GitHub-Lauf meldete in einer kompakten User-Origin-Ansicht einen fehlenden Nachweis nach einem Klick. Ein unabhängig deterministisch nachgewiesener Fehler im Testhelfer (Erfolgsmeldung trotz außerhalb des sichtbaren Bereichs gelandeten Klicks nach verzögertem Layout) wurde anschließend ausschließlich im Test korrigiert; die tatsächliche Klickzustellung wird nun geprüft. Die betroffene Familie besteht lokal 20/20. Die genaue Ursache des ursprünglichen CI-Ausfalls ist damit noch nicht bewiesen; der nächste CI-Lauf muss die Korrektur bestätigen. Der Anwendungscode dieses Lieferpakets bleibt unverändert. Keine dieser Softwareprüfungen ersetzt die physische Geräteprüfung.

## Artefaktidentität

- DEB SHA256: `9ddc02c45bc0a909a76f17a511056c01e9efab8662f6fb1f299ae10d3562e58e`
- Programm SHA256: `b817f009b82dec9fc8bf60229fff448665f0626cbfbc95e0b641f279bc55acac`
- Sourcearchiv SHA256: `c29d5c27f94c6b43b5657839a01b2d6044158ce00b9f63cf6a2a4f8cd91fd8a9`

Alle fünf GitHub-Assetdigests wurden gegen die lokalen Dateien geprüft. GitHub ersetzt im DEB-Dateinamen `~` durch `.`; die externen Downloadprüfsummen verwenden die tatsächlichen Assetnamen. Das Komplettarchiv behält ursprüngliche Namen und seine internen Prüfsummen.

## Noch offen

Physischer Fahrraum, Referenzort, Arbeitsnullpunkt und tatsächliche Absaugung müssen am Gerät beobachtet werden. Die unabhängige Gehäuse-Fan-Port-Steuerung ist nicht geklärt. Der beaufsichtigte Modus benötigt separat laufende geeignete Absaugung und menschliche Anwesenheit; er schaltet keinen Lüfter und misst keinen Luftstrom. Vollständige LightBurn-Parität bleibt offen. Siehe [Einrichtung](START.md#geräteanschluss) und [bekannte Grenzen](KNOWN_LIMITATIONS.md).
