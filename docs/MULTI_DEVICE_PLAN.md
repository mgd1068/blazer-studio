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
| MD2 Durchgängige Profildaten | Arbeitsraum und Ausgabeparameter in einem unveränderlichen Projekt-/Job-Snapshot führen. Editor, Vorschau, Koordinatenumrechnung, Framing, Export, Kamera und Planung daraus versorgen. Plan enthält Geräteidentität und Ausgabe-Fingerprint. Projektformat für neue Profile versionieren; alte Projekte ausdrücklich als M3 migrieren, ohne automatisches Skalieren. Globale Koordinatenlimits, Rasterdichte und Schleifenbudgets für größere Flächen prüfen. Unterschiedliche Profile dürfen keinen alten Plan wiederverwenden. | Offen; nächstes Architekturpaket |
| MD3 Controlleradapter | Neutrale Geometrie und physische Parameter von GRBL-GCode/S-Werten trennen. Adapter übernimmt Protokoll, Ursprung/Transformation, Einheit, Leistungskodierung und Fähigkeiten. Tatsächliche Controllerkonfiguration vor Ausgabe mit Profil abgleichen. Protokolltests, Fehler-/Abbruchfälle und simulierte Transporte pro Adapter. Fehlender Adapter verhindert Verbindung/Ausgabe. | Offen |
| MD4 Geräteverwaltung | Explizite Profile und Geräteinstanzen verwalten, aktive Verbindung an Auftrag binden. Wechsel während Betrieb verhindern; nach Wechsel/Verbindungsverlust erneut prüfen. Materialrezepte nach Gerät und Kopf zuordnen, nicht allein nach nomineller Wattzahl. Kamera und Kalibrierung je Gerät führen. Keine automatische Jobfortsetzung nach Wiederverbindung. | Offen |
| MD5 Gerätweise Freigabe | Supportmatrix mit Profilquelle, implementiertem Adapter, Softwaretests und echter Betriebs-/Ergebnisabnahme führen. Protokollzugang, erforderliche SDKs und Nutzungsrechte vor einer konkreten Integration prüfen. Hersteller- und Modellvarianten mit eigenen Grenzen dokumentieren. | Offen; nur bisherige M3-Teilevidenz vorhanden |

## Aktueller Vertrag und seine Grenze

`DeviceProfile` akzeptiert JSON `blazer-device-profile`, Version 1. Die kanonische Arbeitsfläche verwendet Millimeter und links unten als Ursprung; abweichende Maschinenkoordinaten muss später der Adapter umsetzen. Kerndaten sind typisiert; treiberspezifische Einstellungen und Herkunft sind begrenzt strukturierte Daten. Maximal 64 KiB, Tiefe 8 und 512 Datenknoten verhindern unbeschränkt große Konfigurationen. Die Profildaten werden nur als Kopie herausgegeben.

Der Ausgabe-Fingerprint umfasst Kennung, Arbeitsraum, Kopf, Treiberparameter und erklärte Fähigkeiten. Anzeigename und Herkunftsmetadaten verändern ihn nicht. Diese Kennung ist eine Grundlage für MD2, noch keine bereits vorhandene Bindung aller Jobs.

Der bestehende M3-Start liest weiterhin dieselbe eingebettete Datei. Nach der Konvertierung prüft eine ausdrücklich vorläufige Laufzeitgrenze die vollständige Übereinstimmung mit dem bisherigen M3-Vertrag. **Andere Profile sind derzeit Beschreibungen, keine auswählbaren oder ansteuerbaren Geräte.** Die vorhandenen Konstanten für 200 × 150 mm, S-Max 1000 und weitere M3-Annahmen werden erst in MD2/MD3 durchgängig abgelöst. Die temporäre Grenze darf erst fallen, wenn diese Verbraucher tatsächlich dieselben Profildaten verwenden.

Strukturprüfung ohne GUI oder Maschinenobjekt:

```sh
blazer-studio --validate-device-profile profiles/blaze-x-m3.json
```

Exitcode 0 bedeutet gültige Beschreibung, auch bei einem noch nicht unterstützten Treiber. Das JSON-Feld `runtimeSupported` benennt gesondert die aktuelle Laufzeitverwendbarkeit. Es bedeutet keine Live-Gerätefreigabe. Ungültige Dateien liefern Exitcode 2. Treibereinstellungen und Herkunftsinhalte werden nicht ausgegeben.

Die automatisierten Fälle prüfen unveränderte alte M3-Oberflächendaten, kanonischen Rundlauf, synthetische GRBL-/Galvo-/DSP-Beschreibungen, Fingerprintänderungen, Datenlimits und den CLI-Start ohne Display mit ungültigem Qt-GUI-Backend. Die synthetischen Beispiele sind keine Herstellerfreigaben. Reale Laserversuche bleiben [Geräte- und Materialabnahme](HARDWARE_ACCEPTANCE.md).
