# Hilfsformen T1 und T2

Ab **0.9.0-beta.4-dev** lassen sich Objekte als **T1** oder **T2** kennzeichnen.
Damit können Materialumrisse, Schablonen, Abstände oder Textvermerke in einer
Zeichnung bleiben. Die Grundidee entspricht den
[Tool Layers der Referenz](https://docs.lightburnsoftware.com/2.2/Collections/MaterialUtilization/#tool-layers).
Diese Erweiterung gehört noch nicht zum unveränderten
[beta.3-Kandidaten](beta/CANDIDATE_BETA3.md).

## Zuordnen und bearbeiten

In der Objektliste enthält die Bearbeitungsauswahl neben den Laseroperationen
**T1 · Hilfsform** und **T2 · Hilfsform**. Unter **Werkzeuge → Editor** können die
beiden Schaltflächen die gesamte aktuelle Auswahl zuordnen. Die Objektliste
kennzeichnet jede Hilfsform mit T1 oder T2 und einer eigenen Farbe. Rechts steht
**keine Laserausgabe**; Leistungs- und Geschwindigkeitsfelder werden für die
Hilfsform ausgeblendet.

Rechtecke, Ellipsen, Pfade, Texte und eingebettete Grafiken können Hilfsformen
sein. Positionieren, Drehen, Spiegeln, Gruppieren, Kopieren und Pfadbearbeitung
bleiben verfügbar. Sichtbare Hilfsformen sind wie andere Objekte Fangziele für
Kanten und Mittelpunkte. Die [geraden Hilfslinien](GUIDES.md) sind zusätzlich
als eigene Blattmetadaten verfügbar.

Um eine Hilfsform wieder zu bearbeiten, ausdrücklich eine Laseroperation in der
Objektliste oder **Gravieren/Schneiden** rechts wählen. Diese Zuordnung ist ein
einziger rücknehmbarer Schritt. Die früheren Objektwerte und Zusatzoperationen
bleiben während der Hilfsform-Zuordnung gespeichert; vor der erneuten Ausgabe
sind die nun angezeigten Werte zu prüfen. Das Ändern einer globalen Operation
aktiviert eine Hilfsform nicht. Bei gemischten booleschen Pfadoperationen
übernimmt das Ergebnis wie bisher die Zuordnung des ersten ausgewählten Objekts.

## Ausgabe, Zwischenablage und Speicherung

Hilfsformen gehen niemals in Laserwege, die Auftragsvorschau, Auftragsgrenzen,
die normale Auftragsrahmenfahrt oder CSV-Ausgaben ein. Auch Formen außerhalb des Arbeitsraums vergrößern
den Auftrag nicht. Ein Blatt ausschließlich aus Hilfsformen besitzt keinen
ausführbaren Laserauftrag. Der SVG-Export lässt Hilfsformen ebenfalls aus.
Eine [ausdrücklich gewählte Rahmenfahrt](FRAMING.md) kann T1/T2 oder die sichtbare Auswahl einrahmen, weiterhin ausschließlich mit Laser-Aus.

Die [Blazer-Systemzwischenablage](CLIPBOARD.md) bewahrt die Zuordnung auch beim
Ausschneiden und beim Einfügen in ein anderes Projekt. Sobald eine Auswahl
Hilfsformen enthält, stellt sie ausschließlich das Blazer-Objektformat bereit.
Ein allgemeiner SVG-, Bild- oder Text-Ersatz könnte diese Zuordnung verlieren.
Das separate Speichern eines Bilds bleibt eine ausdrückliche Bildexportfunktion.

Projekte, eigene Vorlagen und Wiederherstellungen bewahren die Zuordnung;
Blattkopien und Projektimport ebenfalls. Solange mindestens eine Hilfsform
vorhanden ist, wird **Projektformat 10** gespeichert. Ein zusätzlich abweichendes Geräteprofil verwendet Format 11 einschließlich dieser Hilfsformfelder. Es verlangt je Objekt ein
ganzzahliges `toolLayer` mit 0 (Laserobjekt), 1 (T1) oder 2 (T2), zusätzlich zu
den Hilfsliniendaten von Format 9. Ältere Versionen müssen Format 10 ablehnen.
Damit wird eine Hilfsform beim Öffnen mit einer älteren Anwendung nicht
stillschweigend zum Laserobjekt. Ohne Hilfsformen bleibt je nach Hilfsliniendaten
Format 8 oder 9 erhalten.

## LightBurn-Import und Grenzen

Der begrenzte native LightBurn-Leser übernimmt `CutIndex` 30/31 als T1/T2.
Eine ausdrücklich als `Tool` deklarierte `CutSetting` wird ebenfalls ohne
Laserausgabe übernommen. Die Zuordnung einer Textform gilt auch für ihre
Kontursicherung. An Gruppen vererbte Zuordnungen können einzelne Kinder
überschreiben. MillMage-Grafiklayer werden nicht anhand ihrer Nummer als
LightBurn-Werkzeuge interpretiert. Bearbeitungsparameter werden weiterhin
nicht aus fremden Projekten übernommen.

Ein unabhängiges öffentliches [LightBurn-Projekt von TTL Makerspace](https://code.ttlmakerspace.com/TTL/DesignForLasers/src/commit/e0e266b0d0cd3eb0c6270afac166253f65a81225/week5/files/BeaverBox_complete.lbrn2) lieferte 21 T1-Formen als
privaten Referenzkorpus. 14 einzeln gespeicherte Formen ohne externe
Geometrieverweise blieben ohne Ausgabe; zusammen mit einer Kontrollkontur
entstand bytegleicher GCode wie für die Kontrollkontur allein. Nach Ergänzung der [geteilten Geometriedaten und kompakten Linienzüge](VECTOR_IMPORTS.md) wird auch das vollständige Original mit 39 Objekten, darunter 21 T1-Hilfsformen, gelesen. Geometrie und Kontrollausgabe wurden unabhängig verglichen. Weitere native Varianten bleiben offen; dies ist keine verlustfreie Projektmigration. Keine fremden Grafikdaten wurden in das Repository übernommen.

Weitere native Varianten, Werkzeug-Framing, Eingabevarianten und die tatsächliche
Desktop-/Fremdprogramm-Abnahme bleiben in F04/F07/T08 offen.
