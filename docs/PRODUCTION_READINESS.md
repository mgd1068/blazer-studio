# Plan zur Produktionsreife

Stand: 12.11.2026. Ausgangspunkt ist `0.9.0-beta.1` auf `6a7b1c0` mit dem lokal hardwaregeprüften Inline-S-Fix. Die Arbeit erfolgt in aufeinanderfolgenden Themen mit nachvollziehbaren Änderungen und Abnahmen. Ein fehlender Nachweis bleibt offen; die Versionsnummer ersetzt keine Abnahme.

## Produktziel und Arbeitsregeln

Das bestehende Gesamtziel in [F01–F53](LIGHTBURN_SCOPE.md) bleibt erhalten: LightBurn Core/Pro dient als versionierte funktionale Referenz; Linux und der vorhandene Blaze X M3 sind die erste Plattform und Geräteintegration. Bereits vorhandene Teilimplementierungen werden anhand konkreter Beispiele bewertet. Hardwarebedingt nicht anwendbare Funktionen erhalten eine Begründung. Ein engerer Umfang für eine erste 1.0 wäre eine ausdrückliche Produktentscheidung, keine stillschweigende Kürzung dieser Liste.

Herstellerparameter bleiben unverändert und erhalten keinen pauschalen Leistungs-Offset. Quelle, Modell, Modul und fehlende Werte bleiben sichtbar. Eigene Versuchswerte und gemessene Ergebnisse werden getrennt geführt. Herstellerreferenzen für M3 Pro sind keine automatische Materialfreigabe für M3.

Der Nutzer hat den Laser für die Nacht ausgeschaltet. Bis zur erneuten beaufsichtigten Gerätebereitschaft erfolgen ausschließlich Softwarearbeiten und Simulator-/PTY-Tests. Keine automatische Verbindung, Bewegung, Referenzfahrt oder Emission. Externe Bedienungstests werden durch echte Testpersonen durchgeführt. Softwarebefehle ersetzen keine Hardware-Schutzfunktion.

## Reihenfolge und Abnahmekriterien

| Thema | Arbeit und überprüfbares Ergebnis | Voraussetzung | Stand |
|---|---|---|---|
| T01 Ausgabe und Leistungswechsel | Inline-S-Fix einschließlich Nullleistung und Unterbrechung durch Leerfahrt prüfen; normale und ASan/UBSan-Regression bestehen; Änderung als eigene Revision sichern. | Vorhandene Reproduktion und Materialversuche | Softwareprüfung bestanden |
| T02 Kommunikation und Zustände | Empfang fragmentierter/verschachtelter Statusdaten, Fehler, Pause/Stop, Reset und neue Verbindung mit PTY prüfen; bei Fehlern keine automatische Jobfortsetzung und keine Verwendung veralteter Freigaben. Gefundene Fehler mit Regression beheben. | T01 | Softwareprüfung bestanden |
| T03 Materialdaten und Versuchsnachweise | Herstellerdaten unverändert erhalten; reale Ergebnisse, fehlende Durchtrennung und Gegenlichtbefund dokumentieren; historische Aufbauangaben durch aktuelle, bedingte Testanweisungen ersetzen. | T02 | Bestanden für Dokumentation und Quellenintegrität |
| T04 Vollständiger App-Ablauf | Normaler Einstieg, Import, Bearbeitung, Profilübernahme, Vorschau, GCode, Speichern/Wiederöffnen und Simulatorlauf über die echte Oberfläche prüfen; veraltete Jobs dürfen nicht starten. | T03 | In Arbeit |
| T05 Funktionsabnahme F01–F53 | Jede Familie mit Code-/Bedienungsnachweisen, Einschränkungen und offenem Abnahmefall verknüpfen; zugesagte Lücken systematisch abarbeiten. Keine vollständige Gleichwertigkeit aus vorhandenen Schaltern oder wenigen Tests ableiten. | T04 | Offen |
| T06 Reale Betriebsabnahme | Beaufsichtigt Pause/Fortsetzen, Stop, USB-/KVM-Verlust, Wiederverbindung und Schutzkontakt prüfen. Nur dokumentierte Bedienhandlungen; keine Umgehung von Alarmen oder Interlocks. Ergebnis je Fall eintragen. | T02, T04; Nutzer anwesend, Gerät bereit | Wartet auf Gerätebereitschaft |
| T07 Reale Ergebnisabnahme | Größere Raster/Graustufen, maßhaltige Konturen, wiederholbar vollständig getrennte Schnitte auf bezeichnetem Material und allgemeines Relief prüfen. Dicke, Fokus, Parameter, Durchgänge und Ergebnis erfassen; Druck zum Trennen zählt nicht als vollständiger Laserschnitt. | T03, T04, T06; geeignetes Material | Wartet auf Gerätebereitschaft |
| T08 Bedienung und Desktop | Beobachtete externe Anfängerabläufe sowie unterstützten Desktop, Tastatur, Dezimalkomma, Größenänderung und HiDPI prüfen; reproduzierbare Probleme beheben. | T04; echte Testpersonen für externe Fälle | Offen |
| T09 Auslieferung | Aus sauberer festgelegter Revision Paket und Quellen bauen; Installation/Update/Entfernung auf frischer Laufzeit prüfen; Version, Grenzen, Anleitung und Prüfsummen konsistent ausliefern. Neuer Kandidat bleibt vor ausstehender Gesamtfreigabe als solcher gekennzeichnet. | Softwarethemen; öffentliche 1.0 zusätzlich T05–T08 | Offen |

Softwarearbeiten an späteren Themen können vorbereitet werden, wenn ein physischer Nachweis fehlt. Der fehlende Nachweis wird dadurch nicht geschlossen. Jede abgeschlossene Änderung erhält relevante Tests; erneute Komplettläufe erfolgen bei neuen Änderungen oder offenen Fehlern.

## Bereits vorhandene Evidenz

- 0.9 enthält Projektwiederherstellung, Hintergrundplanung, Simulator, einen breiten Editor-/Raster-/Reliefumfang und eine geprüfte Ubuntu-26.04-amd64-Paketstrecke. [Stabilisierung](STABILIZATION_08.md), [Prüfbericht](QA_REPORT.md), [Paketbau](beta/RELEASE_BUILD.md).
- Die reale Kraftpapiermatrix mit neun Feldern wurde einmal je Feld ausgeführt. Bei 4000/5000 mm/min wurden volle Sollvorschub-/S-Werte gemeinsam erfasst; bei den kurzen 6000er-Zeilen getrennt. Die Zahlen sowie Felder 3, 6a und 6b übertragen nach Nutzerprüfung Licht. Das bestätigt Materialwirkung, keine reine Oberflächengravur und keine optische 5-W-Messung.
- Der anschließende 30 × 40-mm-Ausschnitt bei 100 % und 1000 mm/min war nahezu durchgehend, musste aber mit leichtem Druck getrennt werden. Das ist keine erfolgreiche vollständige Schnittabnahme.
- Die Hardwareversuche nutzten Prüfprogramme mit Blazer-Kern und Geräteadapter. Der vollständige reale Bedienungsablauf der normalen Anwendung bleibt separat abzunehmen.
- Webcamaufnahmen und eine vorläufige geometrische Zuordnung existieren außerhalb der App. Ein integriertes, abgenommenes Live-Kamerasystem ist damit noch nicht geliefert.

## Freigabe einer 1.0

Für den ausdrücklich zugesagten Produktumfang müssen die zugehörigen Funktions-, Betriebs-, Ergebnis- und Bedienungsfälle bestanden sein. Keine bekannten kritischen Fehler bei Datenhaltung, Jobzuordnung, Leistungswerten oder Abschaltung. Das geprüfte Installationspaket muss aus genau der freigegebenen Revision stammen. Offene, nicht zugesagte Erweiterungen bleiben nachvollziehbar in der Zielmatrix; zugesagte Lücken werden vor einer entsprechenden 1.0-Zusage geschlossen.

## Arbeitsjournal

- 12.11.2026: Plan angelegt; isolierter Arbeitszweig `codex/production-readiness`. Nutzer schläft, Laser ausgeschaltet. Start mit T01, keine Hardwarezugriffe.

- 12.11.2026, T01: Inline-S-Ausgabe einschließlich Nullleistung und Rasterneustart nach M5 geprüft. Alle zehn CTest-Programme normal und mit ASan/UBSan bestanden (`detect_leaks=0`; keine Leckfreiheitszusage). Keine Geräteverbindung. Nächstes Thema: Kommunikation und Zustände.

- 12.11.2026, T02: Empfangs-/Freigabe-/Abbruchfälle und USB-Fehlerrekursion behoben; 25 GRBL-Fälle sowie alle zehn Programme normal/ASan bestanden. Anschließend Arbeitsnullpunkt gegen physische Bewegungsgrenzen prüfen, bevor das Kommunikationsthema geschlossen wird. Reale Wiederabnahme bleibt T06.

- 12.11.2026, T02: Physische Zielgrenzen einschließlich G54 ergänzt; 31 GRBL-Fälle und alle zehn Programme normal/ASan bestanden. Nächster Randfall: Nullpunktwechsel darf keine vor dessen Abschluss empfangene Offsetmeldung wiederverwenden.

- 12.11.2026, T02 abgeschlossen für Software: Nullpunktwechsel gegen alte WCO-Meldung abgesichert; 32 GRBL-Fälle normal/ASan grün. Der vollständige vorherige Grenzprüfungsstand hatte zehn grüne Programme in beiden Varianten. T03 beginnt; T06 bleibt echte Geräteabnahme.

- 12.11.2026, T03: Geräte-/Materialprotokoll und aktuelle Beta-Anweisungen berichtigt, Herstellerdateien unverändert, Kern/UI/Katalog normal und mit ASan grün. T04 startet. Weiterführende Dokumentationslinks müssen auch im installierten Paket und Downloadarchiv auflösbar sein (T09).
