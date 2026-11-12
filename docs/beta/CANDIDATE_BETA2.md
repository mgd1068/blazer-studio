# Softwarekandidat 0.9.0-beta.2

Geprüfte Paketrevision: **18ce2e67e691d1d6f117132f22d4479e9243ec4d**, Ubuntu 26.04 amd64. Dieses Protokoll wird nach dem Paketbau ergänzt; es ändert die geprüfte Paketrevision nicht. Paket, Quellarchiv und Manifest nennen dieselbe Revision. Der Kandidat ist keine 1.0-Freigabe.

## Änderungen seit beta.1

- Leistungswechsel innerhalb laufender M4-Rasterzeilen stehen auf Bewegungsblöcken; Leerfahrten bleiben laserfrei.
- GRBL verlangt aktuelle Positions-, Offset- und Laser-Aus-Rückmeldungen. Door/Alarm/Transportfehler verwerfen den Auftrag. Physische Grenzen berücksichtigen G54, Nullpunktwechsel benötigen eine spätere Offsetbestätigung.
- Der vollständige App-Ablauf ist mit der tatsächlichen Machine-Fassade im Simulator geprüft. Offline-CLI-Modi öffnen keine Hardware und verändern keine Benutzerbibliothek.
- Basic zeigt die tatsächlich aktive Bearbeitung an. Abgelehnte Materialprofile bleiben nicht sichtbar ausgewählt.
- Checkout-Start baut aktuelle Quellen; Buildfehler starten keine vorhandene alte Binärdatei. Version und Herkunft werden konsistent ermittelt.
- Installiertes Paket und Download enthalten die vollständige Dokumentationsstruktur einschließlich Abbildungen und Lizenzen.

Herstellerdateien bleiben unverändert. Es wurde kein pauschaler Leistungs-Offset eingeführt.

## Software und Auslieferung

Alle 13 CTest-Programme bestanden normal und mit ASan/UBSan (`detect_leaks=0`, keine Aussage über Leckfreiheit). Der vollständige Workflow bestand zusätzlich bei 150 % und 200 % Qt-Skalierung im Offscreen-/Software-Backend. Dies ersetzt keinen echten Desktop- oder Einsteigertest.

Der lokale Paketbau erfolgte aus einem sauberen Archiv der genannten Revision. Alle 145 versionierten Quelldateien wurden bytegleich zur Revision geprüft. Alle rekursiven Downloadprüfsummen sowie 104 lokale Dokumentationsverweise im Download und installierten Paket wurden bestätigt.

Ein frischer Ubuntu-Laufzeitcontainer bestand: Installation und Selbsttest von beta.1 (`6a7b1c0`), Upgrade auf beta.2, Start als normaler Benutzer, Bildformate/Schriften/Rendern, Projektrundlauf/GCode, Startmenükommando ohne Datei, Neuinstallation und Entfernung. Vor dem Upgrade angelegte Benutzerdaten blieben erhalten. Compiler, CMake und Qt-Entwicklungspakete waren nicht installiert. Programmversion, vollständige Quellrevision und Debian-Version stimmten mit dem Manifest überein. Der Host wurde dabei nicht aktualisiert.

Die GitHub-Läufe auf dem exakten Arbeitszweig bestanden: [Regression normal/ASan](https://github.com/mgd1068/blazer-studio/actions/runs/43129109372), [unabhängiger Paketbau und frische Laufzeit](https://github.com/mgd1068/blazer-studio/actions/runs/43129109333). Auch die Prüfungen des [Entwurfs-PRs](https://github.com/mgd1068/blazer-studio/pull/1) bestanden: [Regression](https://github.com/mgd1068/blazer-studio/actions/runs/43129150113), [Paket](https://github.com/mgd1068/blazer-studio/actions/runs/43129150199).

## Lokale Paketprüfsummen

| Datei | SHA-256 |
|---|---|
| `blazer-studio_0.9.0~beta.2_amd64.deb` | `3064ef0394f9211ea0c43be5c11dd0af9eeeb29dfce5efb40f98e3c486239b9e` |
| `blazer-studio-0.9.0-beta.2-source.tar.gz` | `afca68f87ef8daad8e199f8ceb0a60ec718eab0d721a14dddadd82072ee77f6f` |

Die vollständige Liste liegt im Download als `SHA256SUMS`. Die obigen Summen bezeichnen den lokalen Kandidaten; unabhängige Binärbauten müssen nicht bytegleich sein.

## Offene Freigaben

Der neue Treiberstand braucht beaufsichtigte Geräteabnahmen. Vollständiger Schnitt, Relief, reproduzierbare Qualität und unabhängige Maße sind offen. Der reale Papierschnitt ließ sich nur mit Druck trennen. Externe Einsteiger, native Dateidialoge und tatsächlicher Wayland-/X11-/Monitorbetrieb sind noch nicht abgenommen.

Der breite [Funktionskatalog](../FUNCTION_ACCEPTANCE.md) enthält verbleibende Fähigkeiten. Eine spätere ausdrückliche Entscheidung über den Versionsumfang ersetzt keine reale Prüfung. Bis dahin bleibt das bestehende Gesamtziel erhalten. [Produktionsplan](../PRODUCTION_READINESS.md), [Geräte-/Materialbefunde](../HARDWARE_ACCEPTANCE.md).

Während der Nacht wurden ausschließlich Software, Simulator und PTYs verwendet. Es fand keine Verbindung zum ausgeschalteten Laser statt.
