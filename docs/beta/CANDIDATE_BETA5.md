# Softwarekandidat 0.9.0-beta.5

Geprüfte Paketrevision: **26e1464dd0d1ec13081c76011fb3bbeebec6f240**, Ubuntu 26.04 amd64. Dieser Bericht wird nach dem Paketbau ergänzt und verändert die Paketrevision nicht. DEB, Quellarchiv, `SOURCE_REVISION`, installierte Anwendung und Manifest stimmen überein. Keine öffentliche Gerätebeta und keine 1.0-Freigabe.

## Änderungen seit beta.4

- Deutsch und Englisch gleichzeitig bei jedem normalen Start: Laser niemals unbeaufsichtigt betreiben. Verbindungsabriss oder hängender Controller kann den Laser eingeschaltet lassen; Software kann Laser-Aus nicht garantieren. Ständig am Gerät bleiben und zur direkten Stromunterbrechung bereit sein. Bestätigung nur für diesen Start, auch bei abgeschlossener Einführung und vor Wiederherstellung; keine dauerhafte Abschaltung. Hardwaregesperrte nichtinteraktive CLI-Modi bleiben ausgenommen.
- Gemeinsame Controller-Verbindungsschnittstelle statt direkter GRBL-Felder in Machine. Bisher ausschließlich bestehendes serielles GRBL; reale Verbindungen bleiben auf den M3-Ausgabevertrag begrenzt.
- Direkte Zerstörung einer offenen GRBL-Sitzung führt vor Ressourcenabbau dieselbe Schließlogik wie explizites Trennen aus. Änderungsrückrufe aus dem sterbenden Objekt werden unterdrückt. Leerlauf M5 S0, Auftrag/Pause/Fokus Hold/Soft-Reset; bereits geschlossene Verbindungen senden nicht erneut. Das ist ein bestmöglicher Sendeversuch, keine physische Aus-Bestätigung nach Kommunikationsverlust.

[Änderungsumfang und Grenzen](CHANGES_BETA5.md). Der [beta.4-Kandidat](CANDIDATE_BETA4.md) bleibt unverändert und enthält diese Korrektur nicht. Herstellerdateien, Leistungsumrechnung und Projektformate bleiben unverändert; kein allgemeiner Leistungs-Offset.

## Quellen und Paketprüfung

Alle **199 versionierten Quelldateien** stimmen vollständig und bytegleich mit der Paketrevision überein, einschließlich Ausführungsrechten. `SOURCE_REVISION` ist korrekt; alle rekursiven Prüfsummen und 221 lokale Dokumentationsziele im Download und installierten Paket bestehen. Release-Build mit Qt 6.10.2 und GCC 15.2.0, keine C++-Compilerwarnungen.

Ein frischer Ubuntu-26.04-Laufzeitcontainer bestand Installation des bisherigen beta.4-Pakets, Upgrade auf exakt dieses beta.5-Paket, Nicht-root-Start, Manifest-/Versionsabgleich, Bildformate/Schriften/Rendering, Selbsttest, Projekt-/Vorlagenrundlauf, Menükommando ohne Datei, Neuinstallation und Entfernung. Compiler, CMake und Qt-Entwicklungspakete waren nicht installiert. Der Host wurde nicht aktualisiert.

Vor dem Upgrade lagen ein eigenes Format-7-Projekt, eine Format-8-Vorlage und echte Testbenutzereinstellungen im Benutzerverzeichnis. Alter und neuer Leser lieferten den unabhängig festgelegten Rechteck-GCode. Projekt, Vorlage, Einstellungen und Benutzerdaten-Sentinel blieben nach Update, GUI-Test, Neuinstallation und Entfernung bytegleich erhalten.

## Tatsächlich installierter Startdialog

Nach der Basisprüfung wurden ausschließlich im wegwerfbaren Container zusätzliche X11-Testwerkzeuge installiert: Xvfb, xdotool und ImageMagick. Diese sind keine zusätzlichen Produktabhängigkeiten und kein Qt-SDK. Der normale installierte Programmstart lief als Testbenutzer unter X11/xcb mit Softwaredarstellung und bereits abgeschlossener Einführung. Keine Quelleinbindung, keine Hardwaredurchreichung und kein Zugriff auf den Hostbildschirm.

Drei tatsächlich erzeugte Aufnahmen wurden visuell geprüft:

1. Erster normaler Start: vollständiger deutscher und englischer Hinweis samt Bestätigungsbutton sichtbar.
2. Nach tatsächlichem Klick auf „Verstanden / I understand“: Hinweis geschlossen, normaler Simulator-Editor sichtbar.
3. Nach Beenden des Testprozesses und erneutem Start: derselbe zweisprachige Hinweis wieder sichtbar.

Der erste private Aufnahmeversuch beendete nur eine Hintergrund-Shell; die Anwendung lief noch und sperrte korrekt die zweite Instanz. Dieser Versuch ist kein Neustartnachweis. Nach Korrektur verfolgt das Testskript den tatsächlichen runuser-Prozess und prüft nach dessen Beendigung, dass kein Programmfenster mehr existiert. Die gesamte frische Installations-/Update-/GUI-/Entfernungsprüfung wurde anschließend erfolgreich wiederholt; nur deren abschließende Bilder gelten als Nachweis.

Diese X11-Prüfung zeigt die tatsächlich installierte Oberfläche. Ein virtueller Bildschirm ersetzt keine menschliche Einsteiger-, echte Monitor-/Wayland-/HiDPI- oder Geräteabnahme.

## Regressionen

Der letzte funktionale Stand 3338283 bestand lokal **20/20 normal (145,88 s)** und **20/20 ASan/UBSan (201,08 s, `detect_leaks=0`; keine Aussage über Leckfreiheit)**. Der Kandidaten-Freeze verändert nur Versions-/Dokumentationsdateien. Neue PTY-Fälle decken direkte Sitzungszerstörung ab; vier UI-Datenfälle prüfen je zwei Starts, Text/Modalität, Escape/Außenklick, echten Bestätigungsbutton sowie unveränderte Projekt-/Erststartdaten. Helle und kompakte dunkle Ansicht wurden zusätzlich visuell geprüft.

Beide GitHub-Paketprüfungen der exakten Kandidatenrevision sind erfolgreich: [PR-Paket](https://github.com/mgd1068/blazer-studio/actions/runs/43244554738), [Arbeitszweig-Paket](https://github.com/mgd1068/blazer-studio/actions/runs/43244317811). Auch beide vollständigen Regressionsläufe der Kandidatenrevision sind erfolgreich (PR:20/20normal124,07s,20/20ASan/UBSan139,98s,detect_leaks=0): [PR-Regression](https://github.com/mgd1068/blazer-studio/actions/runs/43244554824), [Arbeitszweig-Regression](https://github.com/mgd1068/blazer-studio/actions/runs/43244317346).

## Lokale Paketprüfsummen

| Datei | SHA-256 |
|---|---|
| `blazer-studio-0.9.0-beta.5-source.tar.gz` | `64803e03b7708e44c50f006f993647b45d5b57c190a1e10ca9d7779f8b9c4853` |
| `blazer-studio_0.9.0~beta.5_amd64.deb` | `b4490ce94f13f62975fd6bdfef8b9562f9de6c310ac0a0797e879c9733ab6661` |

Die vollständige Liste liegt als `SHA256SUMS` beim Kandidaten. Diese Summen bezeichnen den lokalen Kandidaten; unabhängige Binärbauten müssen nicht bytegleich sein. [Paketverfahren](RELEASE_BUILD.md).

## Offene Freigaben

Aktuelle beaufsichtigte Geräte-/USB-/KVM-/Schutzkontaktprüfungen, vollständiger Schnitt, Relief, Werkstückmaße/-qualität und beobachteter Einsteigerlauf U01 bleiben offen. Startwarnung und Softwareprüfungen ersetzen keinen dieser Nachweise. Weitere Controller/Modelle, Geräteverwaltung und vollständiger Funktionsumfang bleiben im [Produktionsplan](../PRODUCTION_READINESS.md), [Funktionskatalog](../FUNCTION_ACCEPTANCE.md) und [Mehrgeräteplan](../MULTI_DEVICE_PLAN.md). [Testprotokoll](TEST_PROTOCOL.md), [reale Materialbefunde](../HARDWARE_ACCEPTANCE.md). Keine Geräteaktion, kein Hostupgrade, Merge oder öffentlicher Release.

Am12.11.2026 hat Mathias diesen Stand ausdrücklich als [öffentliche Softwarebeta](https://github.com/mgd1068/blazer-studio/releases/tag/v0.9.0-beta.5) mit prominent dokumentierten offenen Geräte-/Material-/Einsteigerabnahmen freigegeben. Dies ändert weder Paketrevision noch den Umfang der tatsächlichen Abnahmen. Der folgende Verbindungsfix beta.5.1 ist ein eigener Stand.
