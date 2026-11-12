# Rasterbilder importieren und exportieren

Der hier beschriebene Bildexport gehört zu **0.9.0-beta.4-dev**, noch nicht zum
unveränderten [beta.3-Testkandidaten](beta/CANDIDATE_BETA3.md).

## Import

PNG, JPEG (`.jpg`/`.jpeg`), BMP, GIF, TIFF (`.tif`/`.tiff`) und WebP werden durch
die Qt-Bildleser eingelesen. Die Anwendung verwendet die erste gelesene Bildseite;
Animationen und mehrseitige Dokumente werden nicht als mehrere Motive importiert.
Die neuen Debian-Pakete und die CI-Umgebung benötigen ausdrücklich
`qt6-image-formats-plugins`, damit TIFF und WebP auch auf einer frischen
Installation verfügbar sind. Eine vorhandene Desktopinstallation allein belegt
diese Paketabhängigkeit nicht.

Bilder werden intern in Graustufen mit Transparenz eingebettet. Die Farbinformation
des Originals gehört deshalb nicht zum Projekt. Größen- und Speichergrenzen
bleiben bei 4096 Pixeln pro Seite, 16 MiPixel pro Bild und den bestehenden
Projektbudgets. Diese Grenzen sind von der tatsächlichen Auflösung der späteren
Laser-Rasterung getrennt.

## Export aus der Bildbearbeitung

In **Advanced → 05 Werkzeuge → Bild** genau ein Rasterbild auswählen. Die Felder
zuerst mit **Bildwerte übernehmen** beziehungsweise **Zuschnitt setzen** anwenden.
Noch nicht übernommene Eingaben sind ein Entwurf und werden nicht exportiert.

- **Bearbeitetes Bild exportieren** verwendet denselben Bildverarbeitungsschritt
  wie die Jobvorschau: Zuschnitt, Helligkeit, Kontrast, Gamma, Schärfung,
  Invertierung, elliptische Maske und ausgewählten Dithering-/Graustufenmodus.
- **Bild ohne Korrekturen exportieren** speichert die derzeit eingebettete
  Graustufenquelle ohne die aktuellen Korrektureinstellungen. Bereits eingebrannte
  Drehungen, Spiegelungen und Formmasken bleiben Teil dieser Quelle. Es ist keine
  Wiederherstellung der ursprünglichen farbigen Eingabedatei.

PNG erhält Transparenz und beim Export ohne Korrekturen vorhandene 16-Bit-Werte.
Der verarbeitete Bildstand verwendet wie die Jobverarbeitung 8 Bit Graustufen.
JPEG ist mit Qualitätsstufe 95 verlustbehaftet; JPEG und BMP legen transparente
Bereiche auf einen weißen Hintergrund. Die Dateiendung bestimmt das Format.
Exportiert werden die Quellpixel beziehungsweise die nach Zuschnitt verbleibenden
Pixel, ohne zusätzliche Anpassung an das Scanintervall. PNG enthält die aus
Pixelzahl und aktueller Objektgröße berechnete Auflösungsangabe; das Dateiformat speichert
nur ganze Punkte pro Meter. Besonders bei sehr niedriger Pixeldichte kann die
daraus abgeleitete physische Größe daher abweichen. Lage auf dem Arbeitsblatt,
Operationen und Laserparameter werden nicht als Bildinhalt gespeichert.

## Hintergrundarbeit und Dateisicherheit

Beim Bestätigen des Dateidialogs werden Bilddaten, Korrekturen und physische Größe
als eigener Auftrag eingefroren. Spätere Auswahl-/Projektänderungen beeinflussen
diesen Export nicht. Wurde das gewählte Bild oder der Projektstand bereits während
des geöffneten Dialogs geändert, wird die Anfrage abgewiesen und muss neu geöffnet
werden. Ein zweiter gleichzeitiger Bildexport wird abgewiesen.

Berechnung und Kodierung laufen im Hintergrund; der Knopf **Bildexport abbrechen**
fordert Abbruch an. Vor der Veröffentlichung bleibt die bestehende Zieldatei bei
Abbruch oder Fehler erhalten. Ein bereits vollständig veröffentlichter Export
kann nicht nachträglich abgebrochen werden. Während die Formatbibliothek ein Bild
kodiert, kann der Abbruch erst nach ihrer Rückkehr geprüft werden. Beim Beenden
wartet die Anwendung auf das Ende ihrer abgebrochenen Hintergrundarbeit.

Das Schreiben erfolgt mit Dateisperre und atomarer Veröffentlichung. Dateilinks
werden als Ziel abgewiesen. Eine bestehende normale Datei kann nach der
Überschreibentscheidung im Speicherdialog ersetzt werden. Quelltext-/EXIF-Metadaten
werden nicht aus dem Eingabebild übernommen. Projekt, Dateipfad, Änderungszustand,
Undo-Verlauf und berechneter Job werden vom Export nicht verändert. Es wird keine
Geräteverbindung geöffnet und kein Laserauftrag gestartet.

## Prüfumfang und offene Punkte

`imageexport` prüft feste Pixelwerte für Zuschnitt/Inversion, die gemeinsame
Jobverarbeitung aller vier Modi, Transparenz/weißen Hintergrund, 16-Bit-PNG,
Auflösungsmetadaten, Dateifehler/-sperren, Abbruch, eingefrorene Hintergrunddaten,
veraltete Dialoge und unveränderte Projektzustände. Alle beworbenen Rasterformate
werden mit eigenen kleinen Bilddaten tatsächlich importiert; GIF nutzt eine eigene
2×1-Pixel-Probe. Der Programm-Selbsttest prüft die zusätzlichen Codecs auch in der
Paket-Laufzeitumgebung. `workflow` prüft beide Exportknöpfe und den echten
QML-Dateihandler in heller und kompakter dunkler Ansicht.

Die native Betriebssystem-Dateiauswahl bleibt Teil der Desktopabnahme. Breitere
Eingabekorpora, mehrseitige/animierte Daten und reale Rasterqualität sind damit
nicht vollständig abgenommen. F05/F20 bleiben mit diesen Detailfällen im
[Funktionsplan](FUNCTION_ACCEPTANCE.md) sichtbar.

Die funktionale Referenz trennt ebenfalls normalen Bildexport vom
[Speichern eines verarbeiteten Bildes](https://docs.lightburnsoftware.com/2.1/Reference/SaveProcessedBitmap/).
Die Implementierung verwendet die eigene Blazer-Bildverarbeitung.
