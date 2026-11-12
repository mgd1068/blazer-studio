# Hilfslinien und Ausrichtung

Ab **0.9.0-beta.4-dev** besitzt jedes Blatt eigene waagerechte und senkrechte
Hilfslinien. Sie unterstützen die Platzierung und sind keine Bearbeitungsobjekte.
Sie gehören noch nicht zum unveränderten [beta.3-Kandidaten](beta/CANDIDATE_BETA3.md).

Im Editor eine Linie vom oberen Lineal nach unten ziehen (waagerecht) oder vom
linken Lineal nach rechts (senkrecht). Erst das Loslassen übernimmt die Position;
Escape oder der Verlust des Mausgriffs verwirft den Entwurf. Die Position rastet
auf dem gewählten mm-/Zollraster ein. Eine bestehende, ungesperrte Linie kann an
einem freien Abschnitt verschoben werden. Wird sie links beziehungsweise oberhalb
der Arbeitsfläche oder aus der Ansicht gezogen, wird sie gelöscht. Objekte haben
beim Anklicken Vorrang vor einer darunterliegenden Linie.

Unter **Werkzeuge → Hilfslinien** lassen sich Linien auch hinzufügen, auswählen,
numerisch bearbeiten, sperren und löschen. Die Eingabefelder unterstützen
[Millimeter und Zoll](EDITOR_UNITS.md), einschließlich ausdrücklicher Einheit.
Die Auswahlliste nennt ihre Position stets in Millimetern. Gesperrte Linien bleiben
Fangziele; ihre Position und ihre Einzelentfernung sind gesperrt. Der ausdrücklich
beschriftete Befehl zum Löschen aller Linien dieses Blatts entfernt auch gesperrte.

Sichtbarkeit und Einrasten können projektweit getrennt geschaltet werden.
Ausgeblendete Linien sind keine Fangziele. Beim Verschieben von Objekten können
Kanten und Mittelpunkte an sichtbaren Hilfslinien einrasten. Magentafarbene
Ausrichtungslinien zeigen während der Bewegung das getroffene Ziel an. Alt
unterdrückt das zusätzliche Einrasten an Objekten und Hilfslinien; das Raster
bleibt aktiv. Die Erstellung einer neuen Hilfslinie schaltet ihre Anzeige ein.

Linien, Sperren und die beiden Schalter sind rückgängig zu machen. Blattkopien
erhalten unabhängige Linien; beim Löschen eines Blatts verschwinden seine Linien.
Projektimport fügt Linien aller Quellblätter mit neuen Kennungen auf dem aktiven
Zielblatt ein. Die Systemzwischenablage kopiert weiterhin ausgewählte
Bearbeitungsobjekte. Sie übernimmt keine Hilfslinien und verändert beim Einfügen
keine vorhandenen Hilfslinien des Zielprojekts.

Hilfslinien werden in Projekten, eigenen Vorlagen und Wiederherstellungen
aufbewahrt. Projekte mit Linien oder abweichenden Hilfslinienschaltern verwenden
**Format 9**; mit zusätzlichen [Hilfsformen T1/T2](TOOL_SHAPES.md) **Format 10**. Ohne Linien und mit beiden Schaltern eingeschaltet bleibt Format 8.
Frühere Versionen einschließlich beta.3 müssen Format 9 ablehnen. Grenzen:
höchstens 1000 Linien je Projekt, Positionen von −2000 bis +2000 mm; gültige,
eindeutige Kennungen und Blattzuordnung werden vor der Übernahme geprüft.

Die Linien gelangen weder in GCode, SVG-Ausgabe, Frame, Auftragsgrenzen noch in
Serienaufträge. Speichern oder Verschieben einer Hilfslinie verändert keine
Laserparameter. [Hilfsformen T1/T2](TOOL_SHAPES.md) erlauben zusätzliche bearbeitbare Platzhalter. Weitere
Eingabevarianten sowie native Desktop- und externe Bedienungsabnahmen bleiben
Teil der offenen F07-/T08-Arbeit.
