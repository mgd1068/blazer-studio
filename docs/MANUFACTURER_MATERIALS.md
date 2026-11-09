# Materialvorgaben des Herstellers

Quelle: [BlazeX – Maschineneinstellung](https://de.blazexlaser.com/pages/maschineneinstellung), abgerufen am 09.11.2026. Die Seite wurde direkt per HTTPS gelesen, nachdem der Web-Lesedienst sie nicht öffnen konnte.

## Modellzuordnung

Die Seite bezeichnet die Modelle als **M3 Pro**. Das vorhandene LightBurn-Profil und die bisherigen Projekthinweise nennen **M3**. Deshalb wird diese Sammlung als Herstellerreferenz geführt; sie wird nicht automatisch als validierte Materialbibliothek für Mathias’ Gerät aktiviert.

## Datenbestand

`profiles/manufacturer-materials.json` enthält die fünf Tabellen mit insgesamt 64 Einträgen:

| Quellmodell / Modul | Bearbeitung | Einträge |
|---|---|---|
| M3 Pro / 5 W | Gravur | 15 |
| M3 Pro / 5 W | Schnitt | 11 |
| M3 Pro / 10 W | Gravur | 14 |
| M3 Pro / 10 W | Schnitt | 11 |
| M3 Pro / 1 W | Gravur | 13 |

Leistung bleibt in Prozent, Geschwindigkeit in **mm/min**, Gravurintervall in mm und Schnittwiederholungen als Ganzzahl. Gravur-Durchgänge sind nicht angegeben und bleiben `null`; daraus wird nicht automatisch ein Durchgang. Auch die exakten Materialbezeichnungen, ursprünglichen Zellen, Tabellenüberschriften, URL, Abrufdatum und der SHA-256 der abgerufenen Seite werden erfasst. Leere Tabellenzeilen werden verworfen.

Beispiele für **M3 Pro / 5 W**, nicht als bereits geprüfte Werte für M3 zu interpretieren:

- Sperrholzgravur: 100 %, 5000 mm/min, Intervall 0,10 mm.
- Schnitt Sperrholz 3 mm: 100 %, 250 mm/min, ein Durchgang.

Ein auffälliger Quellwert bleibt ausdrücklich unverändert: 5-W-Schnitt von Sperrholz 10 mm wird mit einem Durchgang angegeben, während bei 8 mm sechs Durchgänge stehen. Die Plausibilität und Übertragbarkeit müssen vor Geräteausgabe geprüft werden; keine stillschweigende Korrektur. Das 1-W-Modul wird auf der Seite nicht zum Schneiden empfohlen.

## Bedienung im Entwurf

Links unter Material öffnet **Herstellerwerte ansehen** die Referenztabellen. Die Modul-/Bearbeitungsauswahl wechselt die Tabelle; es gibt bewusst keine automatische Übernahme in die beiden vorhandenen Bearbeitungen. Quelle und offene Modellzuordnung werden angezeigt. Aktive Projektwerte und die ursprüngliche LightBurn-Konfiguration bleiben unverändert.

Die Daten sind als Grundlage für eine spätere auswählbare Materialbibliothek vorbereitet. Vor Übernahme gehört eine bestätigte Modell-/Modulzuordnung dazu. Eigene Materialtests werden als eigene Datensätze gespeichert und überschreiben die Herstellerquelle nicht.

## Erneut extrahieren

Das Hilfsskript verarbeitet eine lokal gespeicherte Herstellerseite und benötigt Python mit BeautifulSoup4:

```bash
python3 tools/import_manufacturer_materials.py /tmp/blazex-settings.html --retrieved-on 2026-11-09
```

Nach einer Quellenänderung muss das Schema-/Tabellenmapping geprüft werden. Das Skript bricht ab, wenn die erwarteten Tabellenzahlen oder Wertebereiche nicht stimmen. Es gibt keine automatische Aktualisierung im Programm.
