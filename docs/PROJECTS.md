# Projektname und Notizen

Entwicklungsstand nach dem geprüften Kandidaten beta.2, ab `0.9.0-beta.3-dev`.

Oben links **Projektname → Projektangaben** wählen. Der Dialog öffnet einen Entwurf für Name und Notizen. **Übernehmen** ändert das geöffnete Projekt; **Abbrechen** oder Escape verwirft den Entwurf. Danach das Projekt speichern. Undo/Redo kann eine übernommene Änderung zurücknehmen beziehungsweise wiederholen.

Der Name ist einzeilig, hat höchstens 1000 Zeichen und darf nach Entfernen äußerer Leerzeichen nicht leer sein. Die Notizen dürfen bis zu 16000 Zeichen enthalten. Sie werden als Klartext angezeigt; beispielsweise bleibt `<b>Text</b>` wörtlich erhalten.

Notizen eignen sich für Materialbezeichnung, Dicke, Fokus, Versuch und beobachtetes Ergebnis. Sie ändern keine Bearbeitungsparameter. Herstellerwerte bleiben gesonderte Referenzen; eigene Beobachtungen sind keine allgemeine Materialfreigabe. Titel und Notizen gehen nicht in den Laser-GCode oder den technischen Diagnoseexport ein.

Übernommene Angaben gehören zum Projekt und seiner automatischen Wiederherstellung. Beim Öffnen anderer Projekte werden deren Angaben geladen; **Neu** und **Beispiel** entfernen die bisherigen Notizen. Eine Änderung der Angaben erzeugt keine neue Werkzeugwegberechnung. Die gerätespezifischen Startbedingungen bleiben bestehen.

Erfolgreiches Öffnen und Speichern merken den zugehörigen Projektpfad für den nächsten Speicherdialog. Neu, Beispiel, Vorlageninstanzen und Wiederherstellungen starten ohne alten Speicherpfad. Der Vorschlag folgt dem aktuellen Dokument und nicht einer früheren Dialogauswahl; der zuletzt benutzte Ordner kann dabei erhalten bleiben. Die Dateizuordnung gehört nicht zum Projektinhalt oder Undo-Verlauf und wird nicht in der Datei gespeichert.

**Öffnen** lädt das vollständige Projekt einschließlich Name und Notizen. Das Einfügen eines Projekts über den Import übernimmt seine Objekte und Operationen; Name und Notizen des bereits geöffneten Zielprojekts bleiben erhalten.

## Dateiformat

Neu gespeicherte Projekte verwenden **Format 8** mit einem eigenen Klartextfeld `notes`. Formate 1–7 bleiben lesbar und erhalten leere Notizen. Frühere Titel werden beim Lesen nicht stillschweigend normalisiert. Ältere Blazer-Versionen einschließlich beta.2 unterstützen Format 8 nicht und müssen solche Dateien ablehnen. Ein Rückspeichern als Format 7 würde Notizen verlieren und wird daher nicht angeboten.

## Zuletzt verwendete Projekte

**Zuletzt** neben **Öffnen** zeigt bis zu zehn erfolgreich geöffnete oder gespeicherte Projektdateien mit ihrem Pfad. Der jüngste Eintrag steht oben; erneutes Öffnen oder Speichern desselben Projekts erzeugt keine zweite Zeile. Fehlgeschlagene Vorgänge, Motivimporte, automatische Wiederherstellung und reine Exporte nehmen keine Datei in diese Liste auf. Der Verlauf bleibt lokal in den Anwendungseinstellungen und gehört nicht zum technischen Diagnosebericht.

Ein Klick öffnet das ausgewählte Projekt. Bei ungespeicherten Änderungen bietet die Anwendung vorher Speichern, Verwerfen und Abbrechen an. Speichern kann die Verlaufsliste neu ordnen; geöffnet wird weiterhin die ursprünglich gewählte Datei. Bricht der Benutzer ab oder schlägt das Öffnen fehl, bleibt das aktuelle Projekt erhalten.

Fehlende Dateien sind als **nicht gefunden** erkennbar und lassen sich nicht auswählen. Beim erneuten Öffnen der Liste wird die Verfügbarkeit aktualisiert. Umgezogene Dateien über den normalen Dateidialog öffnen; ihr neuer Pfad wird danach aufgenommen. Beschädigte Dateien werden beim Öffnungsversuch abgelehnt und überschreiben das aktuelle Projekt nicht.

**Verlauf leeren** entfernt nur die gespeicherte Liste. Projektdateien und das geöffnete Dokument bleiben erhalten. Ein Fehler beim Speichern der Anwendungseinstellungen darf eine erfolgreich gespeicherte Projektdatei nicht nachträglich als fehlgeschlagen behandeln; der Verlauf meldet seinen eigenen Fehler und zeigt keine erfolgreich geleerte Liste an, wenn die Änderung nicht gesichert werden konnte. Offline-Aufrufe für Screenshot, GCode-Export und Selbsttest verändern den persönlichen Verlauf nicht.

## Projektvorlagen

**Projektname → Als Vorlage speichern …** speichert den aktuellen Entwurf als `.blazertemplate`. Enthalten sind Objekte, eingebettete Bilder/Vektoren, Operationen, Blätter, übernommene Materialwerte, Name und Notizen. Die Ausgangsdateien eingebetteter Motive sind danach zum Laden nicht nötig. Das Speichern einer Vorlage setzt ungespeicherte Änderungen des geöffneten Projekts nicht auf „gespeichert“ und nimmt die Vorlage nicht in den Projektverlauf auf.

**Projektname → Neu aus Vorlage …** erzeugt daraus ein unabhängiges, ungespeichertes Projekt mit frischem Undo-Verlauf. Bestehende Änderungen lassen sich vorher speichern, verwerfen oder durch Abbrechen behalten. Ein abgebrochener Dateidialog oder eine beschädigte Vorlage ersetzt das aktuelle Projekt nicht.

Die Vorlage wird beim Laden, Bearbeiten und normalen Projektspeichern nicht verändert. Das neue Projekt als `.blazer` unter eigenem Namen speichern; normales Projektspeichern darf keine `.blazertemplate` ersetzen. Eine bewusste Aktualisierung erfolgt über **Als Vorlage speichern …**. Bei Überschreiben wird der bisherige Stand als `.bak` gesichert; eine Schreibsperre oder ein Schreibfehler wird gemeldet.

Vorlagen übernehmen keine Maschinenverbindung oder Startfreigabe. Beim neuen Projekt werden Materialbestätigung und der Sonderstatus einer früheren Probe zurückgesetzt. Vor einem echten Start bleiben Material, Lage, Fokus und die gerätespezifischen Bedingungen zu prüfen. Eine Vorlage startet weder Bewegung noch Laser.

Das eigene Vorlagenformat hat die Kennung `blazer-studio-template`, `templateVersion: 1` und enthält das Projektformat 8. Die Dateigrenze bleibt 64 MiB; Projekt-, Bild- und Geometriegrenzen werden beim Laden vollständig geprüft. Es wurde kein Import fremder Vorlagenformate abgenommen.

Das Linux-Paket und der lokale Quellstarter registrieren `.blazertemplate` als eigenen Dateityp. Ein entsprechender Dateistart erzeugt ein neues Projekt. Die Prüfung am tatsächlichen Desktop einschließlich seiner Dateizuordnung bleibt ein eigener T08-Fall.

Für einen Aufruf im Terminal:

```bash
blazer-studio --new-from-template VORLAGE.blazertemplate
blazer-studio --new-from-template VORLAGE.blazertemplate --export-gcode AUSGABE.gcode
```

Die zweite Variante exportiert ausschließlich in eine Datei, öffnet keine Geräteverbindung und verändert den persönlichen Projektverlauf nicht.

## Vorhandene automatische Sicherung beim Start

Eine ältere automatische Sicherung bleibt bis zur Entscheidung über **Wiederherstellen** oder **Sicherung verwerfen** geschützt. Auch ein direkt aus einer Datei gestarteter neuer Vorlagenentwurf darf sie während dieser Entscheidung nicht überschreiben. Beim Beenden ohne Entscheidung bleiben die vorhandenen Sicherungsstände erhalten. Nach Wiederherstellen oder bewusstem Verwerfen sichert Autosave die weiteren ungespeicherten Änderungen wieder automatisch.

Wird das Beenden eines ungespeicherten Entwurfs abgebrochen, erscheint die offene Wiederherstellungsentscheidung erneut. Das gilt auch beim Abbrechen des vorgeschalteten Speicherdialogs. Kann eine Sicherung nicht entfernt werden, meldet die Anwendung den Fehler und hält die Entscheidung offen.

Der vollständige [Funktionsstand](FUNCTION_ACCEPTANCE.md) und die [Abnahmekriterien](PRODUCTION_READINESS.md) bleiben maßgeblich.
