# Rahmenfahrt für Auftrag, Auswahl und Hilfsformen

Ab **0.9.0-beta.4-dev** lässt sich unter **Gerät → Rahmenfahrt · Laser aus**
festlegen, welche Grenze abgefahren werden soll. Beim Anwendungsstart ist
**Laserauftrag** ausgewählt. Das Rahmenziel gilt für die Sitzung; es verändert
weder die Projektdatei noch den Undo-Verlauf. Diese Erweiterung gehört noch
nicht zum unveränderten [beta.3-Kandidaten](beta/CANDIDATE_BETA3.md).

| Rahmenziel | Verwendete Grenze |
|---|---|
| Laserauftrag | Geplante Bearbeitungswege des aktiven Blatts, einschließlich Überlauf und anderer nicht eilender Zwischenwege; keine Anfahrt vom Nullpunkt und keine Eilgänge zwischen Objekten |
| Sichtbare Auswahl | Sichtbare ausgewählte Objektkästen des aktiven Blatts, einschließlich ausgewählter Hilfsformen oder Objekte ohne eingeschaltete Bearbeitung |
| Hilfsformen T1 + T2 | Sichtbare Hilfsformen beider Zuordnungen auf dem aktiven Blatt |
| Nur Hilfsformen T1 / T2 | Sichtbare Hilfsformen der ausdrücklich gewählten Zuordnung auf dem aktiven Blatt |

Die Anzeige nennt X/Y sowie Breite und Höhe in der gewählten
[mm-/Zolleinheit](EDITOR_UNITS.md). Die Rahmenfahrt bleibt ein Rechteck um diese
Grenzen. Sie folgt keiner einzelnen Kontur. Die Wahl einer Hilfsform aktiviert
keine Bearbeitung: [T1/T2](TOOL_SHAPES.md) bleiben aus Laserwegen, Vorschau,
GCode und CSV-Ausgaben ausgeschlossen. Unendliche [Hilfslinien](GUIDES.md)
sind keine begrenzten Objekte und werden nicht mit eingerahmt.

Für **Laserauftrag** muss die aktuelle Berechnung fertig und gültig sein.
Ein Auftrag ausschließlich aus Hilfsformen bleibt als Laserauftrag ungültig;
seine Hilfsformen können trotzdem ausdrücklich eingerahmt werden. Für Auswahl
und Hilfsformen gelten die sichtbaren Objektkästen, unabhängig von ihren
Bearbeitungswerten. Daher kann ihre Grenze von der berechneten Bearbeitungsgrenze
abweichen.

Ein fehlendes oder außerhalb des logischen 200×150-mm-Arbeitsraums liegendes Ziel
wird nicht freigegeben. Gerade Bearbeitungswege erhalten ein mindestens 0,1 mm
breites beziehungsweise hohes Rechteck, das vollständig innerhalb des Betts
bleibt. Bei importierten oder umgeformten dünnen Pfaden bleibt auch ihr
Mindest-Objektkasten innerhalb des Betts, wenn die Geometrie selbst darin liegt;
die physischen Pfadpunkte werden dabei nicht verschoben. Der tatsächliche G54-Versatz und die aktuelle Position werden vor der
Bewegung zusätzlich vom GRBL-Backend geprüft. Ein logisch passender Rahmen kann
bei verschobenem Arbeitsnullpunkt physisch außerhalb liegen und wird dann
abgewiesen; siehe [GRBL-Ablauf](GRBL_WORKFLOW.md).

**Rahmenfahrt** sendet weiterhin Laser-Aus und absolute Millimeter-Eilgänge in
G54. Das Rahmenziel löst selbst keine Bewegung aus; die Schaltfläche muss
betätigt werden. Während laufender Bewegung oder Fokusausgabe ist die Wahl
gesperrt. Beim Klick werden die Grenzen aus dem aktuellen Projekt erneut gelesen.
Eine noch laufende Auftragsberechnung gibt die Schaltfläche nicht frei.
**Stopp** bricht die Rahmenfahrt ab.

Der vorhandene Rahmen- und Ursprungsbezug der
[Referenz für Gantry-Laser](https://docs.lightburnsoftware.com/2.2/Reference/LaserWindow/#framing)
bietet den Vergleichsrahmen. Unsere Rahmenfahrt verwendet ausschließlich
Laser-Aus. Reale Platzierungsgenauigkeit, Gerät-/USB-/Schutzkontaktfälle und
native Bedienungsabnahmen bleiben eigene T06-/T08-Prüfungen. Die Softwarefälle
verwenden Simulator und virtuelle GRBL-Anschlüsse.
