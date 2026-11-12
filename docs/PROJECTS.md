# Projektname und Notizen

Entwicklungsstand nach dem geprüften Kandidaten beta.2, ab `0.9.0-beta.3-dev`.

Oben links auf den Projektnamen klicken. **Projektangaben** öffnet einen Entwurf für Name und Notizen. **Übernehmen** ändert das geöffnete Projekt; **Abbrechen** oder Escape verwirft den Entwurf. Danach das Projekt speichern. Undo/Redo kann eine übernommene Änderung zurücknehmen beziehungsweise wiederholen.

Der Name ist einzeilig, hat höchstens 1000 Zeichen und darf nach Entfernen äußerer Leerzeichen nicht leer sein. Die Notizen dürfen bis zu 16000 Zeichen enthalten. Sie werden als Klartext angezeigt; beispielsweise bleibt `<b>Text</b>` wörtlich erhalten.

Notizen eignen sich für Materialbezeichnung, Dicke, Fokus, Versuch und beobachtetes Ergebnis. Sie ändern keine Bearbeitungsparameter. Herstellerwerte bleiben gesonderte Referenzen; eigene Beobachtungen sind keine allgemeine Materialfreigabe. Titel und Notizen gehen nicht in den Laser-GCode oder den technischen Diagnoseexport ein.

Übernommene Angaben gehören zum Projekt und seiner automatischen Wiederherstellung. Beim Öffnen anderer Projekte werden deren Angaben geladen; **Neu** und **Beispiel** entfernen die bisherigen Notizen. Eine Änderung der Angaben erzeugt keine neue Werkzeugwegberechnung. Die gerätespezifischen Startbedingungen bleiben bestehen.

**Öffnen** lädt das vollständige Projekt einschließlich Name und Notizen. Das Einfügen eines Projekts über den Import übernimmt seine Objekte und Operationen; Name und Notizen des bereits geöffneten Zielprojekts bleiben erhalten.

## Dateiformat

Neu gespeicherte Projekte verwenden **Format 8** mit einem eigenen Klartextfeld `notes`. Formate 1–7 bleiben lesbar und erhalten leere Notizen. Frühere Titel werden beim Lesen nicht stillschweigend normalisiert. Ältere Blazer-Versionen einschließlich beta.2 unterstützen Format 8 nicht und müssen solche Dateien ablehnen. Ein Rückspeichern als Format 7 würde Notizen verlieren und wird daher nicht angeboten.

Projektvorlagen und eine Liste zuletzt verwendeter Dateien sind separate offene Teile von F01. Dieser Dialog liefert diese Fähigkeiten nicht. Der vollständige [Funktionsstand](FUNCTION_ACCEPTANCE.md) und die [Abnahmekriterien](PRODUCTION_READINESS.md) bleiben maßgeblich.
