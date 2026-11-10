# Testprotokoll – Ergebnisse tatsächlich eintragen

Version / Revision:
Testerkennung (frei wählbar):
Datum:
Linux-Version / Architektur:
Modus (Simulator / echtes Gerät):

Für jeden Fall **bestanden**, **fehlgeschlagen** oder **nicht durchgeführt** eintragen. Nicht durchgeführte Tests gelten nicht als bestanden. Keine Namen oder privaten Projekte nötig.

| ID | Test | Erwartung | Ergebnis / Beobachtung |
|---|---|---|---|
| S01 | Paket auf sauberem unterstütztem System installieren | Start ohne Compiler/SDK, Menüsymbol und Einführung sichtbar | |
| S02 | Übungsprojekt im Simulator | Gerät bleibt unbenutzt; Auswahl, Maße und Vorschau verständlich | |
| S03 | Eigene PNG-/SVG-Datei importieren, Material wählen | Auswahl/Parameter/Vorschau stimmen zusammen | |
| S04 | Speichern, Schließen mit Speichern/Verwerfen/Abbrechen, Wiederöffnen | Keine unbeabsichtigte Änderung oder Datenverlust | |
| S05 | Große Rasterberechnung abbrechen und neu starten | Oberfläche bedienbar, kein veralteter Auftrag startbar | |
| S06 | Fehlerbericht erzeugen | Version/Zustände enthalten; keine Projektinhalte oder Geräteprotokolle | |
| S07 | Zweite Instanz und Wiederherstellung prüfen | Keine konkurrierende Überschreibung, gute Sicherung bleibt erhalten | |
| U01 | Anfänger durchläuft S02 ohne Eingriff | Stolperstellen, benötigte Hilfe und Zeit dokumentieren | |
| H01 | Tatsächliches Gerät, Firmware/Einheiten/Grenzen prüfen | Dokumentiertes passendes Profil, kein automatischer Laserstart | |
| H02 | Rahmenfahrt mit ausgeschaltetem Laser | Motiv innerhalb des eingelegten Materials | |
| H03 | Kleine begleitete Markierung | Ergebnis und Leistung dokumentieren, danach Laser-Aus bestätigt | |
| H04 | Pause, Fortsetzen, Stop, normale Trennung und Wiederverbindung | Keine automatische Wiederaufnahme, definierter Leerlauf | |
| H05 | Gravur, Schnittkontur, allgemeines Relief | Jeweils Ergebnis und Wiederholbarkeit bewerten; Schnittmodus ist nicht gleich erfolgreicher Materialdurchtrennung | |

## Aktueller Auftrag am vorhandenen M3

Der Nutzer hat Kraftpapier von ungefähr 100×100 mm in der Mitte eingelegt und für reale Tests **maximal 5 % Sollleistung** vorgegeben. Diese Obergrenze ist verbindlich und darf bei fehlender Wirkung nicht erhöht werden. Die Lüfter-/Absaugbereitschaft und das geschlossene Gehäuse müssen vor Emission bestätigt sein. Aktuell bestätigt: Lüfter aus; keine Emissionsfreigabe aus einem Softwaretest ableiten.

Ein Vorschlag für einen kleinen, materialschonenden Test liegt nahe X100/Y75 im 200×150-mm-Arbeitsraum. Erst Materiallage und laserfreie Rahmenfahrt prüfen; dann eine kurze Markierung innerhalb der vorgegebenen Grenze. Bei 5 % kann Materialdurchtrennung oder sichtbarer Abtrag ausbleiben. Dann das Ergebnis als solches protokollieren. Daraus folgen weder höhere Leistung noch eine allgemeine Abtragskalibrierung.

## Auswertung

Für jeden Fehler: ID, Schrittfolge, Erwartung, Beobachtung, Reproduzierbarkeit und gegebenenfalls Diagnosebericht. Hardware- und Einsteigerergebnisse werden durch echte Beobachtung erhoben und nicht durch automatische Häkchen ersetzt. Vor öffentlicher Gerätebeta müssen offene kritische Fehler und ausstehende Hardwareprüfungen geklärt sein.
