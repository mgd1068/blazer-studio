# Mehrere Laser und Controller

Mathias hat am 12.11.2026 festgelegt, dass weitere Laser ergänzt werden sollen. Das langfristige Produktziel ist die Unterstützung aller am Markt befindlichen Laser. Die konkrete Verfügbarkeit wird je Controller und Modell nachgewiesen. Der Blaze X M3 ist das erste Referenzgerät. Die Funktionszusagen [F01–F53](LIGHTBURN_SCOPE.md) und die offenen realen Abnahmen bleiben bestehen.

## Verbindliche Trennung

- Ein Geräteprofil beschreibt Arbeitsraum, Laserkopf, Controller, Transport und erklärte Fähigkeiten. Wattangabe und digitale Leistungsskala sind getrennte Daten; eine Skala wie S-Max gehört zum jeweiligen Treiber. Ein anderer Lasertyp braucht keine GRBL-Einstellungen.
- Ein Controlleradapter setzt einen neutralen Plan in sein Protokoll um. Serielle GRBL-Verbindungen, Netzwerkcontroller und Galvosteuerungen werden nicht durch Umbenennen eines M3-Profils gleichgesetzt.
- Eine Geräteinstanz besitzt einen Anschluss und eigene Kalibrierungen. Anschlussnamen, Zugangsdaten und aktuelle Verbindungsfreigaben gehören nicht in austauschbare Projektdaten.
- Ein Projekt und sein geplanter Auftrag müssen an dieselbe wirksame Gerätekonfiguration gebunden sein. Ein Profilwechsel darf weder Maße noch Leistung stillschweigend umdeuten. Laufende Aufträge werden nicht auf ein anderes Gerät umgestellt.
- Profilbeschreibung, vorhandener Treiber und reale Geräteabnahme sind getrennte Nachweise. Eine Datei mit `verified: true` kann keine aktuelle Verbindung freigeben. Die Oberfläche darf nur tatsächlich verfügbare Funktionen anbieten.

## Arbeitspakete und Nachweise

| Paket | Ergebnis und Abnahme | Stand |
|---|---|---|
| MD1 Profilvertrag | Unveränderlicher, begrenzt validierter Deskriptor; alter M3-Dateiinhalt bleibt erhalten. Aktueller Start prüft den Vertrag vor Erzeugung der Maschine. Reine CLI-Validierung benötigt weder Display noch Geräteverbindung. Synthetische andere Arbeitsräume, Skalen und Controller müssen beschreibbar, aber noch nicht ausführbar sein. | Implementiert; Prüfstand siehe unten |
| MD2 Durchgängige Profildaten | Arbeitsraum und Ausgabeparameter in einem unveränderlichen Projekt-/Job-Snapshot führen. Editor, Vorschau, Koordinatenumrechnung, Framing, Export, Kamera und Planung daraus versorgen. Plan enthält Geräteidentität und Ausgabe-Fingerprint. Projektformat für neue Profile versionieren; alte Projekte ausdrücklich als M3 migrieren, ohne automatisches Skalieren. Globale Koordinatenlimits, Rasterdichte und Schleifenbudgets für größere Flächen prüfen. Unterschiedliche Profile dürfen keinen alten Plan wiederverwenden. | Für den vorhandenen GRBL-XY-Projektpfad umgesetzt; weitere Adapter und Geräteverwaltung bleiben MD3/MD4 |
| MD3 Controlleradapter | Neutrale Geometrie und physische Parameter von GRBL-GCode/S-Werten trennen. Adapter übernimmt Protokoll, Ursprung/Transformation, Einheit, Leistungskodierung und Fähigkeiten. Tatsächliche Controllerkonfiguration vor Ausgabe mit Profil abgleichen. Protokolltests, Fehler-/Abbruchfälle und simulierte Transporte pro Adapter. Fehlender Adapter verhindert Verbindung/Ausgabe. | Neutrale Planung, Ausgabeadapter und gemeinsame Verbindungsschnittstelle umgesetzt; weitere Controller und Protokolle offen |
| MD4 Geräteverwaltung | Explizite Profile und Geräteinstanzen verwalten, aktive Verbindung an Auftrag binden. Wechsel während Betrieb verhindern; nach Wechsel/Verbindungsverlust erneut prüfen. Materialrezepte nach Gerät und Kopf zuordnen, nicht allein nach nomineller Wattzahl. Kamera und Kalibrierung je Gerät führen. Keine automatische Jobfortsetzung nach Wiederverbindung. | Offen |
| MD5 Gerätweise Freigabe | Supportmatrix mit Profilquelle, implementiertem Adapter, Softwaretests und echter Betriebs-/Ergebnisabnahme führen. Protokollzugang, erforderliche SDKs und Nutzungsrechte vor einer konkreten Integration prüfen. Hersteller- und Modellvarianten mit eigenen Grenzen dokumentieren. | Offen; nur bisherige M3-Teilevidenz vorhanden |

## Aktueller Vertrag und seine Grenze

`DeviceProfile` akzeptiert JSON `blazer-device-profile`, Version 1. Die kanonische Arbeitsfläche verwendet Millimeter und links unten als Ursprung; abweichende Maschinenkoordinaten muss später der Adapter umsetzen. Kerndaten sind typisiert; treiberspezifische Einstellungen und Herkunft sind begrenzt strukturierte Daten. Maximal 64 KiB, Tiefe 8 und 512 Datenknoten verhindern unbeschränkt große Konfigurationen. Die Profildaten werden nur als Kopie herausgegeben.

Der Ausgabe-Fingerprint umfasst Kennung, Arbeitsraum, Kopf, Treiberparameter und erklärte Fähigkeiten. Anzeigename und Herkunftsmetadaten verändern ihn nicht. `JobInput` übernimmt die Profildaten als Wert. `JobPlan` hält sein Profil privat und gibt es nur lesbar heraus; vorberechnete Befehle können dadurch nicht separat auf eine andere Konfiguration umgebunden werden. Hintergrundplanung übernimmt denselben Wert und verwirft überholte Berechnungen auch bei einem reinen Profilwechsel.

Planungsgrenzen einschließlich Konturen und Rasterüberlauf verwenden den Arbeitsraum des Auftragsprofils. Der vorhandene GRBL-Kodierer rechnet Prozentwerte mit dessen ganzzahliger S-Max-Skala um; die nominelle Wattzahl verändert diese Umrechnung nicht. Gerundet wird erst nach `Prozent × S-Max / 100`, etwa 50 % bei S-Max 255 zu S128. Inline-Wechsel einschließlich S0 bleiben erhalten. Materialtestpläne übernehmen ebenfalls das Profil und müssen vollständig in dessen Arbeitsraum passen. Ein unbekannter Controller kann nun einen neutralen XY-Bewegungsplan erhalten, aber ohne registrierten Ausgabeadapter keinen Controllerpuffer oder vermeintlichen GRBL-Auftrag. Schnittstellen und Bindung beschreibt [Bewegungsplan und Controlleradapter](CONTROLLER_ADAPTERS.md). Das bisherige Planungskoordinatenlimit von 2000 mm bleibt ausdrücklich bestehen; größere gültige Beschreibungen sind damit noch nicht planbar.

Die Verbindung hält ihr eigenes unveränderliches Profil. Vor dem ersten Auftragsbefehl müssen dessen Fingerprint und der des Plans übereinstimmen, auch bei einem vorberechneten Befehlspuffer. Baudrate, erwartete Controllermaße/S-Max, physische Grenzen und Fokus-S-Wert stammen nun aus diesem Profil. Alle bestehenden Status-, Laser-Aus-, G54- und Fehlerprüfungen gelten weiter. Die vorläufige Laufzeitgrenze verhindert weiterhin das Öffnen einer anderen Konfiguration.

Der bestehende M3-Start liest weiterhin dieselbe eingebettete Datei und prüft ihren bisherigen Ausgabevertrag. Abweichende, bekannte GRBL-XY-Projektprofile werden nun mit **Projektformat 11** gespeichert. Dieses enthält auch alle Hilfslinien-/Hilfsformfelder. Für das unveränderte M3 bleibt Format 8, 9 oder 10 erhalten. Ältere Dateien werden unabhängig vom gerade geöffneten Profil ausdrücklich als M3 gelesen, ohne Skalierung der Geometrie. Vorlagen, Wiederherstellung und Verlauf übernehmen das gespeicherte Profil. Projektimport mit abweichendem Ausgabe-Fingerprint wird abgewiesen; die Zwischenablage überträgt Objekte und Bearbeitungswerte, niemals das Zielgerät.

Editorfläche, Lineale, Fangziele, Koordinatenumrechnung, Platzierung, dünne Pfadkästen, Rahmenfahrt, SVG-Ausgabe, Simulator und Foto-Registrierung verwenden den Dokumentarbeitsraum. Die Linealbeschriftung hält unabhängig von der Bettgröße einen Mindestabstand; Raster und Schleifen bleiben durch maximal 2000 × 2000 mm begrenzt. Ein Profilwechsel verändert weder Objektmaße noch Bearbeitungswerte, verwirft aber die bisherige Materialzuordnung. Bei verändertem Ausgabe-Fingerprint wird eine Foto-Zuordnung ungültig und muss neu kalibriert werden. Ein bloß anderer Anzeigename verwirft sie nicht.

Ein Wechsel des Ausgabeprofils ist bei verbundener Maschine, laufender/pausierter Simulation, Fokusausgabe oder CSV-Export gesperrt. Auch Öffnen, Wiederherstellung, Vorlagen und Verlauf prüfen dies vor Änderung von Inhalt, Dateiidentität oder Historie. Nach einem zulässigen Wechsel erhält der Simulator seine neue Fläche; ein neuer, noch nicht geöffneter Adapter übernimmt das Profil. Veraltete Rahmenziele werden anhand ihres Fingerprints abgewiesen. Die eigentliche Geräteprofilverwaltung in der Oberfläche folgt in MD4; aktuell können solche Profile in Projektdateien und über die interne C++-API vorliegen.

Projektprofile akzeptieren derzeit ausschließlich das implementierte Schema für serielle GRBL-XY-Ausgabe mit Ursprung links unten, ohne Z-Steuerung. Bekannte numerische/Bool-Parameter werden geprüft; beliebige Treibereinstellungen, Anschlussnamen und Zugangsdaten werden nicht als Projektdaten übernommen. Herkunftsmetadaten einschließlich alter Anschlussangaben werden entfernt. Andere Controller benötigen in MD3 einen eigenen geprüften Projektparametervertrag. Die allgemeine reine Deskriptorvalidierung bleibt davon getrennt und kann unbekannte Controller beschreiben.

**Abweichende Projekte sind offline bearbeitbar, simulierbar und mit ihrem GRBL-Vertrag exportierbar; zusätzliche reale Geräte sind weiterhin nicht freigegeben.** Die Laufzeitgrenze lässt ausschließlich den bisherigen M3-Ausgabevertrag zu. Eine neue Beschreibung, größere Fläche oder andere Skala allein hebt sie nicht auf.

Strukturprüfung ohne GUI oder Maschinenobjekt:

```sh
blazer-studio --validate-device-profile profiles/blaze-x-m3.json
```

Exitcode 0 bedeutet gültige Beschreibung, auch bei einem noch nicht unterstützten Treiber. Das JSON-Feld `runtimeSupported` benennt gesondert die aktuelle Laufzeitverwendbarkeit. Es bedeutet keine Live-Gerätefreigabe. Ungültige Dateien liefern Exitcode 2. Treibereinstellungen und Herkunftsinhalte werden nicht ausgegeben.

Die automatisierten Fälle prüfen unveränderte alte M3-Oberflächendaten, kanonischen Rundlauf, synthetische GRBL-/Galvo-/DSP-Beschreibungen, Fingerprintänderungen, Datenlimits und den CLI-Start ohne Display mit ungültigem Qt-GUI-Backend. Die synthetischen Beispiele sind keine Herstellerfreigaben. Reale Laserversuche bleiben [Geräte- und Materialabnahme](HARDWARE_ACCEPTANCE.md).
