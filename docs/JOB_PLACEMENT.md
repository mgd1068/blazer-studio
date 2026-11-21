# Startmodus und Neunpunkt-Ursprung: Offline-Vertrag

F39-Teilumfang, geprüft gegen LightBurn **2.2.01**, 21.11.2026. Die offiziellen [Koordinaten-/Ursprungsregeln](https://docs.lightburnsoftware.com/2.2/Reference/CoordinatesOrigin/) unterscheiden absolute Entwurfskoordinaten, die Kopfposition beim Start und einen zuvor gesetzten Benutzerursprung. Die neun Punkte liegen an Ecken, Kantenmitten und Zentrum des Grafikbegrenzungsrechtecks; bei absoluter Ausgabe sind sie wirkungslos. Galvo verwendet absolute Koordinaten. Die [Auswahlursprungsoption](https://docs.lightburnsoftware.com/2.2/Reference/CutSelectedGraphics/) unterscheidet beim Ausgabeausschnitt den Ursprung des gesamten Designs von dem der Auswahl. Eine Endposition ist ein gesonderter Arbeitsablauf.

`src/jobplacement.{h,cpp}` implementiert zunächst ausschließlich die reine Platzierung eines vorhandenen kanonischen XY-Plans. Die Funktion liest weder Controllerstatus noch Gerätekonfiguration aus und erteilt keine Freigabe für reale Ausgabe.

## Quellstand und Grafikgrenzen

`SourceSnapshot::capture(plan, graphicsBounds, geometryFingerprint, scope)` bindet unveränderlich:

- den vollständigen Bewegungsfingerprint des gültigen Plans und seinen Geräteprofilfingerprint;
- explizit übergebene Grafikgrenzen und eine 64-stellige kleingeschriebene hexadezimale SHA-256-Geometriesignatur;
- `OriginScope::Design` oder `OriginScope::Selection`.

**Der Aufrufer muss Plan, Grafikgrenzen und Geometriesignatur aus demselben unveränderlichen Quellstand ableiten.** Die Signatur muss alle für die gewählte Ursprungsgeometrie relevanten Daten berücksichtigen, einschließlich des gewählten Blatts/Ausgabeausschnitts und der durch diese Auswahl wirksamen Geometrie. Der Helper kann einen fälschlich behaupteten Zusammenhang zwischen einem fremden Rechteck und einem Plan nicht beweisen. Er erkennt aber spätere Änderungen an Plan, Profil, Rechteck, Signatur oder Ursprungsscope über die gebundene Vorschauidentität. Bei Wiederverwendung ist ein neuer Quellsnapshot für den aktuellen Entwurfsstand erforderlich.

Die Grafikgrenzen sind **nicht** der Umfang sämtlicher Bewegungen. Insbesondere zählen weder die synthetische initiale Bewegung ab 0,0 noch Rasterüberlauf zum Neunpunkt-Rechteck. `Document::framingBounds("job")` enthält absichtlich auch Überlaufbewegungen mit `rapid=false` und ist deshalb hierfür nicht geeignet. Die spätere Document-/JobInput-Integration muss den vereinbarten Grafik-/Auswahlausschnitt ausdrücklich ableiten. Das bestehende Framing bleibt unverändert. Eine horizontale oder vertikale Grafik darf ein Rechteck mit einer Nullausdehnung haben.

## Anfrage und Positionen

`Request` enthält `mode`, `anchor`, `expectedSourceFingerprint` und optionale Positionsstände. Die Anker heißen `LowerLeft`, `LowerCenter`, `LowerRight`, `CenterLeft`, `Center`, `CenterRight`, `UpperLeft`, `UpperCenter`, `UpperRight`; ihre Reihenfolge läuft in kanonischen Koordinaten von unten links zeilenweise nach oben rechts.

Ein `PositionSnapshot` enthält einen ausdrücklich übergebenen Punkt in kanonischen Millimetern, `known`, den passenden Geräteprofilfingerprint, einen 64-stelligen Koordinatenrahmenfingerprint und eine positive Positionsrevision. `known` ist eine Behauptung des Aufrufers, kein Homingnachweis. Ein bloß gemeldetes 0,0 wird niemals als bekannte Referenz abgeleitet. Bei Änderung von Referenz, Arbeitsnullpunkt oder Koordinatenkonvention muss der Aufrufer einen anderen Rahmenfingerprint liefern.

- `Absolute`: Entwurfskoordinaten bleiben erhalten, der Anker wird ignoriert. Ein vorhandener expliziter Kopfstand wird als Anfahrt verwendet. Ohne diesen Stand bleibt die Vorschau auf dem ursprünglichen nominellen Planstart; `explicitApproach()` ist ausdrücklich `false`. Die tatsächliche erste Anfahrt ist dann unbekannt.
- `CurrentPosition`: Ein bekannter `startPosition`-Stand ist verpflichtend. Der ausgewählte Punkt des Grafikrechtecks wird genau auf diesen Punkt gelegt.
- `UserOrigin`: Bekannter `userOrigin` und tatsächlicher `startPosition`-Stand sind verpflichtend. Der Anker wird auf den gespeicherten Ursprung gelegt, während die Anfahrt vom tatsächlichen Kopfstand ausgeht. Beide Stände müssen zum gleichen Koordinatenrahmen gehören; der Ursprung darf älter sein, weil zwischen Setzen und Start verfahren werden kann.

Die relative Verschiebung lautet `Zielpunkt − Grafikanker`. Auswahl- und Designursprung werden nicht automatisch vertauscht. Eine veraltete Quellidentität oder ein fehlerhafter Positionsvertrag liefert ein ungültiges Ergebnis ohne Bewegungen.

## Bewegungs- und Wiederverwendungsvertrag

`place(plan, source, request)` liefert ein `Result` mit nur lesbarem Planzugriff, Translation, verschobenen Grafikgrenzen und Quell-/Anfragefingerprint. `matches(plan, source, request)` prüft die Wiederverwendbarkeit gegen einen aktuellen Quellsnapshot und Positions-/Anfragestand. Eine geänderte Kopfpositionsrevision invalidiert auch eine vorherige Vorschau mit denselben Zahlenkoordinaten.

Die Verarbeitungspfade, Überscansegmente und inneren Leerfahrten werden nur verschoben. Leistung, Vorschub, Reihenfolge, Ebene und Reliefschicht bleiben erhalten. Eine vorhandene initiale Eilfahrt erhält den expliziten Kopfstand als Anfang. Beginnt der Quellplan direkt mit einer Bearbeitungsbewegung, wird eine eigene laserfreie Eilfahrt mit Leistung 0 vorgeschaltet. Es entsteht keine gravierende Verbindung vom aktuellen Kopf zur ersten Kontur. Es gibt keine automatische Rückfahrt oder neue Endposition.

Vor einem gültigen Ergebnis werden alle Anfangs- und Endpunkte einschließlich Anfahrt und Überlauf im tatsächlichen Profilarbeitsraum geprüft. Alle Segmente müssen zusammenhängend sein. Nicht endliche Koordinaten, ungültige Vorschübe/Leistungen, Leistung auf Eilfahrten oder bei einer stationären Bewegung, unbekannte Modi/relative Anker und widersprüchliche Positionen werden abgelehnt. Kein Pfad wird abgeschnitten oder still verschoben, um ein ungültiges Ergebnis passend zu machen. Der bestehende Rahmen von maximal 2000 mm pro Achse und 500000 Bewegungen gilt auch einschließlich einer hinzugefügten Anfahrt. Weg- und Zeitschätzwerte werden für den neuen Bewegungsplan neu berechnet; sie sind keine gemessenen Maschinenzeiten.

Der originale Plan und dessen eventuell vorhandener Ausgabecache bleiben unverändert. Ein geänderter Bewegungsplan übernimmt keine gültigen alten Befehle. `Result::plan()` ist Vorschau-/Planungsdatenzugriff; eine gespeicherte GCode-Datei oder ein gültiger Profilfingerprint allein berechtigt weiterhin nicht zum Öffnen eines Controllers oder Starten eines Jobs. Controllerfähigkeiten und insbesondere Galvo-Beschränkungen müssen zusätzlich in der späteren Adapter-/Oberflächenintegration beachtet werden.

## Prüfung und offene Integration

`jobplacement` prüft unabhängig vorgegebene Koordinaten aller neun Anker, Zentrum/Ecke, absolute Koordinaten, unterschiedlichen Benutzerursprung und Kopfstand, laserfreie Erstfahrt auch bei direkt beginnendem Laserkonturpfad, Design-/Auswahlscope, beidseitigen Rasterüberlauf, nicht passenden tatsächlichen Startpunkt, veraltete Geometrie-/Profil-/Positionsstände, ungültige oder unbekannte Positionen, unterbrochene Pläne, Quellcacheerhalt, Abbruch und das Bewegungsbudget.

Die Änderung bearbeitet keine `JobPlan`-/Document-/Controller-Implementierung und fügt keine Main.qml-Bedienung hinzu. Die sichere Durchleitung von Grafikquelle, Simulationsstand, Vorschau, Auswahlfiltern und gespeicherter Projektkonfiguration folgt als eigenes Integrationspaket. Ohne diese Integration ist F39 nicht als vollständig bedienbar oder physisch abgenommen geschlossen. Reale Referenz-, Maschinenkoordinaten- und Zubehörprobleme bleiben unverändert offen.

Prüfstand 21.11.2026: **10 QtTest-Ergebnisse einschließlich Initialisierung/Abschluss, 0 Fehler, 0 übersprungene Fälle**; dazu `controlleradapter` bestanden. Beide CTest-Programme bestanden zusammen in 0,99 s; die anschließende zusätzliche Prüfung stationärer Leistungsbewegungen bestand ebenfalls. 234 lokale Dokumentziele lösen auf. Protokolle liegen unter `/home/mathias/benslab/analysis/blazer-rebuild-20261121/agent-job-placement/`. Keine Geräteverbindung, Installation oder reale Ausgabe.
