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
| H05 | Gravur, vollständiger Schnitt, allgemeines Relief | Materialqualität und Wiederholbarkeit prüfen; vollständiger Schnitt ohne Druck zum Trennen; fehlender Versuch bleibt offen | |
| H06 | Vollständiger Ablauf in der normalen App | Import, Material, Vorschau, laserfreie Rahmenfahrt, beaufsichtigter Job und bestätigter Abschluss gehören zum selben geprüften Ablauf | |
| H07 | Werkstück nach Bearbeitung prüfen | Maße, Gegenlicht, verbleibende Verbindungen und Oberflächenqualität dokumentieren; Kamerabild allein genügt nicht | |

## Aufbau für einen neuen Gerätetest

Vor Ausführung eintragen: genaue App-Revision, Gerät/Modul, Material und Dicke beziehungsweise Grammatur, Materiallage, manuell eingestellter Fokus, Betriebsbereitschaft von Einhausung und Absaugung, vereinbarte Sollleistung/Vorschub/Durchgänge sowie anwesender Bediener. Fehlende Herstellerwerte bleiben als solche erkennbar; eigene Ergänzungen erhalten eine eigene Kennzeichnung.

Ein früheres Testprotokoll ist keine aktuelle Gerätefreigabe. Nach USB-/KVM-Wechsel oder Trennen/Entnehmen des Materials Lage und Controllerzustand neu prüfen; alte Aufträge nicht automatisch fortsetzen. Erste Betriebsprüfungen eines neuen Treiberstands erfolgen ohne Emission. Ohne anwesenden Bediener keine Geräteaktionen.

Herstellerprofile bleiben unverändert; ein pauschaler Leistungs-Offset wird nicht verwendet. Die Quellenangabe M3 Pro ist keine bestätigte Gleichsetzung mit dem vorhandenen M3. Hersteller-Gravurwerte sind keine Schnittwerte. Auch bei sichtbarer Kontur oder dunklem Raster werden Durchtrennung, Transparenz und mechanischer Zusammenhalt am Werkstück getrennt beurteilt.

Die Ergebnisse vom 11./12.11.2026 stehen in [Geräte und Materialabnahme](../HARDWARE_ACCEPTANCE.md): Rasterwirkung bestätigt; Zahlen und Felder 3/6a/6b transparent; der Ausschnitt musste mit leichtem Druck getrennt werden. Das ersetzt keine vollständige Schnitt-, Relief- oder erneute Betriebsabnahme des geänderten Treibers.

## Auswertung

Für jeden Fehler: ID, Schrittfolge, Erwartung, Beobachtung, Reproduzierbarkeit und gegebenenfalls Diagnosebericht. Hardware- und Einsteigerergebnisse werden durch echte Beobachtung erhoben und nicht durch automatische Häkchen ersetzt. Vor öffentlicher Gerätebeta müssen offene kritische Fehler und ausstehende Hardwareprüfungen geklärt sein.
