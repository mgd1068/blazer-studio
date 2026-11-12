# Längen in Millimetern und Zoll

Ab **0.9.0-beta.4-dev** kann im Editor unten rechts zwischen **mm** und **in**
(Zoll) gewechselt werden. Die Auswahl gilt lokal für die Anwendung und wird in
den Einstellungen gespeichert. Sie gehört noch nicht zum unveränderten
[beta.3-Kandidaten](beta/CANDIDATE_BETA3.md).

## Eingabe und Darstellung

Längenfelder zeigen die gewählte Einheit an. Das betrifft Positionen, Größen,
Abstände, Versätze und andere als Länge gekennzeichnete Felder, einschließlich
entsprechender Material-/Reliefdialoge. Andere Einheiten wie Pixel, Grad, Prozent,
Watt und mm/min bleiben ausdrücklich unverändert. Herstellerreferenztabellen,
benannte Maßproben und Geräteanzeigen behalten ihre angegebene Einheit.

Dezimalkomma und Dezimalpunkt sind erlaubt. In jedem Längenfeld können außerdem
eine ausdrückliche Einheit und Exponenten verwendet werden:

| Eingabe | Physische Länge |
|---|---|
| `1,25 in` | 31,75 mm |
| `25.4 mm` | 1 Zoll |
| `2"` | 50,8 mm |
| `1e-2 in` | 0,254 mm |
| `0.5` | 0,5 in im Zollmodus beziehungsweise 0,5 mm im Millimetermodus |

Brüche wie `1/2 in`, Fußangaben und allgemeine Rechenausdrücke werden nicht
interpretiert. Eine ungültige Eingabe ändert den Wert nicht. Die bestehenden
geometrischen und parameterbezogenen Grenzen gelten für die physische Länge.

Die Anzeige ist gerundet: Zollwerte erhalten mindestens vier Nachkommastellen,
feiner eingestellte Felder entsprechend mehr. Ein unverändert verlassenes Feld
schreibt diese Rundung nicht in das Modell zurück. Wird eine noch nicht
übernommene Eingabe während eines Einheitenwechsels gehalten, behält sie ihre
ursprüngliche Eingabeeinheit bis zum Übernehmen; ein ausdrücklich angehängtes
`mm` oder `in` hat Vorrang. Nach dem Übernehmen zeigt das Feld die neue Einheit.
Dies verhindert eine unbemerkte Umdeutung eines Entwurfs. Undo/Redo im
fokussierten Textfeld betreffen den Eingabetext, nicht den Projektverlauf.

## Lineale und Fangraster

Die Arbeitsfläche bleibt physisch 200×150 mm groß. Im Zollmodus zeigen die
Lineale Zoll, das kleine Raster 0,1 in und das große Raster 1 in. Alle Y-Marken
und Rasterlinien beziehen sich weiterhin auf den unteren Arbeitsraumrand.
Beim Verschieben rastet die Auswahl im Zollmodus bei 0,025 in ein; im
Millimetermodus bleibt der bisherige Schritt von 0,5 mm erhalten. Alt unterdrückt
zusätzlichen Objekt-/Kantenfang, nicht das Grundraster. Zoomen und Verschieben
der Ansicht ändern die physischen Maße nicht.

Der Einheitenwechsel selbst verändert weder Projektinhalt noch Undo-Verlauf,
Materialdaten oder berechneten GCode. Projektformat 8, SVG-Übergaben und
Gerätebefehle bleiben intern in Millimetern; GRBL-Ausgabe verwendet weiterhin
G21. Es entsteht dadurch keine zusätzliche Maschinen- oder Materialgenauigkeit.
Wenn die Einstellung nicht gespeichert werden kann, bleibt die bisherige Einheit
aktiv und eine Fehlermeldung erklärt den fehlgeschlagenen Wechsel.

## Nachweise und verbleibende Abnahme

`lengthUnitPreferenceIsValidatedPersistentAndIndependent` prüft Speicherung,
Neuladen, ungültige Einstellungen, Dateifehler und erfolgreichen neuen Versuch
bei unverändertem Projekt und GCode.
`lengthFieldsConvertSuffixesAndPreserveUneditedPrecision` prüft die echten
QML-Längenfelder, Einheitenzusätze, Entwürfe, unveränderte Genauigkeit und
unabhängige Geschwindigkeitsfelder.
`imperialDragSnapsInWorldUnitsAndIsOneUndoStep` prüft eine tatsächliche
Verschiebung über den Workspace-Ereignispfad gegen unabhängig berechnete
Millimeterkoordinaten und genau einen rücknehmbaren Schritt.

Der volle QML-Workflow wählt die Einheit per Oberfläche, übernimmt Eingaben in
beiden Einheiten und prüft unveränderte Projektbytes, Verlauf und GCode beim
reinen Umschalten. Helle und kompakte dunkle Ansicht werden geprüft. Native
Desktop-/externe Bedienung bleibt T08; Hilfslinien und weitere Eingabevarianten
bleiben als offene Aufgaben der [Familie F07](FUNCTION_ACCEPTANCE.md) erhalten.
