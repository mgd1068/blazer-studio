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

Der reine Helper ändert weder Controller noch Gerätefreigabe. Die nachfolgende Document-/Planungsintegration ist unten beschrieben. Simulatorlieferant und Positionierungsdialog wurden anschließend integriert; der neue Auswahlworkflow unten ist zunächst als Backend verfügbar. F39 ist damit weiterhin nicht als vollständig bedienbar oder physisch abgenommen geschlossen. Reale Referenz-, Maschinenkoordinaten- und Zubehörprobleme bleiben unverändert offen.

Prüfstand 21.11.2026: **10 QtTest-Ergebnisse einschließlich Initialisierung/Abschluss, 0 Fehler, 0 übersprungene Fälle**; dazu `controlleradapter` bestanden. Beide CTest-Programme bestanden zusammen in 0,99 s; die anschließende zusätzliche Prüfung stationärer Leistungsbewegungen bestand ebenfalls. 234 lokale Dokumentziele lösen auf. Protokolle liegen unter `/home/mathias/benslab/analysis/blazer-rebuild-20261121/agent-job-placement/`. Keine Geräteverbindung, Installation oder reale Ausgabe.


## Document-, Hintergrundplanungs- und Exportintegration

`PlacedJobPlanning::Input` enthält einen einzigen unveränderlichen `JobInput` sowie den dazugehörigen Platzierungsintent und ausdrücklich übergebene Laufzeitpositionsstände. `Document::job()`, `JobPlanner::build(const Document&)` und `JobPreparation` verwenden denselben Wrapper. Der kanonische `JobInput` transportiert auch die Ausgabemaske und kopierte Objektkennungen; der rohe `JobPlanner::build(const JobInput&)` filtert diese vor der Pfadplanung.

Die Ankergrenzen werden jetzt aus **Artwork-Bounds genau dieses JobInput** abgeleitet: nur sichtbare Laserobjekte auf dem aktiven Blatt mit mindestens einer wirksamen aktivierten Operation und positiver Leistung. Hilfsformen, ausgeblendete Objekte, andere Blätter und deaktivierte oder leistungslose Bearbeitungen vergrößern den Ursprung nicht. Primäre Objektparameter werden wie im Planner über die Ebenenwerte gelegt; zusätzliche Operationen verwenden ihre eigenen Ebenenwerte. Der Geometriefingerprint bindet aktives Blatt, Profil, Operationen und wirksame Objektgeometrie einschließlich Pfad, Stil, Parametern und Assetinhalt. Die Editorobjekte werden nicht verschoben.

Die Document-Schnittstelle bietet typisierte Qt-Enums `StartMode` (`AbsoluteCoordinates`, `CurrentPosition`, `UserOrigin`) und `JobOrigin` (die neun Anker), die Eigenschaften `startMode`, `jobOrigin` und `placementSummary` sowie die bewussten Aufrufe `setStartMode`, `setJobOrigin`, `captureUserOrigin`, `confirmUserOrigin` und `clearUserOrigin`. `placementSummary` nennt gültigen/gesperrten Zustand, Grund, Quell- und Zielgrafikgrenzen, Versatz, Quellen-/Anfragefingerprint und `approachKnown`. Für absolute Altdaten ohne Kopfstand bleibt das bisherige Bewegungsprogramm unverändert und die tatsächliche Anfahrt ausdrücklich unbekannt.

Nur C++ darf `setPreviewPosition(PositionSnapshot)` oder `clearPreviewPosition()` aufrufen. Ein ungültiger neuer Stand löscht einen eventuell alten bekannten Stand, statt ihn weiterzuverwenden. Positionsänderungen verändern weder Dirty-Flag noch Dateiinhalte oder Undo-Verlauf, invalidieren aber die geplante Ausgabe. Neue Koordinatenrahmen oder Ausgabeprofile verwerfen die Bestätigung des gespeicherten Ursprungs. Der Aufrufer muss den Rahmen auch bei Wechsel des einzelnen Geräts ändern, selbst wenn zwei Geräte denselben Profilfingerprint besitzen.

`captureUserOrigin` übernimmt den ausdrücklich bekannten Vorschaupunkt als Koordinatenintent und bestätigt ihn im aktuellen Rahmen. `confirmUserOrigin` bestätigt dagegen bewusst bereits gespeicherte Koordinaten im jetzt übergebenen Rahmen. Keine der beiden Methoden erzeugt eine bekannte Kopfposition oder eine Referenz. Öffnen, Vorlagen, Recovery und Undo/Redo stellen nur den Intent wieder her; eine User-Ursprungsbestätigung wird nie aus einem Snapshot restauriert. Ohne ausdrückliche neue Bestätigung bleibt User-Ausgabe gesperrt. Es erfolgt kein Ersatz durch absolute Ausgabe.

Die Hintergrundplanung nimmt Intent und Positionsstand als Werte in denselben Workerauftrag auf. Eine neue Position invalidiert Revision und Befehlscache; Ergebnisse alter Worker dürfen den aktuellen Plan nicht mehr veröffentlichen. Während neuer Berechnung oder nach Abbruch bleibt der Export gesperrt. Der normale GCode-Export schreibt genau das Programm des geprüften Vorschauplans. Die Datei enthält auf den übergebenen Positionsstand aufgelöste G90-Koordinaten, keinen später beliebig neu interpretierten G91-Kopfursprung und keine Referenzfreigabe. Ein Simulator darf während laufender Bewegung keine Positionsänderungen in diesen Document-Stand zurückspeisen; der spätere Lieferant muss nur im Leerlauf beziehungsweise auf ausdrückliche Anforderung aktualisieren. Diese Schicht liest keine Maschine und kann deren Betriebszustand selbst nicht feststellen.

## Ausgewählte Ausgabe und getrennte Bezugsfläche (Backend)

Nach den offiziellen [LightBurn-2.2-Regeln zu Cut Selected Graphics / Use Selection Origin](https://docs.lightburnsoftware.com/2.2/Reference/CutSelectedGraphics/) (geprüft 21.11.2026, Referenzversion 2.2.01) sind dies zwei getrennte Entscheidungen:

- `cutSelectedGraphics` begrenzt die tatsächlich geplante Ausgabe auf ausgewählte, weiterhin wirksame Objekte. Ohne Auswahl oder bei ausschließlich unwirksamen ausgewählten Objekten bleibt der Auftrag ungültig; es gibt keinen Rückfall auf alle Objekte.
- `useSelectionOrigin` verwendet bei aktivem Auswahlauftrag und Current/User die ausgewählten Grafikgrenzen als Neunpunkt-Bezug. Bei deaktivierter Option bleiben alle wirksamen Grafikgrenzen des aktiven Blatts der Bezug. Damit kann ein Teilauftrag seine ursprüngliche Registrierung behalten. In Absolute oder ohne Auswahlmaske ist die Option inaktiv und verändert die Koordinaten nicht.

Die Document-API bietet beide bool-Eigenschaften sowie `setCutSelectedGraphics(bool)` und `setUseSelectionOrigin(bool)`. Das sind explizite Projektabsichten mit Undo. Eine reine Editor-Auswahl verändert weiterhin nicht das Dirty-Flag, invalidiert bei aktiver Maske aber den Job, Hintergrundauftrag und die Rahmenrevision. Die Kennungen gehören zum unveränderlichen Worker-Snapshot. Alte Ergebnisse und Rahmenziele werden auch bei identischen Begrenzungsrechtecken verworfen. Eine Auswahl auf anderen Blättern aktiviert diese Blätter oder deren Ausgabe nicht.

Vollentwurf, Ausgabeausschnitt und Ursprungsbezug stammen aus demselben Snapshot. Versteckte Formen, T1/T2, andere Blätter sowie deaktivierte oder leistungslose Operationen werden weder durch die Auswahl aktiviert noch als Ursprung verwendet. Unausgewählte Objekte werden vor der Pfadplanung ausgespart; ihre beispielsweise außerhalb des Betts liegende Geometrie erzeugt keinen fehlerhaften Gesamtausgabeplan als Zwischenstufe. Die Ankergrenzen verwenden weiterhin Artwork-Grafikgrenzen statt Eilfahrt oder Rasterüberlauf. Alle tatsächlich ausgegebenen Bewegungen einschließlich Überlauf und erster Anfahrt müssen nach Translation im Arbeitsraum liegen. Die Editorobjekte bleiben unverändert.

`placementSummary` unterscheidet `outputScope` und `originScope` (jeweils `design`/`selection`), `selectionOriginActive`, `designGraphicsBounds` (vollständige Quellgrafik), `sourceGraphicsBounds` (ausgegebene Quellgrafik), `sourceOriginBounds` (Quellbezug), `graphicsBounds` (tatsächliche ausgegebene Zielgrafik) und `originGraphicsBounds` (verschobener Bezug). Der Quellenfingerprint bindet auch ausgewählte Objektidentitäten; zwei deckungsgleiche Objekte sind keine austauschbaren Quellstände.

Vorschau und normaler GCode-Export verwenden denselben maskierten und platzierten Plan. SVG bleibt ein Export der bearbeitbaren Entwurfsgeometrie. CSV-Stapel und separat generierte Materialtestdateien lehnen relative Startmodi **und aktive Auswahlmasken** bis zu ihrer eigenen konsistenten Integration ausdrücklich ab; es gibt keine stille Gesamtausgabe. Die Auswahlbedienung im Hauptfenster ist in diesem Backend-Slice noch nicht integriert. Relative reale Ausgabe bleibt gesperrt; weder die Flags noch ein exportiertes G90-Programm erteilen eine Gerätefreigabe.

Bei einem Seitenwechsel wird `refreshPlanning()` verwendet: dadurch bleiben Intent und expliziter Positionsstand auch im Worker erhalten, und alte Rahmenziele werden selbst bei identischen Grafikgrenzen ungültig. Das behebt den vorherigen direkten `submit(jobInput())`-Umweg.

## Projektintent in Format 13

Sobald Startmodus, Anker oder gespeicherte Ursprungkoordinaten vom leeren absoluten Standard abweichen, verwendet das Projekt **Format 13**. Es enthält sämtliche Voraussetzungen von Format 12: Geräteprofil, Hilfslinienfelder und je Objekt `toolLayer` sowie ein striktes `engraveOutline`, auch wenn diese Werte 0/false sind. Hinzu kommt exakt:

```json
"placement": {
    "version": 1,
    "mode": 2,
    "anchor": 4,
    "userOrigin": { "x": 50, "y": 50 }
}
```

Modi sind ganzzahlig 0–2, Anker 0–8; `userOrigin` ist null oder genau ein Objekt mit endlichen Koordinaten in 0–2000 mm. Unbekannte oder fehlende Intentfelder und falsche Typen werden atomar abgelehnt. Der gleiche Intent unter einer alten Formatnummer wird ebenfalls abgewiesen. Leser bis Format 12 müssen die neue Datei ablehnen, statt ihren Startmodus zu verlieren. Absolute Standardprojekte behalten die vorherige passende Formatversion und das bisherige Ausgabeprogramm.

**Positionsstände, Rahmenidentitäten, Revisionen, bekannte Referenzen, Simulatorzustände oder Benutzerursprungsbestätigungen werden nie serialisiert.** Ein erneut geöffnetes User-Projekt benötigt einen bekannten aktuellen Vorschaupositionsstand und eine ausdrückliche Bestätigung seiner gespeicherten Koordinaten. Koordinaten sind damit Projektabsicht, keine portable physische Freigabe.

`placementdocument` prüft die vollständige Softwarekette an wirklichen Document-/Workerinstanzen mit unabhängigen Sollkoordinaten: Ausgabe-Bounds statt Überlauf, ausgeschlossene Objekte, unveränderte Editorobjekte, Vorschau-/Exportbytegleichheit, tatsächliches Speichern/Öffnen, striktes Format 13, Undo ohne Proof-Restauration, Rahmen-/Positionswechsel, Abbruch, blockierte alternative Exporte sowie verworfene langsame Worker mit altem Positionsstand. Root liefert anschließend die Simulator-/QML-Durchleitung; diese Änderungen öffnen keine Hardware.


Prüfstand der Document-Integration am 21.11.2026: `placementdocument` **10 QtTest-Ergebnisse, 0 Fehler, 0 übersprungene Fälle**, einschließlich identischer Laufzeitstands-No-ops und exakter Intentkoordinaten für Dirty/Undo. `core`, `expansion`, `deviceworkspace`, `release`, `job` und die Integrationsfälle bestanden zusammen 6/6 CTest-Programme in 14,32 s; zuvor bestanden zusätzlich `stability` und der reine `jobplacement`-Vertrag (3/3 zusammen mit Integration, 14,41 s). 235 lokale Dokumentziele lösen auf. Evidenz unter `/home/mathias/benslab/analysis/blazer-rebuild-20261121/agent-placement-document/`. Keine Main-/Machine-Änderung, Installation oder reale Geräteaktion.

## Simulator provider and frame requests

Machine supplies a simulator-only position snapshot while idle. Its frame binds
an instance nonce, output profile and selected physical-device identity; exact
point/frame changes increment the position revision. Running or paused simulation
freezes the start snapshot. Real sessions never consume simulator coordinates.
Saved user origins are portable intent; reopening or changing coordinate frames
requires explicit confirmation, and no confirmation grants hardware access.

A frame request binds the Document instance and current planning revision. Machine
compares the entire request with a freshly computed canonical frame, including
scope, selection and bounds. Jogging, source/parameter edits, a different Document
or modified bounds invalidate old requests. Relative real-machine framing and
job output remain unavailable pending actual coordinate commissioning.


## Auswahlintent in Format 14

Sobald mindestens eines der beiden Auswahlflags wahr ist, verwendet das Projekt Format 14 mit allen Format-13-/12-Voraussetzungen. Das strikte `placement`-Objekt hat Version 2 und exakt sechs Felder: `version`, `mode`, `anchor`, `userOrigin`, `cutSelectedGraphics` und `useSelectionOrigin`. Beide neuen Werte müssen JSON-bool sein. Version-2-Intent unter Format 13, Version-1-Intent unter Format 14, unbekannte oder fehlende Felder und falsche Typen werden ohne Zustandsänderung abgelehnt. Ohne die neuen Flags bleibt die bisherige passende Version 8–13 erhalten. Vorhandene Editor-Auswahlmetadaten bleiben Editorzustand; Runtimepositionen, Frames, Revisionen und Ursprungsbestätigungen werden weiterhin nie gespeichert. Ein wieder geöffnetes User-Projekt verlangt ausdrücklich aktuelle Position und neue Ursprungsbestätigung.

Die unabhängigen `placementdocument`-Regressionen enthalten literal erwartete Ausgabe-/Bezugsrechtecke für alle drei Modi, neun Anker und beide Bezugsarten, wirklichen GCode-Dateiexport, leere/falsche Auswahl, T1/T2/hidden/disabled/andere Blätter, unveränderte Motionparameter, Frame- und Worker-Staleness, Undo ohne Proof-Restauration und striktes Format 14. Das prüft die Softwarekette; eine vollständige UI- oder physische Abnahme des neuen Auswahlworkflows ist damit nicht behauptet.

Prüfstand des Auswahl-Backends am 21.11.2026: `placementdocument` **91 QtTest-Ergebnisse, 0 Fehler, 0 übersprungen** (4,268 s), einschließlich aller drei maskierten/unmaskierten Rasterüberlaufvarianten. Die bestehenden CTest-Gruppen `placementdocument`, `jobplacement`, `deviceworkspace`, `release`, `core`, `expansion` und `job` bestanden 7/7 in 20,39 s vor der abschließenden Ergänzung der beiden weiteren maskierten Überlaufvarianten; danach wurde `placementdocument` erneut vollständig ausgeführt. Evidenz: `/home/mathias/benslab/analysis/blazer-rebuild-20261121/agent-selected-output/`. Kein Main-/Machine-/CMake-Eingriff und keine Hardwareaktion.

## beta.9: gesonderter lokaler Handstart

Der oben beschriebene Offlinevertrag erteilt weiterhin keine Gerätefreigabe.
beta.9 ergänzt dafür einen eigenen [Handstart-Ablauf](MANUAL_START.md) mit
getrennter [lokaler Controllerautorität](OPERATOR_LOCAL_FRAME.md). CurrentPosition
und neun Anker werden nach nichtbewegender lokaler Aufnahme sowie vollständiger
Raw-/tatsächlicher-Wire-Restwegprüfung ausdrücklich freigebbar. Die globale Referenz
bleibt unbekannt; Benutzerursprung und aktive Print-and-Cut-Registrierung werden
in diesem lokalen Pfad nicht übernommen. Mechanische Restwege sind echte gemessene
Operatorangaben, keine aus alten Zählern oder Profilmaßen erzeugten Werte.

Nach einem Job kann der Kopf am letzten Bearbeitungspunkt stehen bleiben. Eine
neue CurrentPosition-Planung verwendet die dann frisch lokal nachgeführte Lage;
eine neue Wegeprüfung und Bestätigung sind erforderlich. Die gesonderte Rahmenfahrt
kehrt zu ihrem eigenen Ausgangspunkt zurück und wird dafür separat geprüft.
