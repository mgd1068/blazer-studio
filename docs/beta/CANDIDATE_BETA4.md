# Softwarekandidat 0.9.0-beta.4

Geprüfte Paketrevision: **302a30d9acebf12d8e5bea087870cfe2bda84ff5**, Ubuntu 26.04 amd64. Dieses Protokoll wird nach dem Paketbau ergänzt und verändert die Paketrevision nicht. DEB, Quellarchiv, `SOURCE_REVISION`, installierte Anwendung und Manifest nennen dieselbe Version und vollständige Revision. Kein öffentlicher Release und keine 1.0-Freigabe.

## Umfang und Updategrenzen

[Änderungen seit beta.3](CHANGES_BETA4.md): Blattkopien, Bearbeitungsverlauf, Zwischenablage, mm/Zoll, Hilfslinien und Hilfsformen, getrennte Rahmenziele, weitere begrenzte Geometrieimporte, Bildexporte sowie durchgängige Projektprofile und neutrale Controlleradapter. Echte Verbindungen bleiben auf den bisherigen M3-Ausgabevertrag begrenzt; andere bekannte GRBL-XY-Profile sind ausschließlich offline nutzbar. Keine Zusage allgemeiner Laserunterstützung.

Standardprojekte bleiben Format 8; Hilfslinien verwenden 9, Hilfsformen 10 und abweichende Geräteprofile 11. Beta.3 kann 9–11 nicht öffnen. Herstellerdateien bleiben unverändert; kein pauschaler Leistungs-Offset. Der [beta.3-Kandidat](CANDIDATE_BETA3.md) bleibt unverändert.

## Quellen und Auslieferung

Alle **195 versionierten Quelldateien** sind vollständig und bytegleich zur Paketrevision. Ausführungsrechte sind erhalten; `SOURCE_REVISION` stimmt. Rekursive Prüfsummen und sämtliche 199 lokalen Dokumentationsziele im Download und installierten Paket bestehen. Release-Build mit Qt 6.10.2 und GCC 15.2.0; keine Compilerwarnungen im abschließenden Buildprotokoll.

Ein frischer Ubuntu-26.04-Laufzeitcontainer bestand Installation des bisherigen beta.3-Pakets, Upgrade auf exakt dieses beta.4-Paket, Nicht-root-Start, Manifest-/Versionsabgleich, Bildformate/Schriften/Rendering, Selbsttest, Projekt-/Vorlagenrundlauf, Startmenükommando ohne Datei, Neuinstallation und Entfernung. Compiler, CMake und Qt-Entwicklungspakete waren nicht installiert. Der Host wurde nicht aktualisiert.

Vor dem Upgrade lagen ein eigenes Format-7-Rechteck, eine Format-8-Vorlage und echte Testbenutzereinstellungen im Benutzerverzeichnis. Alter und neuer Leser lieferten den unabhängig festgelegten Rechteck-GCode. Vorlage, Projekt und Einstellungen blieben bytegleich nach Update, Neuinstallation und Entfernung; ebenso der zusätzliche Benutzerdaten-Sentinel.

Zusätzliche unabhängige Paketvergleiche prüften eigene synthetische Formate 9, 10 und 11 sowie eine Profilvorlage: Hilfslinien und T1-Hilfsformen verändern den GCode nicht. Das 600×400-mm-Profil erhält dieselben Koordinaten und Vorschübe, skaliert den S-Bereich unabhängig von 1000 auf 255 und liefert für 80 % exakt S204. Profilprojekt und Profilvorlage ergeben bytegleiche Ausgabe. Die CLI-Exporte laufen ohne Hardwarezugriff.

Der erste zusätzliche Profilvergleich hatte einen Fehler im privaten Sollwertskript: Beim Umskalieren fehlte das S-Präfix. Die Anwendung gab korrekt S204 aus. Nach Korrektur wurde die vollständige frische Installations-/Upgrade-/Entfernungsprüfung erneut erfolgreich abgeschlossen; der vorherige fehlgeschlagene Versuch gilt nicht als Abschlussnachweis.

Lokale relevante Release-/Dokumentationsprüfung: **2/2 bestanden**. Die unveränderten C++-Funktionen des vorherigen Standes e78608e bestanden 20/20 normal und 20/20 mit ASan/UBSan (`detect_leaks=0`, keine Aussage über Leckfreiheit). Beide unabhängigen GitHub-Paketprüfungen der exakten Kandidatenrevision sind erfolgreich: [PR-Paket](https://github.com/mgd1068/blazer-studio/actions/runs/43237947564), [Arbeitszweig-Paket](https://github.com/mgd1068/blazer-studio/actions/runs/43237933231). Die vollständigen Regressionsläufe dieser Revision werden getrennt beobachtet: [PR-Regression](https://github.com/mgd1068/blazer-studio/actions/runs/43237947561), [Arbeitszweig-Regression](https://github.com/mgd1068/blazer-studio/actions/runs/43237933230).

## Lokale Paketprüfsummen

| Datei | SHA-256 |
|---|---|
| `blazer-studio-0.9.0-beta.4-source.tar.gz` | `6f421ad8287a279d5695e8f57b3704d10d90b72c7a1af27b97dce2c2e2950526` |
| `blazer-studio_0.9.0~beta.4_amd64.deb` | `673d26fcd4ebf41cc7cdb95ca5a81bb2f977e514a50cf9f1d122540a8dfc99e3` |

Die vollständige Liste liegt beim Kandidaten als `SHA256SUMS`. Diese Summen bezeichnen den lokalen Kandidaten; unabhängige Binärbauten müssen nicht bytegleich sein. Verfahren: [Paketbau und Laufzeitprüfung](RELEASE_BUILD.md).

## Offene Freigaben

Beaufsichtigte aktuelle Geräte-/USB-/KVM-/Schutzkontaktfälle, vollständiger Schnitt, Relief, unabhängige Maß-/Qualitätsabnahme und der beobachtete Einsteigerlauf U01 sind offen. Offscreen-Softwaredarstellung ersetzt keinen tatsächlichen Wayland-/X11-/Monitorbetrieb oder native Dateiauswahl. Frühere Kraftpapierversuche ersetzen diese Abnahmen nicht.

Der volle [Funktionskatalog](../FUNCTION_ACCEPTANCE.md) und die [Mehrgeräteentwicklung](../MULTI_DEVICE_PLAN.md) bleiben Arbeitsumfang für die Produktionsreife. [Produktionsplan](../PRODUCTION_READINESS.md), [Testprotokoll](TEST_PROTOCOL.md), [Geräte-/Materialbefunde](../HARDWARE_ACCEPTANCE.md). Keine Geräteaktion, kein Merge und keine Veröffentlichung durch diesen Paketnachweis.
