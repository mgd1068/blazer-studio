# Begrenzter technischer Controllerkontext

`ControllerSession::diagnosticSnapshot() const` liefert standardmäßig nur
`{"supported":false}`. Der GRBL-Adapter unterstützt Schema 1 als read-only
`QVariantMap`. Die API öffnet keinen Anschluss, sendet keinen Befehl und ändert
keine Steuerentscheidung. Erfasst werden Aufrufe, die den Controlleradapter
erreichen; schon in UI/Fassade abgewiesene Aktionen erzeugen hier kein
erfundenes Controllerereignis. Die vorhandene datensparsame Standarddiagnose bleibt
unverändert. Ein bewusster lokaler Export mit Vorschau ist eine gesonderte
Integration; diese API exportiert oder überträgt selbst keine Datei. Sie wird
im QObject-Eigentümerthread der Session gelesen; für einen Hintergrundexport
wird anschließend nur die zurückgegebene Wertkopie verwendet.

## Inhalt und Grenzen

- Monotone relative Millisekunden seit Erstellung des Auditobjekts und eine
  fortlaufende Ereignisnummer; keine Uhrzeit, kein Datum und kein Host-Uptimewert.
- Maximal 64 typisierte Ereignisse und 64 ausstehende ACK-Zuordnungen. Ältere
  Einträge werden gezählt verworfen. Ein überlaufender ACK-Puffer ordnet alte
  Antworten ausdrücklich keinem neueren Befehl zu.
- Höchstens 32768 Bytes für die kompakte UTF-8-JSON-Darstellung des Snapshots.
  Der Ring speichert nur feste Enums, Zahlen und begrenzte optionale XY-Werte;
  keine frei eingebbaren Texte oder unbeschränkten Listen.
- Startanforderungen und abgelehnte Vorprüfungen mit festen Grundcodes;
  relevante Sicherheits-/Koordinatenpräambel, erste angeforderte G0-Anfahrt und
  erstes angefordertes XY-Ziel mit programmierter Leistung größer null. Nur
  diese beiden Fahrtereignisse speichern Zielkoordinaten. Weitere Jobpfade,
  ihre Koordinaten und ihre Reihenfolge werden nicht aufgezeichnet; danach
  bleiben Summenzähler und die abschließende Laser-Aus-Anforderung.
- Protokoll-ACKs mit Verweis auf die zugeordnete Ereignisnummer. Ein Befehl kann
  `pending`, `acknowledged`, `rejectedByController` oder
  `notAcknowledgedBeforeBoundary` oder bei überlaufender Instrumentierung
  `acknowledgementTrackingLost` sein. Die letzte Bezeichnung sagt **nicht**,
  dass der Controller den Befehl nicht bereits empfangen/ausgeführt hätte.
- Resetanforderung mit Grund, Annahme der Bytes durch den Qt-Ausgabepuffer und
  anschließend beobachtetes Startupbanner sind getrennte Ereignisse. Ein
  Startupbanner ist keine Referenzfahrt. Nur tatsächlich zum Versand gepufferte
  `$H`-Befehle erscheinen als `homingRequested`; ein abgelehnter Home-Klick
  erzeugt lediglich Anforderung und Vorprüfungsablehnung.
- Normalisierte Handshakefortsetzung nach `error:9` auf das erste M5 wird als
  Ablehnung dokumentiert. Das intern zur Steuerfortsetzung verwendete `ok`
  wird niemals als tatsächlich empfangenes ACK dargestellt.
- Feed-Hold, Cycle-Resume und Override-Anforderungen sind typisierte
  Echtzeit-Schreibereignisse mit Annahmestatus des Qt-Puffers. Periodische
  Statusabfragen erzeugen nur Summenzähler und verdrängen den Ring nicht.
- Neueste bekannte Arbeits-/Maschinen-XY und WCO mit Gültigkeit, Quellalter und
  einem informativen 1500-ms-Frischefenster. Ungültige Zahlen werden nicht als
  null ausgegeben. Ohne bestätigtes `$13=0` werden keine rohen Zoll-/Unklarwerte
  als Millimeter ausgegeben. Fehlende neue WCO-Felder erneuern nicht das Quellalter eines
  alten WCO. Aktuelle Verbindungs-/Bereitschaftsflags und der echte interne
  Status-Frischetest werden separat ausgegeben. Ein alter gültiger Quellwert
  ist keine Aussage über die jetzige physische Position. Reset/Verbindungsverlust
  löschen einen zuletzt gültig gelesenen Zahlenwert nicht aus dem Vorfallkontext;
  `fresh:false` und die aktuellen Zustandsflags unterscheiden ihn von gültiger
  aktueller Controllerkenntnis. Das gilt auch für den letzten numerischen
  FS-Ausgangswert. Fehlerursachen verwenden feste Codes wie `timeout`,
  `offsetChanged` oder `idleOutputReported`, keine frei eingebbaren Fehlertexte.
- Ein akzeptierter Jobstart bewahrt genau einen begrenzten Startkontext mit
  den damals bekannten XY-/WCO-Werten, deren Quellalter, numerischem Readback
  und Referenz-/Vertragsflags. Spätere Reset-/Fehlermeldungen ersetzen diese
  Anfangswerte nicht durch vermeintlich passende Nullen.
- Ausschließlich tatsächlich empfangenes numerisches Readback für `$3`, `$5`,
  `$13`, `$20`, `$21`, `$22`, `$23`, `$24`, `$25`, `$26`, `$27`, `$30`, `$31`,
  `$32`, `$130`, `$131`. Weitere Einstellungen und nicht numerische Inhalte
  werden nicht übernommen. Endliche Zahlen müssen im Betrag höchstens 1e9
  sein. Ungültiges Readback erhält `known:false` ohne erfundenen Zahlenwert.
  Diese Daten erteilen keine Kalibrierungs- oder Homingfreigabe. Zusätzlich
  werden aus tatsächlich empfangenem `$G` nur die numerischen G54–G59-,
  G20/G21- und G90/G91-Codes mit Quellalter übernommen. Fehlende, doppelte oder
  ungültige Kategorien bleiben unbekannt; der übrige Modal-Freitext entfällt.

Die Bezeichnungen lauten bewusst **angefordert**, **gepuffert** und
**quittiert**. M4/S allein beweist weder einen Lichtstrahl noch optische
Leistung; G0/G1 und ein ACK beweisen keine tatsächliche mechanische Bewegung.
Auch ein referenziertes Protokollflag beweist keine Hardwarekalibrierung.

## Datensparsamkeit

Nicht enthalten sind Anschlussnamen/-pfade, Netzwerk- oder Zugangsdaten,
Controller-Freitext, Firmware-Identitätstexte, Meldungspayloads, Projekt- oder
Objektnamen, Grafik-/Bildinhalte, Roh-GCode, komplette Werkzeugwege oder die
unbeschränkten GUI-Protokolltexte. Der Aufzeichnungshelfer akzeptiert keine
beliebigen String-/Map-Payloads. Ein unbekannter gemeldeter Controllerzustand
wird nur als `unknown` dargestellt. Numerische Parameter und einzelne
Koordinaten sind technischer Kontext und keine Garantie vollständiger
Anonymität; eine spätere Exportoberfläche muss sie vor der lokalen Speicherung
sichtbar machen. Es gibt keinen automatischen Upload.

## Offline-Nachweise

Die PTY-Tests prüfen Verwerfen/Begrenzung, monotone Chronologie, korrekte
ACK-Zuordnung, fehlende Endpunkt-/Freitextübernahme, numerische Whitelist,
`error:9` gegenüber echten ACKs, Reset gegenüber Homing und Statusalter.
Die unabhängige virtuelle Pflanze prüft zusätzlich Startkontext, erste
Anfahrt und erstes Leistungsziel sowie das Fehlen weiterer Pfadkoordinaten.
Die bestehenden exakten Befehls-/Streamprüfungen bleiben unverändert gültig.
Diese Instrumentierung wurde ausschließlich offline und mit synthetischen
PTYs geprüft. Sie ersetzt keinen realen Bewegungs-, Grenz- oder Abschaltnachweis.
