# Bewegungsplan und Controlleradapter

Der [Mehrgeräteplan](MULTI_DEVICE_PLAN.md) trennt Beschreibung, Geometrie, Ausgabe und reale Verbindung. Der erste Teil von MD3 ist umgesetzt: Der vorhandene GRBL-Kodierer und sein Projektparameterschema liegen hinter einer Adapter-Schnittstelle. Andere Controller werden dadurch noch nicht unterstützt. Die reale Sitzungssteuerung verwendet weiterhin `GrblSession`; deren allgemeine Schnittstelle und weitere Protokolle bleiben offen.

## Getrennte Ergebnisse

| Ergebnis | Bedeutung |
|---|---|
| Gültiger `DeviceProfile`-Deskriptor | Strukturell gültige Gerätebeschreibung; sagt nichts über einen vorhandenen Adapter oder ein angeschlossenes Gerät aus. |
| `JobPlan::valid()` | Nicht abgebrochener, berechneter XY-Plan in physischen Millimetern, mm/min und Prozentwerten; enthält keine Controllerbefehle. |
| `outputSupported` / `outputError` in der Auftragsübersicht | Der registrierte Ausgabeadapter kann den Profilvertrag kodieren; keine Aussage über aktuelle Verbindung oder über ein beliebig verändertes manuelles Planobjekt. |
| Gültiges `ControllerProgram` | Vollständig erzeugte Ausgabe mit Treiber, Transport, Medienformat, Profil-Fingerprint und Bewegungs-Fingerprint. Dies ist weiterhin nur Datenmaterial. |
| Gerätebereitschaft | Eigener Verbindungszustand mit aktuellen Controller-/Positions-/Laser-Aus-Meldungen. Aktuell bleibt ausschließlich der bisherige M3-Ausgabevertrag für reale Verbindungen verfügbar. |

Die neutrale Planung verlangt deklarierte XY-Bewegung und die bestehenden Geometrie-/Arbeitsraum-/Ressourcenlimits. Eine fehlende dynamische Leistungsfähigkeit oder ein unbekannter Controller verhindert nicht allein die XY-Geometrieberechnung. Vorschub und Prozentwerte müssen endlich und innerhalb der bestehenden Modellgrenzen sein. Ob und wie ein solcher Plan ausführbar ist, entscheidet anschließend der passende Ausgabeadapter.

## Registrierte Ausgabe

`ControllerAdapters::find()` wählt statisch nach genauer Treiberkennung und Transport. Es gibt derzeit ausschließlich `grbl-serial` über `serial`. Unbekannte Kennungen oder Transporte erhalten keinen Ersatzadapter. Diese Auswahl lädt keine Erweiterung, öffnet keine Verbindung und sucht keine Anschlüsse.

`ControllerAdapter` besitzt getrennte Prüfungen für Ausgabeparameter, das bekannte Projektparameterschema und die eigentliche Kodierung. Das GRBL-Schema prüft weiterhin seine bekannten numerischen/Bool-Felder und entfernt Herkunfts-/Anschlussdaten aus Projekten. Der Kodierer verlangt insbesondere eine ganzzahlige S-Max-Skala, XY/dynamische Leistung und den implementierten Achsen-/Ursprungsvertrag. Wattangaben sind keine digitalen Leistungsmultiplikatoren.

`grblencoder.cpp` erzeugt dieselben M5/G21/G90/G54/M4-/Bewegungsblöcke wie zuvor. Inline-Leistungswechsel einschließlich S0, Neustart nach Eilgängen und abschließendes Laser-Aus bleiben erhalten. Die Kodierung validiert alle Endpunkte, Vorschübe und Prozentwerte vor der Erstellung einer Ausgabe. Auch manuell konstruierte ungültige Pläne dürfen keinen Teilpuffer liefern.

## Gebundener und unveränderlicher Ausgabepuffer

`ControllerProgram` stellt seine Metadaten und Nutzdaten nur lesbar bereit. Der Profil-Fingerprint beschreibt den Ausgabevertrag; der Bewegungs-Fingerprint umfasst alle aktuellen Bewegungsfelder in festgelegter binärer Reihenfolge. Die gemeinsame Prüfung verhindert die Übernahme eines Puffers für einen anderen Plan. Eine Ausgabe bleibt auf 64 MiB und ein Plan auf die bisherigen 500000 Segmente begrenzt. Abbruch oder Kodierfehler liefern keinen Teilinhalt.

Bewegungen sind über `JobMoves` als Werte bearbeitbar. Element-, Iterator- und Datenzugriffe sind lesend. Eine Änderung erfolgt durch Kopie und Ersetzen, etwa:

```cpp
auto move = plan.moves[index];
move.power = 20;
plan.moves.replace(index, move);
```

Ein erhaltener schreibbarer Elementverweis darf die Zwischenspeicherung nicht umgehen. Deshalb gibt die Sammlung keine solchen Verweise heraus. Der Ausgabepuffer hält eine gemeinsame lesende Qt-Wertkopie; normale Änderungen lösen diese Verbindung und erzwingen die erneute Bindung über den Bewegungs-Fingerprint. Unveränderte veröffentlichte Pläne können ihren bereits geprüften Puffer ohne erneute Berechnung abrufen. Erzeugung und erste Prüfsumme erfolgen bei Hintergrundplanung im Worker.

## Export und Übertragung

`JobPlan::gcode()` bleibt eine Kompatibilitätsansicht auf echte `text/x-gcode`-Ausgabe. Andere Medienformate werden nicht als Text oder GCode interpretiert. Das Datenformat kann damit später auch binäre Controllerprogramme tragen, ohne ihnen versehentlich GRBL-Zeilen zuzuordnen.

Einzel-, Materialtest- und CSV-GCode-Export prüfen den vollständigen Ausgabepuffer und sein Medienformat vor dem Schreiben. Fehlender Adapter erzeugt keine Datei, die lediglich einen Zeilenumbruch enthält. Bestehende Dateien und die atomare CSV-Veröffentlichung bleiben geschützt. Vor der Übertragung prüft die GRBL-Sitzung weiterhin Bereitschaft, Profil und physische G54-Grenzen sowie zusätzlich den Typ des erzeugten Programms. Ein gültiger Datenpuffer allein startet keinen Laser.

## Weitere Arbeit in MD3

Die konkrete Sitzungssteuerung für Verbindung, Abfragen, Status, Jog, Fokus, Rahmenfahrt und Fehlerbehandlung muss noch eine gemeinsame Controller-Verbindungsschnittstelle erhalten. Ein weiterer Treiber benötigt sein eigenes Parameterschema, Medienformat, Protokoll und eigene Tests. Auch Ursprung, Achsen, Leistungseinheiten und Spezialfunktionen dürfen nicht durch Übernahme des M3-Verhaltens vorgetäuscht werden. Geräteverwaltung und Material-/Kalibrierzuordnung je Instanz folgen in MD4; reale Modellfreigaben bleiben MD5 und die tatsächlichen Geräteabnahmen.
