# Geometrieimporte: HPGL/PLT, GCode und native Projekte

Diese Importe gehören zur Entwicklung **0.9.0-beta.4-dev**. Sie sind noch nicht
Bestandteil des unveränderten [beta.3-Pakets](beta/CANDIDATE_BETA3.md).
Die gesamte Familie F04 bleibt in der [Funktionsabnahme](FUNCTION_ACCEPTANCE.md)
offen; die folgenden Abschnitte beschreiben begrenzte Geometrieübernahmen.

Unter **Grafik, GCode oder Modell importieren** können `.hpgl` und `.plt` eingelesen
werden. Es entstehen editierbare Pfade auf dem aktiven Arbeitsblatt. Der Import
öffnet keinen Geräteanschluss, führt keine Datei aus und startet keinen Auftrag.
Alle Stifte werden derselben anfänglichen Bearbeitung zugeordnet; Leistung,
Geschwindigkeit und weitere Bearbeitungsparameter sind im Projekt neu zu prüfen.

## Unterstützte Geometrie

| Befehle | Verhalten |
|---|---|
| `IN` | Initialisiert Position 0/0, absolute Koordinaten, Stift oben und keine Stiftauswahl; bereits gelesene Pfade bleiben erhalten |
| `IP`, `SC` | Explizite Bezugspunkte und anisotrope, isotrope oder Punkt-Faktor-Skalierung; siehe unten |
| `PA`, `PR` | Absolute beziehungsweise relative Koordinaten; der Modus bleibt für nachfolgende `PU`/`PD` erhalten |
| `PU`, `PD` | Stift heben/senken und optionale Koordinatenpaare; Fahrbewegungen werden nicht zu Schnittlinien |
| `SP` | Positive Stiftnummer 1–255 zeichnet; 0 oder fehlende Nummer deaktiviert Zeichnen. Nummern/Farben werden nicht als Operationen übernommen |
| `AA`, `AR` | Absolutes beziehungsweise relativ zum aktuellen Punkt angegebenes Zentrum; positiver Winkel gegen den Uhrzeigersinn im Weltkoordinatensystem |
| `CI` | Kreis mit positivem Radius, auch bei gehobenem Stift; stellt Position, Stiftzustand und Koordinatenmodus danach wieder her |
| `BZ`, `BR` | Kubische Bézierkurven mit absoluten beziehungsweise zum jeweiligen Segmentanfang relativen Kontrollpunkten |

Die Datei wird als eigenständige Zeichnung mit dem oben beschriebenen
Initialzustand gelesen. Üblich ist ein Beginn mit `IN;SP1;`.
Groß-/Kleinschreibung, Komma/Leerraum oder ein vorangestelltes Plus/Minus zwischen Zahlen und fehlende Semikolons
vor dem nächsten Befehlsnamen sind zulässig. Unterstützt werden Dezimalzahlen
ohne Exponenten.

**Maßstab und Lage:** 40 unskalierte Plottereinheiten ergeben 1 mm. Positive Y
zeigt wie im Blazer-Weltkoordinatensystem nach oben. Alle Pfade werden gemeinsam
so verschoben, dass die untere linke Grenze bei 0/0 liegt; Abstände und Orientierung
bleiben erhalten. Die ursprüngliche Lage zum Plotterursprung wird nicht erhalten.
Danach das gesamte Motiv an der gewünschten Stelle platzieren.

**Dateieigene Skalierung:** `IP x1,y1,x2,y2` definiert P1/P2 in Plottereinheiten.
Die Koordinaten werden auf ganze Einheiten gerundet. Bei gleicher X- oder
Y-Koordinate wird die entsprechende P2-Koordinate um eine Einheit erhöht.
`IP x1,y1` verschiebt ein vorhandenes P2 um dieselbe Differenz; ohne bekanntes P2
bleibt dessen Abstand unbekannt. `IP` ohne Parameter setzt P1 auf 0/0 und löscht
das bekannte P2. Eine Drucker-Seitengröße wird nicht aus dem Laserbett geraten.

- `SC xmin,xmax,ymin,ymax[,0]` skaliert beide Achsen unabhängig.
- Typ `1` verwendet auf beiden Achsen den kleineren Absolutfaktor; optionale
  Randanteile `left,bottom` von 0 bis 100 bestimmen die Lage, Standard je 50.
  Die Anteile folgen der P1→P2-Richtung, auch bei umgedrehten Bezugspunkten.
- `SC xmin,xfaktor,ymin,yfaktor,2` ordnet xmin/ymin P1 zu und gibt die Zahl
  der Plottereinheiten pro Benutzereinheit direkt an. `SC0,40,0,40,2` bedeutet
  daher Benutzereinheiten in Millimetern und benötigt kein bekanntes P2.

Typ 0/1 benötigt ein explizit bekanntes P2 aus IP. Typ 2 verwendet anfangs P1=0/0.
Negative Faktoren oder umgekehrte Bereiche spiegeln entsprechend. Kreise und
Bézierkurven werden mittransformiert; anisotrope Kreise werden Ellipsen.
`SC` ohne Parameter schaltet auf unskalierte Plottereinheiten zurück. Die
gespeicherte Stiftposition bleibt bei IP/SC unverändert; es entsteht keine Linie
durch den bloßen Wechsel. IN verwirft die Skalierung und Bezugspunkte wieder.
Ungültige Bereiche, Nullfaktoren, unvollständige Ränder, unbekannte Seitengrößen
oder numerisch unzuverlässige Rückrechnungen werden vollständig abgewiesen.

Bögen und Kreise werden entsprechend dem HPGL-Sehnenwinkel als Linienzüge
übernommen: Standard 5°, explizit 0,5° bis 180°. Bézierkurven bleiben Kurven.
Ein großer Sehnenwinkel kann eine sichtbar eckige Kontur ergeben.

## Grenzen und Fehler

Unbekannte Befehle brechen **den gesamten Import ohne Projektänderung** ab.
Insbesondere sind seitenrelative Bezugspunkte (`IR`), Drehung (`RO`),
Clipping, Polygon-/Füllmodi, Text, Linienattribute, Rechteck-/Keilbefehle und
komprimierte `PE`-Daten noch nicht implementiert. PCL-Druckerhüllen mit Escape-
Befehlen werden ebenfalls abgewiesen. Negative Kreisradien werden abgewiesen.
Eine Endung `.plt` allein garantiert daher keine Kompatibilität.

Grenzen: 16 MiB Eingabe, 2000 Pfade beziehungsweise die verbleibende
Projektkapazität, 100000 Pfadelemente und Befehle, insgesamt 200000 Zahlen und
Bogen-Unterteilungsschritte. Die Arbeit für unsichtbare Stift-oben-Bögen zählt mit.
Plotter-Bezugspunkte sowie transformierte Koordinaten und Kontrollpunkte müssen innerhalb ±2000 mm liegen, die gemeinsame
Ausdehnung darf 2000 mm pro Achse nicht überschreiten. Der Arbeitsraum des
Geräteprofils bleibt eine zusätzliche Bedingung für einen späteren Auftrag.

Fehlerhafte Zahlen, unvollständige Koordinatenpaare und Überschreitungen erzeugen
keinen Teilimport und keinen Undo-Eintrag. Erfolgreiches Einfügen ist ein einzelner
Schritt **Geometrie importieren**; Projektdatei, Undo/Redo und GCode-Erzeugung
verwenden anschließend die normalen Blazer-Pfade.

## Nachweise und noch offene Formate

Die Expansion-Prüfungen decken ein asymmetrisches Maß-/Orientierungsbeispiel,
getrennte horizontale Linien, relative Modi, Stiftwechsel, Kreiszustand,
Bogenrichtungen, Bézierkontrollpunkte, atomare Fehler und Ressourcenlimits ab.
Ein tatsächlich durch Inkscape 1.4.3 exportierter eigener 20×10-mm-Umriss wird
eingelesen; Projektwiederherstellung erhält Pfade und erzeugten GCode identisch.
Referenz und Erzeugungsbefehl liegen unter `tests/fixtures/hpgl/`. Drei weitere
eigene Eingaben wurden durch den unabhängigen Inkscape-HPGL2-Importer gelesen;
dessen gebackene SVG-Koordinaten belegen isotrope Zentrierung und die Randlage
bei gespiegeltem P1/P2. Der äußere Rahmen fixiert den gemeinsamen Ursprung,
damit eine nachträgliche Normalisierung fehlerhafte Abstände nicht verdeckt.
Skalierte Joblänge, Projekt-/Undo-Rundlauf und unveränderte Stiftposition über
2000 Folgen von Bezugspunkt-/Skalierungswechseln sind zusätzlich geprüft.

Native RDWorks-/SignCut-Formate sind damit noch nicht geliefert. Der begrenzte
MillMage-Import ist unten beschrieben.
Weitere HPGL-Befehle und ein breiterer Korpus realer Ausgabedateien bleiben
Teil von F04. Generischer SVG-Austausch ersetzt diese Abnahme nicht.

Die eigenständige Implementierung orientiert sich an der
[HP PCL-5/HP-GL/2-Herstellerreferenz](https://www.hp.com/ctg/Manual/bpl13211.pdf),
Kapitel 17, 19, 20 und 22. Der Vergleichsumfang folgt weiterhin dem
[LightBurn-Dateikatalog](https://docs.lightburnsoftware.com/2.2/Reference/FileManagement/).

## GCode als bearbeitbare Geometrie

`.gc`, `.gcode` und `.nc` werden als **nominale XY-Pfade** gelesen und als ein
Pfadobjekt mit getrennten Teilpfaden auf dem aktiven Blatt eingefügt. Auch
tausende kurze Rasterwege verbrauchen dadurch nur einen Projektobjektplatz.
Zwischen den Teilpfaden entstehen keine Verbindungslinien. Das Objekt lässt
sich wie andere Pfade bearbeiten; der GCode für einen späteren Auftrag wird
aus dem aktuellen Projekt neu berechnet. Die Quelldatei wird niemals ausgeführt
oder zum Gerät übertragen.

Die ursprünglichen Leistungen, Vorschübe, Pausen, Verweilzeiten, Werkzeugnummern
und Luft-/Spindelschaltbefehle werden **nicht als Bearbeitung übernommen**.
Es entsteht auch kein Graustufenbild aus unterschiedlichen S-Werten. Im Projekt
die gewünschte Bearbeitung wählen und die berechnete Vorschau prüfen.

| Quelle | Eingelesene Geometrie |
|---|---|
| Mit aktiven `M3`/`M4`/`M5`- oder `S`-Wörtern | Nur Vorschubwege bei M3/M4 und S größer 0; Leerfahrten, ausgeschaltete und leistungslose Zwischenwege bleiben leer |
| Ohne solche Wörter | Alle G1/G2/G3-Vorschubwege; G0 bleibt leer |

Kommentare und Text nach dem Programmende beeinflussen diese Unterscheidung
nicht. Anfangszustand: Millimeter, absolute Positionen, relative Bogenzentren,
G0, Spindel aus und S0. Positive Leistung wird ausschließlich zum Erkennen
der gezeichneten Wege verwendet; eine S-Max-Skalierung muss dafür nicht geraten
werden.

Unterstützt werden G0/G1, G2/G3 in der XY-Ebene G17, G20/G21, G90/G91 und
G90.1/G91.1. G40/G49, G54, G80 und G94 werden eingeordnet; aktive Kompensation
und andere Koordinatensysteme sind nicht unterstützt. G54 bezeichnet hier das
einzige Arbeitskoordinatensystem; es werden keine gespeicherten Geräteoffsets
abgerufen. Werkzeugvorwahl T0–T255 wird gelesen, ein tatsächlicher Werkzeugwechsel
M6 dagegen abgewiesen.

Absolute Bewegungen benötigen vor dem ersten gezeichneten Segment einen
bekannten XY-Anfang, beispielsweise `G0 X10 Y20`. Fehlende Achsen bleiben danach
erhalten. Eine vollständig relative Zeichnung wie ein Export für „aktuelle
Position“ wird um einen frei gewählten Ursprung aufgebaut. Die gesamte Geometrie
wird anschließend wie HPGL gemeinsam links unten platziert. Ihre ursprüngliche
Maschinenlage wird nicht erhalten. Ein späterer absoluter Weg nach einer solchen
relativen Zeichnung wird abgewiesen, weil dessen Lage ohne Gerätewissen nicht
eindeutig ist. Noch nicht gezeichnete relative Leerwege dürfen durch eine
vollständige absolute Leerfahrt neu verankert werden.

Kreiszentren können mit I/J oder Radien mit R angegeben werden. Negative Radien
wählen den großen Bogen; Vollkreise benötigen I/J. P1–P1000 ist als ganze
Umlaufzahl zulässig. Absolute Zentren erfordern I und J. Die Start-/Endradien
dürfen höchstens 0,005 mm abweichen; gröber gerundete Dateien werden abgewiesen.
Bögen werden in Linienzüge mit höchstens 0,01 mm Sehnenabweichung vom Nominalkreis
unterteilt; der programmierte Endpunkt wird exakt verwendet.
Die [LinuxCNC-Befehlsreferenz](https://www.linuxcnc.org/docs/html/gcode/g-code.html)
beschreibt die zugrunde liegenden Einheiten-, Abstands- und Bogenmodi.

Wörter eines Blocks werden nach ihrer Bedeutung ausgewertet: Ein inline
gesetzter S-Wert und M3/M4/M5 gelten für die Bewegung desselben Blocks unabhängig
von der geschriebenen Reihenfolge. Semikolon- und Klammerkommentare,
Groß-/Kleinschreibung, Dezimalzahlen ohne Exponenten, optionale N-Zeilennummern,
UTF-8-BOM und LF/CRLF/CR sind unterstützt. Leerraum außerhalb von Kommentaren
wird ignoriert. M2/M30 beenden nach dem aktuellen Block das Programm; ein
Prozentrahmen benötigt ein öffnendes und schließendes `%`. Ohne solche Marker
ist auch eine am Dateiende endende GRBL-Datei zulässig. G4 mit P ohne
Bewegungswörter, M0/M1 und M7/M8/M9 erzeugen keine Geometrie.
Zur Syntax und Blockreihenfolge siehe die
[LinuxCNC-Sprachübersicht](https://www.linuxcnc.org/docs/html/gcode/overview.html).

Variablen, Ausdrücke, Unterprogramme, Prüfsummen, optionale Blocklöschung,
zusätzliche Achsen, G53, G55 ff., G10/G92, Homing/Referenzpositionen,
Zyklen, andere Ebenen und unbekannte Befehle werden vollständig abgewiesen.
Auch doppelte Wörter oder kollidierende Modalbefehle liefern einen Fehler mit
Zeilennummer, ohne das Projekt oder den Verlauf zu verändern.

Grenzen: 16 MiB und begrenzte Dateilesung, 100000 Zeilen mit je höchstens
4096 Byte, 500000 Wörter, 100000 Pfadelemente und zusätzlich 100000
Bogen-Unterteilungsschritte. Die Unterteilungsgrenze gilt auch für ausgeschaltete
Bögen. Die Koordinaten-/Ausdehnungsgrenzen bleiben ±2000 mm beziehungsweise
2000 mm pro Achse. Die normale Objektkapazität muss noch einen Platz frei haben.

Die Prüfungen vergleichen unabhängige Maße, Blockreihenfolge, Inline-S0-Lücken,
Einheiten, relative Ursprünge, Bogenrichtung und Umläufe. Tatsächlich erzeugter
Blazer-GCode wird eingelesen; Projektwiederherstellung erhält den danach neu
erzeugten GCode identisch. Der Korpus umfasst zusätzlich einen vorhandenen
Rayforge-Prüfexport mit 39030 Zeilen und 7601 getrennten Zeichenwegen.
Das ist keine Abnahme sämtlicher CNC-/Laserdialekte oder der übrigen
noch offenen F04-Formate.


## MillMage und gemeinsamer LightBurn-Leser

Ab beta.4-dev liest **Motiv laden** `.mage` mit `MillMageProject`, FormatVersion 1.
Der gemeinsame begrenzte Leser verarbeitet auch `.lbrn`/`.lbrn2` mit
`LightBurnProject`. Das ist ein Grafikimport auf das aktive Blatt, keine
Projektmigration: bestehender Projektname, Notizen, Dateipfad, Material und
Bearbeitungsparameter bleiben erhalten. Basic verwendet das ausgewählte eigene
Materialprofil. Erfolgreiches Einfügen ist ein Undo-Schritt.

Unterstützt sind Rechtecke mit optionalem Eckenradius, Ellipsen, Linien-/kubische
Bézierpfade, Gruppen und gespeicherte Textkonturen. Lokale affine Transformationen
werden über die Gruppen hinweg kombiniert; Grafik-Layer liefern die Sichtbarkeit,
keine Fräs-/Laseroperationen. Getrennte Pfadkonturen bleiben getrennt. Vollständige
Kontrollpunktpaare sind absolute Koordinaten; die native Kurzform `c0x1`/`c1x1`
ohne Y bezeichnet einen deaktivierten Griff. Andere unvollständige Griffe werden
abgewiesen. MillMage-Text ohne gespeicherte Konturen wird abgewiesen. Der bisherige
LightBurn-Ersatz über installierte Systemschriften bleibt erhalten und kann vom
Original abweichen.

**Eingebettete Bilder:** PNG-Daten werden mit physischer Breite/Höhe und ihrer
Transformation übernommen. Die native Bildzeilenrichtung wird in die gewöhnliche
Bildausrichtung umgerechnet. Drehung, Spiegelung und Scherung werden in ein neues
Raster mit transparenten Außenflächen gebacken. Wie andere Blazer-Bilder werden
sie in Graustufen verarbeitet; das ist keine farbtreue Archivierung. Nichtneutrale
Gamma-/Kontrast-/Helligkeits-/Schärfekorrekturen, Beschnittoptionen und unbekannte
Bildattribute brechen den Import ab. Externe Bildpfade werden niemals gelesen;
ohne gültiges eingebettetes PNG schlägt der gesamte Import fehl. Bilder benötigen
eine vorhandene Gravuroperation im Zielprojekt.

**Ursprung und Einstellungen:** Rohe Projektkoordinaten bleiben erhalten; große
CNC-Zeichnungen können außerhalb des Laserbetts liegen und müssen bewusst angepasst
werden. MillMage-Projekte mit gesetztem `MirrorX`/`MirrorY` werden bis zur weiteren
Formatabnahme abgewiesen. Für ältere LightBurn-Importe bleibt das bisherige
Verhalten erhalten: Ursprung und diese Spiegelungsflags werden nicht übernommen,
worauf die Importmeldung ausdrücklich hinweist. Fräsoperationen, Spindeldrehzahl,
Vorschub, Laserleistung, Geräte-/Materialdaten und Quellnotizen werden nicht kopiert.

**Grenzen:** 16 MiB Eingabe, XML-Tiefe 64, 100000 XML-Knoten, 2000 Formen/Gruppen
und 100000 Pfadpunkte beziehungsweise Pfadelemente. Die bestehenden Projektgrenzen
für Koordinaten und kombinierte Objektkapazität gelten zusätzlich. PNG und daraus
transformiertes Raster bleiben jeweils bei höchstens 4096 Pixeln pro Seite und
16 MiPixel; das kombinierte Projekt bleibt bei 32 MiPixel und 40 MiB eingebetteten
Daten. DTD, unbekannte Formtypen, unvollständige Pfade und mehrdeutige Strukturen
werden vollständig abgewiesen. Vor dem Einfügen wird der vorgeschlagene gesamte
Projektzustand validiert; ein Fehler verändert weder Modell noch Undo-Verlauf.

**Prüfung:** Eigene synthetische Testdateien prüfen Gruppen, Scherung, Sichtbarkeit,
Griffkodierungen, Bildausrichtung/-spiegelung/-drehung, Alpha, unveränderte
Zielparameter, Speichern/Wiederherstellen, Undo/Redo und atomare Ressourcenfehler.
Der echte QML-Dateidialog-Handler ist ebenfalls abgedeckt. Zwei vom Autor im
[MillMage-Herstellerforum bereitgestellte Projekte](https://forum.lightburnsoftware.com/t/error-in-routing/189148)
mit AppVersion 0.8.01 lieferten jeweils vier Objekte, darunter ein eingebettetes
1000×500-PNG und einen Pfad mit 408 Elementen. Die Originalausdehnung von etwa
605×698,5 mm liegt außerhalb des M3-Betts; nur eine ausdrücklich verkleinerte
Prüfkopie wurde auf gültige GCode-Erzeugung und identischen Projektrundlauf geprüft.
Die fremden Grafiken bleiben außerhalb des Repositorys. Diese Stichprobe belegt
keine allgemeine Kompatibilität mit allen MillMage-Versionen.

Die [offizielle MillMage-Dateidokumentation](https://docs.millmagesoftware.com/latest/Reference/FileManagement/)
beschreibt Grafikimport getrennt von CNC-Operationen. RDWorks, SignCut, weitere
MillMage-Varianten und der breitere F04-Korpus bleiben offen.
