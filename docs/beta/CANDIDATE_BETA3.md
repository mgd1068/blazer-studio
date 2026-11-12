# Softwarekandidat 0.9.0-beta.3

Geprüfte Paketrevision: **4da2e97a82594bf85836210d521c797a946c95e0**, Ubuntu 26.04 amd64. Dieses Protokoll wird nach dem Paketbau ergänzt und verändert die Paketrevision nicht. DEB, Quellarchiv, `SOURCE_REVISION`, installierte Anwendung und Manifest nennen dieselbe Version und vollständige Revision. Der Kandidat ist keine 1.0-Freigabe.

## Änderungen seit beta.2

- Projektname und Klartextnotizen mit Undo/Redo, Dateispeicherung und Wiederherstellung. Format 8 liest Formate 1–7; beta.2 kann Format 8 nicht öffnen.
- Lokaler Verlauf mit höchstens zehn erfolgreich geöffneten oder gespeicherten Projekten; Leeren löscht keine Dateien. Fehlende Dateien bleiben erkennbar. Import, Export und Wiederherstellung nehmen keine Datei auf.
- Eigene eingebettete Projektvorlagen als `.blazertemplate`. Neu aus Vorlage erzeugt einen unabhängigen ungespeicherten Entwurf mit frischer Historie und ohne übernommene Startfreigabe. Normales Projektspeichern schützt die Vorlage auch über Symlink-Aliase.
- Der Speicherdialog zeigt die aktuelle Dokumentdatei beziehungsweise einen tatsächlich leeren Namen. Neu, Beispiel, Vorlage und Wiederherstellung übernehmen keinen alten Speicherpfad. Die Zahl eigener Qt-Fallback-Dialogressourcen bleibt begrenzt.
- Eine offene Wiederherstellungsentscheidung schützt alte Haupt-/Vorversion vor Autosave eines neuen Startentwurfs. Fehlgeschlagene Entscheidungen lösen diesen Schutz nicht; nach bestätigter Entscheidung läuft Autosave wieder. Nach Sitzungsende startet es nicht erneut.
- Vorlagen-Dateityp und Starterargumente sind integriert. Offline-CLI-Aufrufe bleiben ohne Gerätezugriff und ohne Änderungen persönlicher Einstellungen.

[Projektbedienung und Formatgrenzen](../PROJECTS.md). Herstellerdateien bleiben unverändert; es wurde kein allgemeiner Leistungs-Offset eingeführt. Der [beta.2-Kandidat](CANDIDATE_BETA2.md) bleibt unverändert auf seiner eigenen Revision.

## Software und Auslieferung

Die vollständige Quellenliste umfasst 149 versionierte Dateien. Namen und Inhalte sind vollständig und bytegleich zur Paketrevision; ausführbare Dateien behalten ihr Ausführungskennzeichen. Das Quellarchiv enthält zusätzlich `SOURCE_REVISION`. Alle rekursiven Prüfsummen und die 117 lokalen Dokumentationsverweise im Download sowie im installierten Paket bestehen.

Ein frischer Ubuntu-26.04-Laufzeitcontainer bestand Installation und Selbsttest von beta.2, Upgrade auf beta.3, Nicht-root-Start, Manifest-/Versionsabgleich, Bildformate/Schriften/Rendering, Projekt-/Vorlagenrundlauf, Startmenükommando ohne Datei, Neuinstallation und Entfernung. Compiler, CMake und Qt-Entwicklungspakete waren nicht installiert. Qt erkennt die tatsächlich registrierte Vorlagenkennung `application/x-blazer-template`.

Zusätzlich lag vor dem Upgrade eine tatsächlich vom beta.2-Leser geprüfte Format-7-Projektdatei im Benutzerverzeichnis. Nach dem Upgrade blieben Projekt und Einstellungen bytegleich erhalten. Alte und neue Anwendung sowie eine aus derselben Geometrie erzeugte Format-8-Vorlage exportierten bytegleichen GCode. Projekt, Einstellungen und der zusätzliche Benutzerdaten-Sentinel blieben auch nach Entfernung erhalten. Der Host wurde nicht aktualisiert.

Die erste GitHub-Regression erkannte eine fehlende Testabhängigkeit des generischen `xdg-mime`-Backends. Der Fehler wurde im Container reproduziert und mit `libfile-mimeinfo-perl` behoben. Alle acht Build-/Starterfälle bestehen im korrigierten isolierten Builder. Ein älterer Paketversuch wird nicht als dieser abschließende Kandidat geführt.

Die genaue Paketrevision bestand alle 13 CTest-Programme auf GitHub normal (90,25 s) und mit ASan/UBSan (100,50 s, `detect_leaks=0`; keine Aussage über Leckfreiheit). Die Prüfungen des [Entwurfs-PRs](https://github.com/mgd1068/blazer-studio/pull/1) sind grün: [Regression](https://github.com/mgd1068/blazer-studio/actions/runs/43149078119), [unabhängiger Paketbau/frische Laufzeit](https://github.com/mgd1068/blazer-studio/actions/runs/43149078128). Ebenso bestanden die Arbeitszweigläufe [Regression](https://github.com/mgd1068/blazer-studio/actions/runs/43149066540) und [Paket](https://github.com/mgd1068/blazer-studio/actions/runs/43149066610).

## Lokale Paketprüfsummen

| Datei | SHA-256 |
|---|---|
| `blazer-studio-0.9.0-beta.3-source.tar.gz` | `e902a7ed5824d25775afe348828d6c974c1ec7596541db025c23c2f025d408d0` |
| `blazer-studio_0.9.0~beta.3_amd64.deb` | `a46fc8bc4b3b99dd7499cfc50bbb1d3a44113a52693bc3c7480c7bea66b780ff` |

Die vollständige Liste liegt im Download als `SHA256SUMS`. Diese Summen bezeichnen den lokalen Kandidaten; unabhängige Binärbauten müssen nicht bytegleich sein.

## Offene Freigaben

Der breite [Funktionskatalog](../FUNCTION_ACCEPTANCE.md) enthält weiterhin offene Fähigkeiten. Beaufsichtigte Geräte-/USB-/KVM-/Schutzkontaktfälle, vollständiger Schnitt, Relief, unabhängige Maß-/Qualitätsabnahme und externe Einsteiger sind offen. Die Paketprüfung verwendet Qt offscreen mit Software-Backend und ersetzt keinen tatsächlichen Wayland-/X11-/Monitorbetrieb oder eine native Dateiauswahl.

[Produktionsplan](../PRODUCTION_READINESS.md), [Geräte-/Materialbefunde](../HARDWARE_ACCEPTANCE.md), [Prüfbericht](../QA_REPORT.md). Während der Nacht ausschließlich Software, Simulator und PTYs; keine Verbindung zum ausgeschalteten Laser, keine Bewegung, kein Homing und keine Emission. Kein Merge oder öffentliche Freigabe.
