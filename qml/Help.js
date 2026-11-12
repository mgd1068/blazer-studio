.pragma library
var actions = {
  "Profil duplizieren": "Erstellt eine eigene Kopie des gewählten Materialprofils.",
  "Profil aus Bibliothek löschen": "Entfernt das Profil nach Rückfrage aus der Bibliothek. Projektwerte bleiben erhalten.",
  "Rechteck": "Fügt ein Rechteck ein. Größe und Lage rechts in Millimetern einstellen.",
  "Ellipse": "Fügt eine Ellipse ein. Gleiche Breite und Höhe ergeben einen Kreis.",
  "Text": "Fügt bearbeitbaren Text ein.",
  "+ Form": "Öffnet die Auswahl für Rechteck, Ellipse und Text.",
  "Trennen": "Beendet die Geräteverbindung und fordert Laser-Aus an.",
  "Verbinden": "Verbindet den Simulator. Für den echten Laser GRBL verbinden wählen.",
  "Neu": "Beginnt ein leeres Projekt. Bei Änderungen fragt das Programm zuerst nach.",
  "Öffnen": "Lädt ein gespeichertes Blazer-Projekt mit seinen Bildern und Einstellungen.",
  "Speichern": "Speichert das Projekt zur späteren Weiterbearbeitung. Die vorige Datei wird als Sicherung erhalten.",
  "Beispiel": "Öffnet ein Übungsprojekt. Seine Werte sind keine geprüfte Materialempfehlung.",
  "Bild": "Lädt ein Foto oder eine Zeichnung. Das allein startet den Laser nicht.",
  "Bearbeitetes Bild exportieren": "Speichert genau ein ausgewähltes Rasterbild einschließlich übernommener Korrekturen, Zuschnitt, Masken und Dithering. PNG erhält Transparenz; JPEG ist verlustbehaftet.",
  "Bild ohne Korrekturen exportieren": "Exportiert die eingebettete Graustufenquelle ohne aktuelle Bildkorrekturen. Bereits eingebrannte Drehungen oder Masken bleiben erhalten.",
  "GCode": "Speichert die berechneten Fahr- und Laserbefehle. Das ist eine Ausgabedatei, keine Projektdatei.",
  "Gravieren": "Bearbeitet die Oberfläche oder füllt das Motiv mit Linien.",
  "Schneiden": "Fährt die Umrisse ab. Ob das Material durchtrennt wird, muss mit einer Materialprobe geprüft werden.",
  "Materialtest": "Bereitet kleine Testflächen vor. Damit lassen sich passende Einstellungen am eigenen Material ermitteln.",
  "Herstellerwerte ansehen": "Zeigt die ursprünglichen Herstellerangaben einschließlich Toolkopf und Quellenhinweis.",
  "Bearbeiten": "Öffnet das gewählte Materialprofil zur Bearbeitung. Herstellerangaben und eigene Änderungen bleiben unterscheidbar.",
  "Gruppieren": "Fasst die markierten Objekte zusammen. Sie lassen sich danach gemeinsam verschieben und skalieren.",
  "Auflösen": "Löst die Gruppe auf; ihre einzelnen Objekte bleiben erhalten.",
  "Kopie": "Erzeugt eine unabhängige Kopie der Auswahl.",
  "Einpassen": "Zeigt die ganze Arbeitsfläche. Die tatsächliche Motivgröße bleibt gleich.",
  "Drehen": "Dreht die Auswahl um den eingestellten Winkel. Vektoren werden dabei in bearbeitbare Pfade umgewandelt.",
  "Spiegeln X": "Spiegelt links und rechts. Mit Rückgängig lässt sich die Änderung aufheben.",
  "Y": "Spiegelt oben und unten.",
  "Ausrichten": "Ordnet die markierten Objekte an der gewählten Kante oder Mitte an.",
  "Konturversatz": "Vergrößert oder verkleinert die Umrisse um den eingestellten Abstand.",
  "Knoten bearbeiten": "Wandelt die Auswahl in Pfade um. Die sichtbaren Kontrollpunkte lassen sich ziehen oder numerisch ändern.",
  "Linie": "Fügt eine Linie ein. Endpunkte sind über die Knotenbearbeitung veränderbar.",
  "Bézier": "Fügt eine geschwungene Linie mit veränderbaren Kontrollpunkten ein.",
  "Schrift und Stil": "Wählt eine auf diesem Computer installierte Schrift aus.",
  "Text auf Pfad": "Ordnet einen Text entlang eines zweiten ausgewählten Pfads an. Das Ergebnis besteht aus Konturen.",
  "Anwenden": "Führt das ausgewählte Pfadwerkzeug aus. Zuerst die betroffenen Objekte markieren.",
  "Bildwerte übernehmen": "Übernimmt die Bildkorrekturen in das Projekt. Es wird noch nichts graviert.",
  "Zuschnitt setzen": "Verwendet den angegebenen rechteckigen Bildausschnitt. Angaben in Bildpixeln, nicht Millimetern.",
  "Bild drehen": "Dreht das bearbeitete Bild um 90 Grad.",
  "Spiegeln": "Spiegelt das bearbeitete Bild seitlich.",
  "Maskenform": "Verwendet die zusätzlich ausgewählte Vektorform als Ausschnitt des Bildes.",
  "Vektorisieren": "Erzeugt bearbeitbare Umrisse aus dunklen Bildflächen.",
  "Als Reliefkarte": "Verwendet die Helligkeit des Bildes als gewünschte relative Gravurtiefe.",
  "Tiefenkarte laden": "Lädt ein Bild, dessen Helligkeit die gewünschte Tiefe beschreibt. Schwarz bedeutet normalerweise tief.",
  "Foto → KI-Tiefenkarte": "Schätzt die räumliche Tiefe lokal aus einem Foto. Das Ergebnis muss geprüft werden und ist keine Vermessung.",
  "3D-Vorschau": "Zeigt die gewünschte Reliefstruktur mit überhöhter Tiefe. Die Darstellung sagt keine tatsächliche Gravurtiefe voraus.",
  "Alle": "Zeigt das gesamte Relief statt einer einzelnen Schicht.",
  "0° / 90°": "Wechselt die Bearbeitungsrichtung zwischen waagerecht und senkrecht.",
  "Weiß tief, Schwarz unberührt": "Kehrt die Tiefenbedeutung der hellen und dunklen Bereiche um.",
  "+ Operation": "Legt einen zusätzlichen Bearbeitungsschritt an. Er wirkt erst nach seiner Zuweisung an Objekte.",
  "Operation löschen": "Entfernt die zusätzliche Operation. Betroffene Objekte erhalten eine Basisoperation. Rückgängig ist möglich.",
  "Umbenennen": "Ändert den Namen des ausgewählten Eintrags.",
  "+ Blatt": "Legt eine weitere Arbeitsfläche im Projekt an.",
  "Blatt anzeigen": "Wechselt zum gewählten Arbeitsblatt. Ausgegeben wird ausschließlich das aktive Blatt.",
  "Blatt mit Inhalt löschen": "Entfernt das Blatt und seine Objekte. Rückgängig stellt den vorherigen Stand wieder her.",
  "Auswahl hier kopieren": "Kopiert die markierten Objekte auf das gewählte Zielblatt.",
  "Kopien als Raster": "Erzeugt die eingestellte Anzahl Kopien in gleichmäßigen Reihen und Spalten.",
  "Auswahl verschachteln": "Ordnet ausgewählte Teile platzsparend an. Die Berechnung sucht eine praktikable Anordnung.",
  "QR-Code erzeugen": "Erstellt einen QR-Code aus dem eingegebenen Inhalt. Rundherum wird eine freie Ruhezone benötigt.",
  "CSV-Stapel exportieren": "Ersetzt Text-Platzhalter durch Tabellenwerte und erzeugt getrennte Auftragsdateien. Startet keine Bearbeitung.",
  "Maßprobe": "Erzeugt ein 10-mm-Muster zum Nachmessen. Das Programm verändert keine Firmwareeinstellungen.",
  "Rasterprobe": "Erzeugt Vergleichsflächen mit verschiedenen Linienabständen.",
  "Fokusmarken": "Erzeugt Marken für getrennte manuelle Fokusproben. Es gibt keine automatische Höhenverstellung.",
  "Foto laden": "Lädt ein Referenzfoto für die Ausrichtung auf der Zeichenfläche.",
  "Kalibrieren": "Ordnet die vier angeklickten Foto-Ecken dem 200 × 150-mm-Arbeitsraum zu.",
  "Foto einblenden": "Blendet das registrierte Referenzfoto als Positionierhilfe ein oder aus.",
  "Letzte Sicherung wiederherstellen": "Stellt die letzte automatische Sicherung wieder her. Anschließend separat speichern.",
  "GRBL verbinden": "Öffnet die Verbindung zum ausgewählten Laser und prüft dessen Konfiguration. Keine automatische Bewegung.",
  "Laser verbinden": "Öffnet die Verbindung zum ausgewählten Laser und prüft dessen Konfiguration. Keine automatische Bewegung.",
  "Simulator": "Wechselt zur Bildschirmprobe ohne Hardwarebewegung.",
  "Suchen": "Aktualisiert die Liste der seriellen Anschlüsse.",
  "Arbeitsnullpunkt hier setzen": "Legt die aktuelle Kopfposition als Startpunkt der Werkstückkoordinaten fest.",
  "Live übernehmen": "Ändert Vorschub und Leistung im laufenden Auftrag. Nur mit bestätigter Controller-Rückmeldung verfügbar.",
  "Verbindungsfehler testen": "Simuliert einen Verbindungsabbruch zum Prüfen der Software.",
  "Abbrechen": "Beendet diesen Vorgang ohne automatische Laserfreigabe.",
  "Datei importieren": "Lädt eine unterstützte Grafik, ein Modell oder Geometrien aus einem Projekt.",
  "SVG exportieren": "Speichert die Vektoren des aktiven Blatts für andere Zeichenprogramme.",
  "↓  Importieren": "Lädt eine unterstützte Grafik, GCode-Geometrie oder ein 3D-Modell. GCode wird als editierbarer Pfad eingefügt und nicht ausgeführt. RDWorks-Jobs werden ebenfalls nur als Geometrie gelesen. MillMage übernimmt unterstützte Grafikobjekte ohne Fräsoperationen. Größere Bilder behalten ihre Auflösung.",
  "⌂  Referenzfahrt": "Fährt zu den Endschaltern, damit der Laser seine Position wieder kennt. Der Kopf bewegt sich.",
  "□  Rahmenfahrt": "Zeigt die äußeren Motivgrenzen durch eine Fahrt mit ausgeschaltetem Laser.",
  "◎  Halten für 2 %": "Fordert einen kurzen Laserpunkt an. Auch dieser Punkt ist echte Laserstrahlung.",
  "■  Stop": "Bricht die Bearbeitung ab. Ein Softwarebefehl ersetzt keinen Not-Aus am Gerät.",
  "↶": "Macht die letzte Projektänderung rückgängig.",
  "↷": "Stellt eine rückgängig gemachte Änderung wieder her.",
  "⋈": "Schaltet die Bearbeitung einzelner Pfad-Kontrollpunkte ein oder aus.",
  "T": "Fügt einen Text ein. Name, Inhalt, Position und Größe lassen sich bearbeiten.",
  "□": "Fügt ein Rechteck ein.",
  "○": "Fügt eine Ellipse oder einen Kreis ein.",
  "↖": "Wählt Objekte auf der Zeichenfläche aus; Ziehen verschiebt die Auswahl.",
  "⋯": "Öffnet weitere Funktionen der Materialbibliothek.",
  "+": "Fügt im jeweiligen Bereich einen Eintrag hinzu oder vergrößert die Ansicht.",
  "−": "Entfernt die Auswahl oder verkleinert die Ansicht; die Beschriftung des Bereichs beachten.",
  "↑": "Bewegt den Kopf oder den Eintrag nach oben. Bei Kopfbewegung gilt die gewählte Schrittweite.",
  "↓": "Bewegt den Kopf oder den Eintrag nach unten. Bei Kopfbewegung gilt die gewählte Schrittweite.",
  "←": "Bewegt den Laserkopf um die gewählte Schrittweite nach links.",
  "→": "Bewegt den Laserkopf um die gewählte Schrittweite nach rechts."
}
function context(control) {
 if(!control || !control.parent)return ""
 var siblings=control.parent.children
 var result=""
 for(var i=0;i<siblings.length;i++) {
  if(siblings[i]===control)break
  if(typeof siblings[i].text!=="undefined" && String(siblings[i].text).length>0)result=String(siblings[i].text)
 }
 return result
}
function numeric(label,unit,control) {
 var rangeHints={"v↓":"Langsamste Geschwindigkeit der Materialtest-Reihen.","v↑":"Schnellste Geschwindigkeit der Materialtest-Reihen.","P↓":"Niedrigste Leistung der Materialtest-Spalten.","P↑":"Höchste Leistung der Materialtest-Spalten."}
 if(rangeHints[label])return rangeHints[label]+" Einheit: "+unit+". Dezimalkomma ist erlaubt."
 var meanings={
  "Links": "Abstand des linken Motivrandes vom linken Rand der Arbeitsfläche.",
  "Unten": "Abstand des unteren Motivrandes vom unteren Rand der Arbeitsfläche.",
  "Breite": "Breite der Auswahl in der angezeigten Einheit.",
  "Höhe": "Höhe der Auswahl in der angezeigten Einheit.",
  "v": "Geschwindigkeit: Wie schnell sich der Kopf bewegt. Langsamer kann mehr Material abtragen.",
  "P": "Leistung: Angeforderter Anteil der Laserleistung. Ein höherer Wert kann mehr Material abtragen.",
  "×": "Anzahl der Wiederholungen. Mehr Durchgänge können die Bearbeitung vertiefen.",
  "↕": "Abstand zwischen Gravurlinien. Kleiner bedeutet mehr Linien, längere Laufzeit und mehr Energie je Fläche.",
  "X": "Waagerechte Position; der Ursprung liegt unten links.",
  "Y": "Senkrechte Position; größere Werte liegen weiter oben.",
  "B": "Breite des Motivs oder des Bildausschnitts.",
  "H": "Höhe des Motivs oder des Bildausschnitts.",
  "S": "Skalierung relativ zur aktuellen Größe. 100 Prozent lässt die Größe unverändert.",
  "↻": "Drehwinkel der Auswahl in Grad.",
  "Δ": "Abstand oder Konturversatz in der angegebenen Einheit.",
  "↔": "Abstand der Zeichen oder Spaltenzahl entsprechend der Einheit.",
  "K": "Kontrast: 1 lässt das Bild unverändert.",
  "γ": "Tiefen- oder Helligkeitskurve: verändert vor allem die mittleren Grauwerte."
}
 return (meanings[label] || (context(control) ? context(control)+": verändert diesen Wert." : "Ändert den im danebenstehenden Text bezeichneten Wert.")) + (unit ? " Einheit: "+unit+". Dezimalkomma ist erlaubt." : " Dezimalkomma ist erlaubt.")
}
function describe(control) {
 if(!control)return ""
 var named={
  "editorUnitChoice": "Maßeinheit für Längenfelder, Lineale und Fangraster: mm oder Zoll (in). Projekt und GCode bleiben in Millimetern. Längen können auch mit mm oder in eingegeben werden.",
  "toolHeadChoice": "Wähle die Nennleistung des tatsächlich eingebauten Laserkopfs. Danach zeigt die Materialliste nur passende Profile. Der Kopf wird nicht automatisch erkannt.",
  "materialChoice": "Wähle das tatsächliche Material und seine Dicke. Die Werte stammen aus dem Profil; Herstellerwerte müssen am eigenen Gerät geprüft werden.",
  "objectNameInput": "Name des ausgewählten Objekts. Eingabe oder Verlassen des Feldes übernimmt die Änderung.",
  "serialPortChoice": "Wähle den USB-Anschluss des Lasers. Suchen aktualisiert die Liste.",
  "reliefSlice": "Zeigt die bearbeiteten Bereiche einer Tiefenschicht. Null zeigt alle Schichten gemeinsam.",
  "reliefInvert": "Kehrt die Tiefenzuordnung um: hell wird tief statt dunkel."
}
 if(named[control.objectName])return named[control.objectName]
 var nearby=context(control)
 var t=typeof control.text!=="undefined" ? String(control.text) : ""
 if(actions[t])return actions[t]
 if(t.indexOf("Vorbereiten")>=0)return "Schritt 1: Motiv laden, Material auswählen und Größe sowie Position festlegen."
 if(t.indexOf("Vorschau")>=0)return "Schritt 2: Bearbeitungsflächen und Fahrwege kontrollieren, bevor der Laser startet."
 if(t.indexOf("Gerät")>=0)return "Schritt 3: Gerät verbinden und die kontrollierte Bearbeitung auslösen."
 if(t.indexOf("Relief")>=0)return "Erweiterter Arbeitsbereich für Gravur in mehreren Tiefenschichten."
 if(t.indexOf("Werkzeuge")>=0)return "Erweiterte Bearbeitungs- und Produktionsfunktionen."
 if(t.indexOf("starten")>=0)return t.indexOf("Simulation")>=0 || t.indexOf("Bildschirm")>=0 ? "Spielt den Auftrag am Bildschirm ab; der Laser bleibt unbenutzt." : "Startet den vorbereiteten Auftrag am verbundenen Laser. Material und Gerät müssen dafür vorbereitet sein."
 if(t.indexOf("Pause")>=0||t.indexOf("Fortsetzen")>=0)return "Hält den laufenden Auftrag an oder setzt ihn nach einer Pause fort."
 if(t.indexOf("Schritt")>=0||t.indexOf("mm")>=0)return "Wählt die angezeigte Schrittweite für die manuelle Positionierung."
 if(typeof control.model!=="undefined")return (nearby ? nearby+": " : "")+"Wähle einen Eintrag aus der Liste. Die aktuelle Auswahl wird im Feld angezeigt."
 if(typeof control.from!=="undefined")return (nearby ? nearby+": " : "")+"Wert im Bereich "+control.from+" bis "+control.to+"."
 if(typeof control.placeholderText!=="undefined")return control.placeholderText || "Text eingeben. Änderungen mit Eingabe oder durch Verlassen des Feldes übernehmen."
 return t ? "Einstellung: "+t+". Änderungen vor der Ausgabe in der Vorschau prüfen." : (nearby ? nearby+": ein- oder ausschalten." : "Aktiviert oder deaktiviert diese Einstellung.")
}
