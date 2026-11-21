# Portable Oberflächeneinstellungen

F53-Teilumfang, Stand 21.11.2026. Blazer Studio kann eine kleine eigene JSON-Datei mit **Maßeinheit, hellem/dunklem Design und Einfach-/Erweitertmodus** exportieren, vor dem Import vergleichen und auf ausdrücklichen Aufruf gemeinsam übernehmen. Das ist kein LightBurn-User-Bundle-Konverter und keine Sicherung der gesamten Anwendung.

## Ablauf und Schnittstelle

`ApplicationServices` bietet die Eigenschaften `lengthUnit` (`mm`/`in`), `darkTheme` und `beginnerMode` mit gemeinsamem `changed`-Signal. `setLengthUnit`, `setDarkTheme` und `setBeginnerMode` speichern bewusste Änderungen. Design und Bedienmodus werden unter `ui/v1/darkTheme` und `ui/v1/beginnerMode` ergänzt; die bisherige Maßeinheit bleibt unter `editor/v1/lengthUnit`. Ohne gespeicherte Werte gelten Millimeter, helles Design und Einfachmodus.

1. `exportSettings(file)` schreibt die aktuellen drei Werte atomar in eine reguläre lokale JSON-Datei. Ein vorhandener Export wird zuvor als `.bak` gesichert.
2. `previewSettingsImport(file)` liest höchstens 16 KiB und liefert `valid`, `error`, `digest`, `preferences` sowie `changes`. Jeder Änderungseintrag enthält `key`, `label`, `current` und `incoming`. Der SHA-256-`digest` beschreibt genau die gelesenen Bytes. Eine gültige Vorschau verändert weder gespeicherte noch aktive Einstellungen.
3. Nach Anzeige und bewusster Übernahme ruft die Oberfläche `importSettings(file, digest)` auf. Die Datei wird erneut vollständig gelesen und validiert. Abweichende Bytes verlangen eine neue Vorschau. Nur die drei erlaubten Werte werden gemeinsam übernommen; erfolgreiche Persistenz geht der Aktualisierung der aktiven Eigenschaften und einem einzelnen `changed`-Signal voraus.

Das gewöhnliche Öffnen einer Projektdatei löst keinen Einstellungsimport aus. Eine abgebrochene Vorschau oder eine abgelehnte Übernahme verändert keine Präferenz. `message` enthält eine Erfolgsmeldung oder den konkreten Ablehnungsgrund.

## Dateivertrag

```json
{
    "schema": "blazer-ui-settings",
    "version": 1,
    "preferences": {
        "lengthUnit": "mm",
        "darkTheme": false,
        "beginnerMode": true
    }
}
```

Alle Felder sind verpflichtend. Unbekannte Felder, unbekannte Versionen, doppelte Schlüssel einschließlich anders geschriebener JSON-Escapes, unvollständige Dateien und falsche Typen werden vollständig abgewiesen. Ein boolescher Wert muss JSON `true`/`false` sein; `"true"` und `1` sind kein Ersatz. Es gibt keine teilweise Übernahme oder automatische Korrektur unbekannter Daten.

Dateiadressen müssen lokal und absolut sein, mit `.json` enden und dürfen weder Query noch Fragment oder fremden Host enthalten. Symbolische Verknüpfungen und nicht reguläre vorhandene Dateiziele werden abgewiesen. Die tatsächlich verwendete QSettings-Datei wird auch über kanonische Pfadidentität und unter Linux über Hardlinkidentität als Austauschziel gesperrt.

Die Übernahme verwendet QSettings mit zwingend atomarer Dateisynchronisierung. Nicht betroffene alte Einstellungen bleiben erhalten. Bei einem Schreibfehler werden auch vorgemerkte QSettings-Werte zurückgesetzt, damit ein späterer Destruktor oder Wiederholversuch keinen zuvor abgelehnten Import übernimmt. Beschädigte lokale Einstellungen werden nicht durch Import repariert oder überschrieben.

## Grenzen und Nachweise

Die Austauschdatei enthält **keine** Geräte, Anschlüsse, Netzwerkdaten, Zugangsdaten, Maschinenkalibrierungen, physische Prüffreigaben, Verbindungs-/Bereitschaftszustände, Erststartbestätigungen, Dateiverläufe, Materialprofile oder Projektinhalte. Bestehende solche Daten werden beim Präferenzimport nicht verändert. Materialbibliotheken benötigen einen eigenen, separat geprüften Sicherungs-/Mergevertrag und gehören nicht zu Version 1 dieser Datei.

`settingsbundle` prüft Schema, Typen, doppelte/unbekannte/fehlende Schlüssel, Bytebudget, Vorschau ohne Änderung, explizite Übernahme, unveränderte ausgeschlossene Altdaten, Persistenz nach neuem Serviceobjekt, Dateiveränderung nach Vorschau, ungültige Dateiadressen, Symlink-/Hardlinkschutz, kaputte lokale INI-Dateien und fehlgeschlagene Persistenz samt bewusstem Wiederholversuch. Dokument und Simulator werden dabei nicht verändert oder verbunden. `release` enthält die bisherigen Persistenz-, Diagnose- und Dateiverlauffälle als Regression.

F53 bleibt für vollständige Einstellungen, Maschinenkonfigurationssicherungen und Wiederherstellung anderer Bibliotheken offen. QML-Bedienung und Integration müssen im zusammengeführten Stand gesondert geprüft werden; diese Backendprüfung startet keine installierte Anwendung und führt keine Geräteaktion aus.

Geprüfter Backendstand auf c797aad: `settingsbundle` **7 QtTest-Fälle, 0 Fehler, 0 übersprungene Fälle**; bisheriges `release` ebenfalls bestanden. Zusätzlich wurde ein tatsächlicher finaler Schreibfehler mit schreibgeschützter Datei und Verzeichnis geprüft: aktiver und gespeicherter Stand bleiben erhalten, nach wiederhergestellten Rechten übernimmt auch der QSettings-Destruktor keine abgelehnten Werte; ein neuer bewusster Import gelingt. 233 lokale Dokumentziele lösen auf. Keine Installation und kein realer Gerätezugriff.
