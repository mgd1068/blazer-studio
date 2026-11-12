# Vektorimporte: HPGL/PLT

Dieser Import gehört zur Entwicklung **0.9.0-beta.4-dev**. Er ist noch nicht
Bestandteil des unveränderten [beta.3-Pakets](beta/CANDIDATE_BETA3.md).
Die gesamte Familie F04 bleibt in der [Funktionsabnahme](FUNCTION_ACCEPTANCE.md)
offen; der folgende Umfang ist eine begrenzte HPGL-Geometrieübernahme.

Unter **Grafik oder Modell importieren** können `.hpgl` und `.plt` eingelesen
werden. Es entstehen editierbare Pfade auf dem aktiven Arbeitsblatt. Der Import
öffnet keinen Geräteanschluss, führt keine Datei aus und startet keinen Auftrag.
Alle Stifte werden derselben anfänglichen Bearbeitung zugeordnet; Leistung,
Geschwindigkeit und weitere Bearbeitungsparameter sind im Projekt neu zu prüfen.

## Unterstützte Geometrie

| Befehle | Verhalten |
|---|---|
| `IN` | Initialisiert Position 0/0, absolute Koordinaten, Stift oben und keine Stiftauswahl; bereits gelesene Pfade bleiben erhalten |
| `PA`, `PR` | Absolute beziehungsweise relative Koordinaten; der Modus bleibt für nachfolgende `PU`/`PD` erhalten |
| `PU`, `PD` | Stift heben/senken und optionale Koordinatenpaare; Fahrbewegungen werden nicht zu Schnittlinien |
| `SP` | Positive Stiftnummer 1–255 zeichnet; 0 oder fehlende Nummer deaktiviert Zeichnen. Nummern/Farben werden nicht als Operationen übernommen |
| `AA`, `AR` | Absolutes beziehungsweise relativ zum aktuellen Punkt angegebenes Zentrum; positiver Winkel gegen den Uhrzeigersinn im Weltkoordinatensystem |
| `CI` | Kreis mit positivem Radius, auch bei gehobenem Stift; stellt Position, Stiftzustand und Koordinatenmodus danach wieder her |
| `BZ`, `BR` | Kubische Bézierkurven mit absoluten beziehungsweise zum jeweiligen Segmentanfang relativen Kontrollpunkten |

Die Datei wird als eigenständige Zeichnung mit dem oben beschriebenen
Initialzustand gelesen. Üblich ist ein Beginn mit `IN;SP1;`.
Groß-/Kleinschreibung, Komma/Leerraum zwischen Zahlen und fehlende Semikolons
vor dem nächsten Befehlsnamen sind zulässig. Unterstützt werden Dezimalzahlen
ohne Exponenten.

**Maßstab und Lage:** 40 unskalierte Plottereinheiten ergeben 1 mm. Positive Y
zeigt wie im Blazer-Weltkoordinatensystem nach oben. Alle Pfade werden gemeinsam
so verschoben, dass die untere linke Grenze bei 0/0 liegt; Abstände und Orientierung
bleiben erhalten. Die ursprüngliche Lage zum Plotterursprung wird nicht erhalten.
Danach das gesamte Motiv an der gewünschten Stelle platzieren.

Bögen und Kreise werden entsprechend dem HPGL-Sehnenwinkel als Linienzüge
übernommen: Standard 5°, explizit 0,5° bis 180°. Bézierkurven bleiben Kurven.
Ein großer Sehnenwinkel kann eine sichtbar eckige Kontur ergeben.

## Grenzen und Fehler

Unbekannte Befehle brechen **den gesamten Import ohne Projektänderung** ab.
Insbesondere sind Skalierung/Bezugspunkte (`SC`, `IP`, `IR`), Drehung (`RO`),
Clipping, Polygon-/Füllmodi, Text, Linienattribute, Rechteck-/Keilbefehle und
komprimierte `PE`-Daten noch nicht implementiert. PCL-Druckerhüllen mit Escape-
Befehlen werden ebenfalls abgewiesen. Negative Kreisradien werden abgewiesen.
Eine Endung `.plt` allein garantiert daher keine Kompatibilität.

Grenzen: 16 MiB Eingabe, 2000 Pfade beziehungsweise die verbleibende
Projektkapazität, 100000 Pfadelemente und Befehle, insgesamt 200000 Zahlen und
Bogen-Unterteilungsschritte. Die Arbeit für unsichtbare Stift-oben-Bögen zählt mit.
Rohkoordinaten und Kontrollpunkte müssen innerhalb ±2000 mm liegen, die gemeinsame
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
Referenz und Erzeugungsbefehl liegen unter `tests/fixtures/hpgl/`.

GCode-Geometrieimport und native RDWorks-/SignCut-/MillMage-Formate sind damit
noch nicht geliefert. Weitere HPGL-Befehle und ein breiterer Korpus realer
Ausgabedateien bleiben Teil von F04. Generischer SVG-Austausch ersetzt diese
Abnahme nicht.

Die eigenständige Implementierung orientiert sich an der
[HP PCL-5/HP-GL/2-Herstellerreferenz](https://www.hp.com/ctg/Manual/bpl13211.pdf),
Kapitel 17, 19, 20 und 22. Der Vergleichsumfang folgt weiterhin dem
[LightBurn-Dateikatalog](https://docs.lightburnsoftware.com/2.2/Reference/FileManagement/).
