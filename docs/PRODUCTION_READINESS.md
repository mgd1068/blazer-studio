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
| T04 Vollständiger App-Ablauf | Normaler Einstieg, Import, Bearbeitung, Profilübernahme, Vorschau, GCode, Speichern/Wiederöffnen und Simulatorlauf über die echte Oberfläche prüfen; veraltete Jobs dürfen nicht starten. | T03 | Softwareprüfung bestanden; Desktop-/Gerätefälle separat |
| T05 Funktionsabnahme F01–F53 | Jede Familie mit Code-/Bedienungsnachweisen, Einschränkungen und offenem Abnahmefall verknüpfen; zugesagte Lücken systematisch abarbeiten. Keine vollständige Gleichwertigkeit aus vorhandenen Schaltern oder wenigen Tests ableiten. | T04 | Bestandsaufnahme fertig; Funktionslücken offen |
| T06 Reale Betriebsabnahme | Beaufsichtigt Pause/Fortsetzen, Stop, USB-/KVM-Verlust, Wiederverbindung und Schutzkontakt prüfen. Nur dokumentierte Bedienhandlungen; keine Umgehung von Alarmen oder Interlocks. Ergebnis je Fall eintragen. | T02, T04; Nutzer anwesend, Gerät bereit | Wartet auf Gerätebereitschaft |
| T07 Reale Ergebnisabnahme | Größere Raster/Graustufen, maßhaltige Konturen, wiederholbar vollständig getrennte Schnitte auf bezeichnetem Material und allgemeines Relief prüfen. Dicke, Fokus, Parameter, Durchgänge und Ergebnis erfassen; Druck zum Trennen zählt nicht als vollständiger Laserschnitt. | T03, T04, T06; geeignetes Material | Wartet auf Gerätebereitschaft |
| T08 Bedienung und Desktop | Beobachtete externe Anfängerabläufe sowie unterstützten Desktop, Tastatur, Dezimalkomma, Größenänderung und HiDPI prüfen; reproduzierbare Probleme beheben. | T04; echte Testpersonen für externe Fälle | Qt-Skalierung 150/200 % bestanden; Desktop-/Personenfälle offen |
| T09 Auslieferung | Aus sauberer festgelegter Revision Paket und Quellen bauen; Installation/Update/Entfernung auf frischer Laufzeit prüfen; Version, Grenzen, Anleitung und Prüfsummen konsistent ausliefern. Neuer Kandidat bleibt vor ausstehender Gesamtfreigabe als solcher gekennzeichnet. | Softwarethemen; öffentliche 1.0 zusätzlich T05–T08 | Für Softwarekandidat beta.3 bestanden; 1.0 offen |

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

- 12.11.2026, T04: Durchgängiger Simulator-/Dateirundlauf in zwei Ansichten und Offline-Isolation ergänzt; elf Programme normal/ASan grün. Sichtprüfung zeigt noch die Anzeige einer deaktivierten Operation nach Profilwahl im leeren Basic-Projekt; vor Abschluss mit Regression beheben. Native Dateiauswahl und echte Hardware bleiben separat.

- 12.11.2026, T04 abgeschlossen als Softwarefall: beide Anzeigeabweichungen vor der Korrektur reproduziert; UI und Workflow danach in beiden Ansichten normal/ASan bestanden. Herstellerprofile unverändert; kein Gerätezugriff.

- 12.11.2026, T05: [Bestandsaufnahme F01–F53](FUNCTION_ACCEPTANCE.md) enthält jede Familie genau einmal mit Teilnachweisen und konkreten Restarbeiten. Der breite Zielumfang ist weiterhin nicht fertig; Bestandsaufnahme ist keine Funktionsfreigabe. Unabhängige Auslieferungsfehler T09 werden während ausstehender realer Abnahmen behoben.

- 12.11.2026, T09 in Arbeit: Startskript baut aktuelle Quellen, Build-Herkunft wird bei jedem Build erneuert, Version zentral auf nächsten Kandidaten beta.2 gesetzt. Sechs isolierte Herkunfts-/Starttests bestanden. Quellbaum und Dokumentations-Installationsstufe prüfen 101 beziehungsweise 104 lokale Verweise. Gesamtregression und sauberer Paket-/Upgrade-/Laufzeitnachweis folgen vor einer Kandidatenabnahme.

- 12.11.2026, T09: [Kandidat beta.2](beta/CANDIDATE_BETA2.md) aus exakter Revision `18ce2e6` gebaut. 145 Quelldateien bytegleich, Download-/Installationslinks und Prüfsummen gültig. Frischer Laufzeitcontainer bestand Upgrade beta.1→beta.2, Nicht-root-Start, Manifestabgleich, Neuinstallation/Entfernung und Erhalt von Benutzerdaten. Beide GitHub-Pipelines auf Arbeitszweig und PR grün. Kein Hostupgrade, keine Hardwareaktion, kein Merge und keine öffentliche 1.0-Freigabe. Versionsumfang wurde zur späteren ausdrücklichen Entscheidung vorgelegt; bestehendes Gesamtziel bis dahin erhalten.

- 12.11.2026, T05/F01 in Arbeit: [Projektnamen und Notizen](PROJECTS.md) als Klartext und atomare, rücknehmbare Projektänderung ergänzt. Format 8 liest ältere Formate weiterhin; der bestehende beta.2-Kandidat bleibt unverändert. Kernfälle und normaler Workflow in beiden Ansichten bestehen; Gesamtregression und Prüfung mit dem tatsächlichen alten Leser folgen. Vorlagen und zuletzt verwendete Dateien bleiben offen.

- 12.11.2026, T05/F01 Projektangaben geprüft: alle 13 Programme normal/ASan grün; nach Abschluss vier betroffene Programme erneut in beiden Varianten bestanden. Tatsächliche beta.2-Binärdatei lehnt Format 8 ohne GCode ab, Format-7-Kontrolle funktioniert; neuer Leser erzeugt unveränderten GCode. Kompakter Dialog visuell geprüft, 114 lokale Verweise gültig. Projektangaben abgeschlossen als F01-Teil; nächste Teile sind Vorlagen und Dateiverlauf. Keine Hardwareaktion und keine 1.0-Freigabe.

- 12.11.2026, T05/F01 Dateiverlauf in Arbeit: nur erfolgreiche Projektzugriffe werden lokal erfasst, maximal zehn eindeutige Pfade. Fehlende Dateien, atomar abgelehnte Öffnungen, Einstellungsfehler und Leeren ohne Dateilöschung sind in Releasefällen geprüft. Workflow prüft insbesondere unveränderte Ziel-URL trotz einer durch Speichern umsortierten Liste. Gesamtregression läuft; Vorlagen bleiben offen.

- 12.11.2026, T05/F01 Dateiverlauf geprüft: Gesamtregression normal/ASan je 13 Programme grün, Workflow in beiden Ansichten und kompakte Sichtprüfung bestanden. Zusätzlicher Fehler-/Reparaturfall zeigte einen bleibenden Settings-Fehlerstatus; nach Korrektur Releaseprogramm nochmals in beiden Varianten grün. Nur erfolgreiche Projektzugriffe, richtige Ziel-URL nach Umsortierung, fehlende Dateien, Datenschutz und Leeren ohne Dateilöschung sind geprüft. Nächster F01-Teil: Vorlagen. Keine Geräteaktion und keine 1.0-Freigabe.

- 12.11.2026, T05/F01 Vorlagen in Arbeit: eigener Dateityp mit eingebetteten Inhalten, unabhängiger neuer Entwurf, Schutz vor normalem Überschreiben der Vorlage und frische Startbestätigung. Kern-/Releasefälle und Workflow in beiden Ansichten bestehen; Gesamtregression läuft. Dateityp/Starter werden im isolierten Quellinstallationsfall geprüft; echte Desktopbedienung bleibt T08.

- 12.11.2026, T05/F01 softwareseitig abgeschlossen: Vorlagen samt eingebetteten Inhalten, Dateizuordnung und tatsächlicher leerer Speichervorbelegung geprüft; reproduzierte Altpfad-/Qt-Dialogprobleme behoben. Finale Gesamtregression normal/ASan jeweils 13 Programme grün, Dialogressourcen begrenzt, Quellstarter/MIME isoliert geprüft, 114 lokale Verweise gültig. Native/externe Bedienung bleibt T08; T05 insgesamt weiterhin mit offenen Familien. Nächster Auslieferungsschritt: neuer Kandidat aus exakter Revision mit aktualisierter Runtime-/MIME-Prüfung. Keine Geräteaktion oder 1.0-Freigabe.

- 12.11.2026, T05/F01 zusätzliche Recovery-Korrektur: neuer ungespeicherter Startentwurf konnte eine noch unbestätigte ältere Sicherung überschreiben; mit echtem Timer reproduziert und behoben. Haupt-/Vorversion bleiben bis zur erfolgreichen Entscheidung geschützt; Abbruch von Beenden/Speichern kehrt zur Entscheidung zurück. Nach Sitzungsende keine weiteren Autosaves. Alle 13 Programme in beiden Varianten geprüft, UI nach Fokus-Synchronisierung separat abschließend grün; Details im QA-Protokoll. Keine persönlichen Recovery-Dateien oder Geräte benutzt. Neuer exakt gebauter Kandidat bleibt nächster Auslieferungsschritt.

- 12.11.2026, T09 beta.3: erster lokaler Kandidat ec7e732 bestand frischen Upgrade-/Laufzeitfall und bytegleiche Legacy-/Vorlagen-GCode-Ausgabe. GitHub deckte eine fehlende MIME-Abfrageabhängigkeit im separaten Python-Desktoptest auf; im Container reproduziert und durch ausdrückliches libfile-mimeinfo-perl korrigiert. Dieser erste Paketversuch wird nicht als abschließend geprüfter Kandidat geführt; sauberer Neubau und neue CI folgen.

- 12.11.2026, T09 beta.3 abgeschlossen für Softwareauslieferung: [Kandidat](beta/CANDIDATE_BETA3.md) aus exakter Revision 4da2e97. Alle 149 Quelldateien vollständig/bytegleich und ausführbare Kennzeichen erhalten, Download-/Installationslinks und Prüfsummen gültig. Frischer Nicht-root-Upgradefall beta.2→beta.3 mit tatsächlichem Format-7-Projekt, Einstellungen und Vorlagen-Dateityp bestanden; Daten auch nach Entfernung erhalten, GCode bytegleich. Alle 13 Programme normal/ASan sowie beide Paketpipelines auf Arbeitszweig und PR grün. Hardware und externe/nativ-desktopbezogene T08-Fälle bleiben offen. Nächstes Softwarethema T05/F02: Blattkopien und eindeutige aktive Ausgabe. Kein Hostupgrade, Merge oder öffentliche Freigabe.

- 12.11.2026, T05/F02 in Arbeit: tatsächliche Sprünge der aktiven Seite beim Löschen eines anderen Blatts, Gruppenverlust bei Auswahlkopie, feste Kopfzeile und veraltete Objektzeilen nach Neu-Blatt reproduziert. Vollständige unabhängige Blattkopie und korrigierte Ziel-/Ausgabeauswahl ergänzt. Kernfälle und heller Workflow einschließlich eingefrorenem Simulatorauftrag bestehen; Gesamtregression und kompakte dunkle Ansicht laufen. Entwicklung beta.4-dev bleibt vom geprüften beta.3-Paket getrennt. Keine Geräteaktion.

- 12.11.2026, T05/F02 softwareseitig geprüft: vollständige Blattkopien, Gruppenerhalt, Grenzen und gespeicherte Ausgabe sowie explizite Zielwahl ergänzt. Zusätzlich Maximalnamen-Layout und durch reine Berechnungsstatusmeldungen zurückgesetzte Zielauswahl reproduziert und korrigiert. Alle 13 Programme in beiden Varianten geprüft; nach den letzten UI-Korrekturen die betroffenen Programme ui/workflow/release nochmals normal/ASan grün. Sichtprüfung des gebauten Programms mit langem Namen in 1180×760 bestanden. Entwicklung beta.4-dev; beta.3-Paket unverändert. Nächste Familie F03: sichtbarer Undo-/Redo-Verlauf. Native/externe und reale Geräteabnahmen bleiben offen.

- 12.11.2026, T05/F03 in Arbeit: benannte, auf 50 Schritte begrenzte Historie mit aktuellem/gespeichertem Inhalt und atomarem mehrstufigem Wechsel ergänzt. Revisionstoken schützt alte Auswahlen; Liste bleibt bei Jobstatus stabil. Erste Gesamtregression normal/ASan vollständig grün. Tatsächliche Restore-/Discard-UI-Fälle mit leerem/ungespeichertem Start und nachfolgender Speicherung/Autosave ergänzt; betroffene Nachprüfungen laufen. Entwicklung weiterhin beta.4-dev, unveränderter Kandidat beta.3. Keine Geräteaktion.

- 12.11.2026, T05/F03 softwareseitig geprüft: benannter Undo-/Redo-Verlauf, aktuelle/gespeicherte Inhalte und atomare mehrstufige Wechsel mit Revisionstoken abgeschlossen. Vollständige Regression normal/ASan je13 Programme grün; nach zusätzlichen Recovery-Entscheidungen und präzisierten Importbeschreibungen die fünf betroffenen Programme nochmals in beiden Varianten bestanden. Helle/dunkle Ansicht und neuer Programm-Selbsttest geprüft. Nicht-desktopgebundene Restore-/Discard-Fälle bewahren beziehungsweise entfernen die ausdrücklich gewählten Daten korrekt. Entwicklung weiterhin beta.4-dev; unveränderter Kandidat beta.3. Nächste Familie F04: fehlende Austausch-/Geometrieimporte. Reale/native/externe Abnahmen bleiben offen.
