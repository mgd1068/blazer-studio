# Kopieren und Einfügen

Ab **0.9.0-beta.4-dev** verwenden die Knöpfe **Kopieren** und **Einfügen** unter
der Objektliste die Systemzwischenablage. Diese Funktion gehört noch nicht zum
unveränderten [beta.3-Kandidaten](beta/CANDIDATE_BETA3.md). **Kopie** bleibt das
bisherige direkte Duplizieren innerhalb des Projekts.

| Aktion auf der Arbeitsfläche | Kürzel |
|---|---|
| Auswahl kopieren | Strg+C |
| Auswahl ausschneiden | Strg+X |
| Einfügen | Strg+V |
| Blazer-Objekte an ihrer Originalposition einfügen | Strg+Umschalt+V |

Textfelder behalten ihre eigenen Kopieren-/Ausschneiden-/Einfügen-Funktionen.
Bei geöffneten Projekt-, Datei- und Bearbeitungsdialogen greifen die neuen
Objekt-Kürzel nicht in die Arbeitsfläche ein. Verwendet wird die normale
Zwischenablage, nicht die separate X11-Markierung für die mittlere Maustaste.

## Blazer-Objekte

Das eigene Format `application/x-blazer-objects-v1` enthält ausschließlich die
gewählten Objekte mit eingebetteten SVG-/Bild-/Reliefdaten, Text-/Bildkorrekturen,
Objektparametern und zugeordneten Operationen. Projektname, Projektnotizen,
Materialprofil und Dateipfad werden nicht als Projektidentität übernommen.
Die erforderlichen Basisoperationen und benutzten Zusatzoperationen werden als
Parameterdaten mitgeführt. Es werden keine Quelldateien nachgeladen.

Einfügen erzeugt neue Objekt- und Gruppenkennungen auf dem aktiven Arbeitsblatt.
Vollständig oder mit mehreren Mitgliedern kopierte Gruppen bleiben als neue
Gruppen erhalten; ein einzeln kopiertes Gruppenmitglied wird unabhängig.
Identische Zieloperationen werden wiederverwendet, andere erforderliche
Quelloperationen ergänzt. Damit bleiben **auch Leistung, Vorschub und andere
zugeordnete Bearbeitungen der Kopie erhalten**. Vor einer Ausgabe diese Werte
prüfen; der Vorgang startet keinen Laserauftrag. Das bestehende Materialprofil,
seine eigenen Operationen, Titel, Notizen und Dateipfad bleiben erhalten.

Normales Einfügen versetzt Blazer-Objekte zunächst um 5 mm in X/Y. Weitere
Einfügungen desselben Inhalts auf demselben Blatt werden jeweils zusätzlich um
5 mm versetzt. Ein neues/geöffnetes Projekt setzt diesen Zähler zurück.
**Einfügen an Originalposition** verwendet die ursprünglichen Koordinaten ohne
Versatz. Es gibt kein stillschweigendes Einpassen ins Gerätebett. Einfügen und
Ausschneiden sind jeweils ein rücknehmbarer Schritt; reines Kopieren verändert
keine Projektinhalte. Ausschneiden entfernt Objekte erst, nachdem Qt die
Zwischenablagedaten übernommen hat.

## Austausch mit anderen Programmen

Reine Vektorauswahlen bieten zusätzlich SVG mit absoluten Millimetermaßen,
Y-Umrechnung und gespeicherten Textkonturen an. Ein kleiner Rand erhält die
sichtbare Strichbreite. Für sehr große oder nicht darstellbare Auswahlen wird
dieses Austauschformat ausgelassen; das eigene Blazer-Format bleibt erhalten.
Ein einzelnes Rasterbild bietet zusätzlich seine eingebettete PNG-/Bildquelle an.
Diese Darstellung enthält keine aktuellen, noch nicht eingebrannten
Bildkorrekturen; dafür steht der [verarbeitete Bildexport](RASTER_EXPORTS.md)
zur Verfügung. Ein einzelner Text bietet zusätzlich seinen Klartext an.
Gemischte Auswahlen und Reliefs werden außerhalb Blazers nicht vollständig
repräsentiert.

Beim Einfügen haben eigene Blazer-Daten Vorrang, danach SVG, Bild und Klartext.
Externe SVG-Daten werden als geprüftes SVG-Motiv übernommen. Bilddaten werden
wie normale Bildimporte in Graustufen mit Transparenz eingebettet und zunächst
mit höchstens 100 mm auf der längsten Seite angelegt. Externer Klartext bleibt
editierbarer Text; HTML wird nicht ausgeführt. Externe Inhalte erhalten eine
eigene vorhandene Gravuroperation des Zielprojekts. Datei-/URL-Listen werden
nicht automatisch geöffnet. Defekte bevorzugte Daten werden gemeldet und nicht
durch einen scheinbar erfolgreichen Teilimport verdeckt.

## Grenzen und Abnahme

Eigenes Format höchstens 64 MiB; insgesamt höchstens 2000 Objekte, 64 Operationen,
1 Million gespeicherte Pfadelemente und die bestehenden Bild-/Projektbudgets.
Diese Arbeitsgrenze wird vor der gemeinsamen Serialisierung geprüft, damit viele
Kopien eines gemeinsam gespeicherten großen Pfads keine riesige JSON-Struktur
erzeugen. Die öffentliche SVG-Darstellung bleibt zusätzlich auf 100000 erzeugte
Pfadelemente und 8 MiB begrenzt. SVG-Eingaben verwenden dieselben Größen- und
Inhaltsprüfungen wie der Dateiimport; Klartext höchstens 1000 Zeichen. Fehler
ändern weder Projekt noch Verlauf und führen zu keinem Teilimport.

`clipboard` prüft Gruppen-/Kennungsunabhängigkeit, Operationen, eingebettete
Bilder, Metadatentrennung, versetztes/originales Einfügen, Ausschneiden/Undo,
SVG-Maße auch bei getrennten flachen Pfaden, Fremdformate, beschädigte Daten und
Arbeits-/Kapazitätsgrenzen. Der normale QML-Workflow prüft Knöpfe und Kürzel in
heller und kompakter dunkler Ansicht, einschließlich der eigenen Kürzel in
Textfeldern. Die Unit-Tests verlangen ausdrücklich die Offscreen-Plattform.

Zusätzlich wurde die tatsächliche X11-Übertragung zwischen getrennten Prozessen
auf einem privaten Xvfb-Testbildschirm geprüft: native 20×10-mm-Geometrie,
externes 7×3-mm-SVG und ein eingebettetes 640×480-Pixel-Bild mit 1309118 Byte
Zwischenablagedaten. Sender-/Empfänger-Prüfsummen des Bildpayloads stimmen überein.
Die normale Benutzerzwischenablage blieb unberührt. Das ersetzt keine Abnahme des unterstützten Desktops, von
Wayland, Clipboard-Managern, allen Fremdprogrammen oder dem Verhalten nach
Beenden der Quellanwendung. Diese Fälle bleiben in T08 offen. Zollbedienung und
Hilfslinien der Familie F07 bleiben zusätzliche Aufgaben.

Auswahlen mit [Hilfsformen T1/T2](TOOL_SHAPES.md) bewahren ihre fehlende Laserausgabe im Blazer-Objektformat. Für diese Auswahl wird kein allgemeiner SVG-, Bild- oder Text-Ersatz ohne diese Zuordnung angeboten.
