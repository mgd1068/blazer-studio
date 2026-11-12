# Projektname und Notizen

Entwicklungsstand nach dem geprüften Kandidaten beta.2, ab `0.9.0-beta.3-dev`.

Oben links auf den Projektnamen klicken. **Projektangaben** öffnet einen Entwurf für Name und Notizen. **Übernehmen** ändert das geöffnete Projekt; **Abbrechen** oder Escape verwirft den Entwurf. Danach das Projekt speichern. Undo/Redo kann eine übernommene Änderung zurücknehmen beziehungsweise wiederholen.

Der Name ist einzeilig, hat höchstens 1000 Zeichen und darf nach Entfernen äußerer Leerzeichen nicht leer sein. Die Notizen dürfen bis zu 16000 Zeichen enthalten. Sie werden als Klartext angezeigt; beispielsweise bleibt `<b>Text</b>` wörtlich erhalten.

Notizen eignen sich für Materialbezeichnung, Dicke, Fokus, Versuch und beobachtetes Ergebnis. Sie ändern keine Bearbeitungsparameter. Herstellerwerte bleiben gesonderte Referenzen; eigene Beobachtungen sind keine allgemeine Materialfreigabe. Titel und Notizen gehen nicht in den Laser-GCode oder den technischen Diagnoseexport ein.

Übernommene Angaben gehören zum Projekt und seiner automatischen Wiederherstellung. Beim Öffnen anderer Projekte werden deren Angaben geladen; **Neu** und **Beispiel** entfernen die bisherigen Notizen. Eine Änderung der Angaben erzeugt keine neue Werkzeugwegberechnung. Die gerätespezifischen Startbedingungen bleiben bestehen.

**Öffnen** lädt das vollständige Projekt einschließlich Name und Notizen. Das Einfügen eines Projekts über den Import übernimmt seine Objekte und Operationen; Name und Notizen des bereits geöffneten Zielprojekts bleiben erhalten.

## Dateiformat

Neu gespeicherte Projekte verwenden **Format 8** mit einem eigenen Klartextfeld `notes`. Formate 1–7 bleiben lesbar und erhalten leere Notizen. Frühere Titel werden beim Lesen nicht stillschweigend normalisiert. Ältere Blazer-Versionen einschließlich beta.2 unterstützen Format 8 nicht und müssen solche Dateien ablehnen. Ein Rückspeichern als Format 7 würde Notizen verlieren und wird daher nicht angeboten.

## Zuletzt verwendete Projekte

**Zuletzt** neben **Öffnen** zeigt bis zu zehn erfolgreich geöffnete oder gespeicherte Projektdateien mit ihrem Pfad. Der jüngste Eintrag steht oben; erneutes Öffnen oder Speichern desselben Projekts erzeugt keine zweite Zeile. Fehlgeschlagene Vorgänge, Motivimporte, automatische Wiederherstellung und reine Exporte nehmen keine Datei in diese Liste auf. Der Verlauf bleibt lokal in den Anwendungseinstellungen und gehört nicht zum technischen Diagnosebericht.

Ein Klick öffnet das ausgewählte Projekt. Bei ungespeicherten Änderungen bietet die Anwendung vorher Speichern, Verwerfen und Abbrechen an. Speichern kann die Verlaufsliste neu ordnen; geöffnet wird weiterhin die ursprünglich gewählte Datei. Bricht der Benutzer ab oder schlägt das Öffnen fehl, bleibt das aktuelle Projekt erhalten.

Fehlende Dateien sind als **nicht gefunden** erkennbar und lassen sich nicht auswählen. Beim erneuten Öffnen der Liste wird die Verfügbarkeit aktualisiert. Umgezogene Dateien über den normalen Dateidialog öffnen; ihr neuer Pfad wird danach aufgenommen. Beschädigte Dateien werden beim Öffnungsversuch abgelehnt und überschreiben das aktuelle Projekt nicht.

**Verlauf leeren** entfernt nur die gespeicherte Liste. Projektdateien und das geöffnete Dokument bleiben erhalten. Ein Fehler beim Speichern der Anwendungseinstellungen darf eine erfolgreich gespeicherte Projektdatei nicht nachträglich als fehlgeschlagen behandeln; der Verlauf meldet seinen eigenen Fehler und zeigt keine erfolgreich geleerte Liste an, wenn die Änderung nicht gesichert werden konnte. Offline-Aufrufe für Screenshot, GCode-Export und Selbsttest verändern den persönlichen Verlauf nicht.

Projektvorlagen bleiben ein separater offener Teil von F01. Der vollständige [Funktionsstand](FUNCTION_ACCEPTANCE.md) und die [Abnahmekriterien](PRODUCTION_READINESS.md) bleiben maßgeblich.
