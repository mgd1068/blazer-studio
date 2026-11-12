# Prüfung des Entwurfs 0.2

Stand 09.11.2026; Ubuntu 26.04, GCC 15, Qt 6.10.2. Prüfung des vorhandenen Prototyps, keine Abnahme des gesamten LightBurn-Zielkatalogs.

| Bereich | Nachweis |
|---|---|
| Materialbibliothek | Auswahl übernimmt Parameter und Ausgabeschalter; Bearbeiten, Duplizieren, Löschen und erneutes Laden; ungültige Profile und Schreibfehler verändern den alten Stand nicht; beschädigte Datei wird erhalten |
| Projekt und Profil | Angewendeter Profilstand bleibt unabhängig von späteren Bibliotheksänderungen; manuelle Parameterabweichung wird erkannt; Übernahme und Änderungen mit Undo |
| Editor | Formen, Eigenschaften, Kopieren/Löschen, Ziehen als ein Undo-Schritt; Änderungen ohne Wirkung erzeugen keinen Undo-Schritt; Rückkehr zum gespeicherten Zustand hebt die Änderungsmarkierung auf; Qt-Modelltester prüft Änderungsbenachrichtigungen |
| Projektdateien | Speichern/Öffnen, Version-1-Koordinatenmigration; ungültige Geometrie, falsche Datentypen, doppelte IDs und falscher Ursprung werden ohne Verlust des offenen Projekts abgelehnt |
| SVG | Physische Maße, inch/cm-Umrechnung, Speicherung; externe Referenzen, Skripte, DTD/Entities und uneindeutige Prozentmaße abgelehnt; Renderer wird zwischen Objektkopien und Undo-Ständen geteilt |
| Simulator | Fokus-/Jobkonflikte, drei Sekunden Fokuslimit, Abbruch und Wiederverbindung; Bewegungsgrenzen; Rahmenpunkte und Abschluss einer echten Rechteckumrundung ohne Emission |
| Oberfläche | Maus/Tastatur: Material auswählen, Namen bearbeiten, Dezimalkomma speichern, Bibliothek erneut laden, ungültige Eingabe zurückweisen, Abbrechen und erneut öffnen; Formen/Undo, Bereichswechsel, Fokus-Haltebedienung und Abschaltung beim Bereichswechsel |
| Herstellerdaten | Vier Python-Testfälle prüfen 64 Quellzeilen, UTF-8, Einheiten, fehlende Werte, Quellenänderungen und atomare Ausgabe. Erneute Extraktion der gespeicherten Originalseite erzeugt bytegleiches JSON |

CTest enthält drei erfolgreiche Testprogramme: 15 Kernfälle, einen kombinierten Qt-Quick-Bedienfall und vier Python-Katalogfälle. Zusätzlich bestehen diese Programme mit AddressSanitizer und UndefinedBehaviorSanitizer; `ASAN_OPTIONS=detect_leaks=0` deaktiviert die Leak-Prüfung der Qt-Laufzeit. Dies ist kein Nachweis von Leckfreiheit oder vollständiger Testabdeckung.

Visuell geprüft: Hauptfenster 1480 × 920 hell, 1180 × 760 dunkel und Materialeditor. Die Screenshot-Läufe erzeugten keine QML-Laufzeitwarnungen. Das lokale Qt-Paket meldet beim CMake-Konfigurieren Warnungen über fehlende statische QML-Plugin-Ziele; der dynamische Build und die Laufzeittests funktionieren.

## Überarbeitung

- Objekt-, Achsen- und Simulatorpositionen verwenden einheitlich Ursprung unten links.
- Rahmenfahrt folgt den aktiven Objektgrenzen und lehnt Motive außerhalb des Arbeitsraums ab.
- Typisierte, gemeinsam genutzte Undo-Stände ersetzen wiederholtes JSON-/SVG-Parsen; SVG-Renderer und Textpfade werden wiederverwendet.
- Qt-Modellbenachrichtigungen umschließen Einfügen/Entfernen korrekt; ungültige Projektdateien werden vor jeder Zustandsänderung vollständig geprüft.
- Materialeditor bewahrt ungültige Eingaben zur Korrektur; Abbrechen hinterlässt beim erneuten Öffnen keine alten Entwurfswerte.

## Grenzen

Kein serieller Treiber, kein GCode, keine echte Werkzeugweg- oder Füllberechnung. Keine reale Laseremission oder Materialprüfung. Die Vorschau zeigt Layout und Verbindungen; der Demoablauf ist ein unabhängiger Simulator. Wayland/X11 auf realem Display, HiDPI, alle Importvarianten und echte Controllerreaktionen sind noch nicht umfassend abgenommen. Die aktive ältere App-Instanz wurde zur Erhaltung möglicher ungespeicherter Arbeit nicht beendet.

Wiederholen:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DBLAZER_SANITIZERS=ON
cmake --build build-sanitize --parallel 4
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-sanitize --output-on-failure
```

## Korrektur: Schließen nach Verwerfen

Der ergänzte Bedienungstest reproduzierte zunächst den Fehler: Nach „Verwerfen“ blieb das Fenster offen. `Qt.quit()` führte erneut durch den Schließen-Handler, der wegen des weiterhin geänderten Projekts wieder nachfragte. Die bestätigte Aktion schließt jetzt das Fenster mit einer nur während dieses Schließvorgangs gültigen Freigabe. Der Dialog wird vorher geschlossen; Abbrechen entfernt die vorgemerkte Aktion. Projektänderungen werden dabei nicht fälschlich als gespeichert markiert.

Regressionstest: geändertes Projekt schließen, Fokusfreigabe aufgehoben, Abbrechen erhält Fenster und Änderungen, erneutes Schließen und Verwerfen beendet das Fenster ohne erneute Abfrage. Alle drei CTest-Programme bestehen nach der Korrektur.

## Korrektur: abgeschnittene Materialauswahl (10.11.2026)

Die Standard-ComboBox begrenzte die Liste auf die Breite der Seitenleiste. Ein eigenes Popup im Fenster-Overlay öffnet jetzt bis zu 560 Pixel breit und hält Abstand zum Fensterrand. Einträge und geschlossenes Auswahlfeld brechen lange Namen um; Farben berücksichtigen das helle und dunkle Design. Der Bedienungstest prüft sichtbare Textobjekte ohne Abschneiden, Popupbreite und Fenstergrenzen sowie die gewählte mehrzeilige Anzeige. Nach Fenstergrößenänderungen wartet der Maushelfer auf die Layoutaktualisierung. Qt-Quick-Bedienungstest bestanden; Screenshots in 1480 × 920 hell und 1180 × 760 dunkel visuell geprüft.

## Vollständige auswählbare Herstellerbibliothek (10.11.2026)

Herstellerseite erneut per HTTPS gelesen und mit dem bisherigen Datenbestand verglichen: fünf Tabellen und 64 Zeilen unverändert. Alle Zeilen erzeugen nun bearbeitbare Profile direkt aus dem eingebetteten Quellenkatalog, zusätzlich zum eigenen Beispiel. Es gibt keine zweite manuell gepflegte Parameterliste. Modell, Modul, Bearbeitung und Originalparameter bleiben nachvollziehbar. Gravur-Durchgänge fehlen in der Quelle; ein eigener anfänglicher Arbeitswert von 1 wird sichtbar gekennzeichnet und verändert den ursprünglichen Nullwert nicht. Die andere Bearbeitung ist jeweils deaktiviert.

Bestehende Bibliotheken erhalten fehlende Profile einmalig. IDs, eigene Profile und Änderungen bleiben erhalten; die Originaldatei wird vorher gesichert. Ein gespeicherter Katalogversionsstand verhindert, dass bewusst gelöschte Vorgaben bei jedem Start wieder erscheinen.

Neue Kernfälle prüfen jede einzelne der 64 Quellzeilen gegen genau ein Profil einschließlich Leistung, Geschwindigkeit, Durchgängen/Intervall, Modul und aktivierter Bearbeitung; alle Profile lassen sich ins Projekt übernehmen. Migration, Erhalt geänderter Werte, Sicherung, Löschen und erneutes Laden werden separat geprüft. Der UI-Test wählt auch den letzten 1-W-Eintrag per Tastatur und kehrt zum ursprünglichen Schnittprofil zurück. Alle drei CTest-Programme bestanden (jetzt 17 Kernfälle, kombinierter UI-Bedienfall, vier Python-Katalogfälle). Helle und kompakte dunkle Materiallisten visuell geprüft. Keine Hardwareaktion.

## Toolkopf-Auswahl und Materialfilter (10.11.2026)

Oberhalb von Material steht eine eigene Toolkopf-Auswahl mit dem eingerichteten 5-W-Modul als Standard. Die Materialliste filtert nach der im Profil hinterlegten Modulleistung; eigene Modulleistungen ergänzen die Auswahl. Nur die Liste ändert sich beim Kopfwechsel, keine Projektparameter. Die gefilterte Zeile wird auf den ursprünglichen Bibliothekseintrag abgebildet, damit Auswahl, Bearbeiten, Duplizieren und Löschen weiterhin das richtige Profil betreffen. Bei einem Modellwechsel wird der von ComboBox vorübergehend gesetzte Standardindex zurückgesetzt, sodass kein fremdes Material als ausgewählt erscheint.

Kernfall prüft 27/25/13 Profile für 5/10/1 W, jeden enthaltenen Eintrag und ein eigenes 20-W-Profil einschließlich Änderung auf 10 W und Löschen. UI-Test prüft Standardkopf, Wechsel 5→10→1→5, Erhalt der Projektwerte beim Filtern und Objektbearbeiten, Materialübernahme, Rückzuordnung auf das ursprüngliche Profil und stabile Auswahl nach dem Speichern. Alle drei Testprogramme bestanden (18 Kernfälle, kombinierter UI-Test, vier Katalogfälle). Die UI-Prüfung bestätigt auch den unveränderten Projektkopf nach einem Filterwechsel und die korrekte Auswahl nach Speichern. Helle und kompakte dunkle Darstellung visuell geprüft. Keine Hardwarekommunikation.

## Version 0.3: Objektauswahl, numerische Bearbeitung und Gruppen (10.11.2026)

Reproduzierter Auswahlfehler: Der Klick in der Objektliste rief `project.select(...)` auf, aber die C++-Methode war nicht als Q_INVOKABLE freigegeben. Der UI-Test meldete „Property select ... is not a function“ und eine unveränderte Auswahl. Die Methode ist nun verfügbar. Vor einem Auswahlwechsel wird der Fokus aus dem bisherigen Editor genommen, damit noch nicht bestätigte Namens-/Maßeingaben am richtigen Objekt bleiben.

Objektzeilen sind einzeln anwählbar und haben eine eigene Gravieren-/Schneiden-Auswahl. Mehrfachauswahl und flache, benannte Gruppen verwenden stabile Mitgliedschaften. Translation und Skalierung wirken atomar auf die Auswahl und erhalten relative Anordnung; Kopien erhalten neue Objekt- und Gruppen-IDs. Umgruppieren teilt bestehende Restgruppen nicht über mehrere Listenabschnitte auf. Gruppennamen, Mitgliedschaften und Mehrfachauswahl werden in Format 3 gespeichert; Formate 1/2 bleiben lesbar. Ungültige Gruppenmetadaten oder Auswahlindizes werden vor Änderung des offenen Projekts abgelehnt.

Liste und Zeichenfläche markieren alle ausgewählten Mitglieder. Die Liste scrollt zur Auswahl, die Zeichenfläche verschiebt/verkleinert bei Bedarf die Ansicht. Ausgewählte ausgeblendete Objekte erhalten nur im Editor eine schwache Darstellung. Numerische Bearbeitung umfasst X/Y, Breite/Höhe und eine proportionale Prozentskalierung relativ zur aktuellen Größe. Außerhalb des Arbeitsraums platzierte Objekte bleiben bearbeitbar und sichtbar; die Prüfung der Ausgabegrenzen bleibt erhalten.

Nachweise: **22 Kernfälle**, ein erweiterter Qt-Quick-Bedienfall und vier Katalogfälle bestehen, zusätzlich mit AddressSanitizer/UndefinedBehaviorSanitizer (`detect_leaks=0`, keine Aussage über Leckfreiheit). Kernfälle prüfen Gruppenverschiebung/-skalierung, Einzelzuweisung, unabhängige Kopien, Löschen/Auflösen, Undo/Redo, Speichern/Laden, defekte Gruppen, Umgruppieren und Sichtbarkeit außerhalb des Arbeitsraums. Der UI-Test klickt echte Objektzeilen, bearbeitet Namen und Zahlen mit Dezimalkomma, wechselt Bearbeitungsarten, gruppiert über Häkchen, positioniert/skaliert/benennt die Gruppe und zieht, kopiert und löscht sie. Die Verschiebung jedes Mitglieds ist gleich. Keine QML-Laufzeitwarnungen in den geprüften Test-/Screenshot-Logs. Helle Hauptansicht, kompakte dunkle Ansicht und gemischte Objektgruppe visuell geprüft.

Bedienung: [Objekte und Gruppen](OBJECTS_GROUPS.md). Gruppierung ist flach; SVG-Grafiken werden dadurch nicht in Einzelelemente zerlegt. Keine Hardwarekommunikation oder echte Laserpfadberechnung hinzugefügt.

## Version 0.4: durchgängiger Job und reale GRBL-Verbindung (10.11.2026)

Dieser Abschnitt ersetzt die früheren Grenzen zu fehlenden Werkzeugwegen, GCode und serieller Kommunikation. Rechteck-/Ellipsen-/Text-/SVG-Konturen, Füllpfade mit Innenräumen und weißen SVG-Aussparungen, Vorschau, GCode-Export, Schwarzweiß-Bildimport und bearbeitbare Materialtestmatrix sind umgesetzt. Projektformat 4 erhält Objektparameter; alte Formate bleiben lesbar. Ein gültiger Job wird für die Übertragung eingefroren. Nicht unterstützte aktive Geometrie oder Arbeitsraumüberschreitungen verhindern die gesamte Ausgabe.

**Automatisiert:** Alle fünf CTest-Programme bestehen: 22 Kernfälle, fünf GRBL-Fälle, sieben Jobfälle, ein kombinierter Qt-Quick-Bedienfall und vier Python-Katalogfälle. Dieselben Programme bestehen mit AddressSanitizer/UndefinedBehaviorSanitizer (`detect_leaks=0`; keine Aussage über Leckfreiheit). Die GRBL-Prüfung verwendet ein Pseudoterminal und prüft Konfigurationssperren, echte Qt-Metaobjekt-Weiterleitung an den Geräteadapter, eingefrorenen Job, Antwortquittierung, Warten auf neues Idle, Pause/Stop/Neuinitialisierung, Fokusbefehlsfolge und Alarm. Keine reale Laseremission durch diese Tests.

**Am vorhandenen M3:** Firmware Grbl-ESP32 1.3a.20211103, 115200 Baud; S-Max 1000, Lasermodus an, Millimeter und 200 × 150 mm ausgelesen. Referenzfahrt, Fahrt nach X5/Y5 und Rückkehr nach X0/Y0 wurden vom eigenen Treiber bestätigt. Ein vollständiger berechneter 5-mm-Quadratjob bei X10/Y10 wurde mit jeder M4-Freigabe durch M5/S0 ersetzt übertragen und abgearbeitet; anschließende Rückkehr X0/Y0 und Idle/S0 bestätigt. Kein Laser eingeschaltet, kein Materialtest, kein Fokus-Laserpuls. Positionsnachweis ausschließlich durch Controllerstatus, keine unabhängige Maßmessung. Der Nutzer bestätigte geschlossenes Gehäuse und ausgeschalteten Gehäuselüfter. Unbekannte Lüfter-Firmwareparameter wurden nicht geändert.

**Visuell:** Pfadvorschau und vollständige 4×4-Materialmatrix in 1480 × 920 hell sowie Geräteansicht in 1180 × 760 dunkel geprüft. Zusammenfassung außerhalb der Zeichenfläche verdeckt keine Testfelder. Die geprüften Screenshot- und UI-Logs enthalten keine QML-Laufzeitwarnungen. Nach den letzten Darstellungsänderungen UI-Test erneut normal und mit Sanitizern bestanden. Die bestehende ältere Benutzerinstanz wurde nicht beendet.

**Weiter offen:** reale Emissions-/Materialabnahme, Fokus-Puls am Gerät, komplexere Importformate und SVG-Sonderfälle, Beschleunigungs-/Kommunikationsmodell für Laufzeiten und der übrige LightBurn-Zielkatalog. Dies ist keine Produktionsfreigabe oder vollständige LightBurn-Gleichwertigkeit. Details und reproduzierbare Geräteprüfung: [GRBL-Workflow](GRBL_WORKFLOW.md).

Lizenz: eigener Code unter Blazer Studio Free Redistribution License 1.0; Nutzung auch gewerblich erlaubt, Softwareverkauf verboten und Weitergabe kostenlos, daher Source Available statt OSI Open Source. Qt-Lizenztexte und Drittanbieterhinweise liegen bei. Rohe Geräteausgaben mit werkseitigen Netzwerkkennungen bleiben außerhalb des öffentlichen Repositorys.

## Version 0.5: geschichtete Reliefgravur (10.11.2026)

Ein eigener vierter Arbeitsbereich **Relief** importiert Graustufen-Tiefenkarten ohne Dithering, erzeugt 1–256 Schichten nach Schwärzung/Gamma und zeigt eine relative 3D-Ansicht oder die aktive Schichtmaske. Vorschau und Pfadberechnung benutzen dieselbe Rasterabtastung; die einzelnen XY-Durchgänge laufen vor normalen Vektoroperationen und Schnitten. Im GCode sind Schichten kommentiert; keine Z-Kommandos. Alle vorhandenen Ausgabegrenzen gelten weiter. Zusätzliche Raster-, Pixel- und Aufwandsgrenzen verhindern übergroße Berechnungen und Speicherbelegung. Projektformat5 liest weiterhin1–4.

Sieben neue Relief-Testfälle prüfen Quantisierung, Gamma/Umkehr/Transparenz, tatsächliche Besuche und Weglängen pro Schicht, Bildorientierung, Gravurzwang und Schnittreihenfolge, Speichern/Laden, gemeinsame unveränderliche Bilddaten, unabhängige Kopien, Undo, Kalibrierungsrücksetzung, defekte Dateien, dekodiertes Bildbudget, aufwändige/außerhalb platzierte Motive sowie weiße Bilder ohne Emission. Beim Roundtrip gefundenen QVariant-Typunterschied zwischen QML-/JSON-Zahlen behoben; Test bleibt als Regression erhalten. Der erweiterte UI-Test bedient den vierten Bereich, Umkehr, Gamma mit Dezimalkomma und Schichtregler und prüft die kalibrierte Tiefenanzeige.

Sechs CTest-Programme: 22 Kernfälle, fünf GRBL-Fälle, sieben Relief-Fälle, sieben Jobfälle, ein kombinierter UI-Fall und vier Python-Katalogfälle. Normal und mit ASan/UBSan geprüft (`detect_leaks=0`, keine Aussage über Leckfreiheit). Helle Reliefansicht1480×920, dunkle1180×760 und Pfadvorschau geprüft; Details unter [Relief](RELIEF.md). Ein synthetisches Tiefenbild und ein vollständiger16-Schichten-GCode wurden zum Nachvollziehen erzeugt.

Keine Hardwarebefehle in dieser Erweiterung. Der eingegebene Abtrag je Durchgang ist ausschließlich eine freiwillige Messwertangabe; ohne sie gibt es keine Millimetertiefe. Reale M3-Reliefgravur auf Kieseln, Krümmung/Fokus und Materialabtrag sind noch nicht erprobt. Ein Foto wird nicht automatisch in eine geometrisch richtige Tiefenkarte umgerechnet. Die vorhandene Benutzerinstanz wurde nicht beendet.

## Ausbaupaket 0.6 (10.11.2026)

Sieben CTest-Programme bestehen vollständig, normal und mit AddressSanitizer/UndefinedBehaviorSanitizer (`detect_leaks=0`; keine Zusage von Leckfreiheit). Funktionsfälle: 22 Kern-, sechs GRBL-, zwölf Ausbau-, sieben Relief-, sieben Job-, ein kombinierter UI- und vier Python-Katalogfälle. Nach den letzten Änderungen an Oberfläche und Qt-Versionskompatibilität wurden die betroffenen Ausbau-/UI-Programme erneut normal und mit Sanitizern erfolgreich geprüft. Keine Compilerwarnungen oder QML-Laufzeitwarnungen in den geprüften letzten Build-/Screenshot-/Testlogs.

Die neue Ausbauprüfung deckt freie Operationen/Mehrfachzuweisung und Verwaltung, aktive Arbeitsblätter, Drehen/Spiegeln, Knoten, Boolesche Werkzeuge/Ausrichten, Wiederherstellung/Backup, Scanwinkel/Kreuzschraffur/Überlauf, Haltestege/Schnittfuge/Arbeitsraum, Bildkorrekturen/Schärfung/Graustufen/Trace, 16-Bit-Unterschiede, Relief-Reinigung, QR, Kopien, gruppenerhaltendes Nesting über mehrere Blätter, CSV mit zitierter Eingabe und Überschreibschutz, Foto-Registrierung und beschädigte Projekte ab. Importprüfungen umfassen ASCII-DXF, ein synthetisches LightBurn-Gruppen-/Pfadbeispiel, SVG-Clip sowie einen echten Inkscape-Konvertierungsaufruf für ein erzeugtes PDF und OBJ-Rasterung. Dies ist keine Abnahme sämtlicher Varianten dieser Formate.

Zwei beim Ausbau gefundene Fehler wurden als Regression festgehalten: Nesting darf QR-Ruhezonen nicht verlieren und muss vollständige Gruppen gemeinsam verschieben; SVG-Clips dürfen bei fehlender Rendererunterstützung nicht stillschweigend größere Gravurflächen erzeugen. Unterstützte globale Clips werden ausgewertet, andere ausdrücklich abgelehnt. Die Prüfung einer halben Rechteckfläche bestätigt die tatsächliche Weglänge.

Der erweiterte UI-Fall bedient die Werkzeugnavigation, legt eine Operation an, weist sie zusätzlich einem Objekt zu, ändert den Bildmodus per Auswahl und bestätigt eine Vierpunkt-Fotoregistrierung. Die bestehenden Material-/Gruppen-/Relief-/Schließen-Prüfungen bleiben erhalten. Ansichten: Arbeitsfläche und Operations-/Bildbereich in 1480×920 hell, Werkzeugbereich in 1180×760 dunkel, aktualisierte Reliefansicht.

GRBL-Pseudoterminalprüfung bestätigt ein Fenster mit mehreren noch nicht bestätigten Zeilen bei höchstens 100 Bytes, normale Quittierung/Idle, Abbruch/Neuinitialisierung und Live-Override-Bytes mit Statusrückmeldung. Kein serieller Anschluss des realen Lasers wurde in dieser Entwicklungsrunde geöffnet. Neue Emissions-/Materialabnahme, tatsächliche Overrides/Pufferung am M3 und separate Absaugsteuerung bleiben offen.

Zusatzprüfungen außerhalb CTest: Ein erzeugter QR-Code wurde mit OpenCV unabhängig wieder als `https://example.org` gelesen. Das optionale ONNX-Modell erzeugte eine 16-Bit-Tiefenkarte; der Lauf wurde in einer Linux-Netzwerk-Namespace ohne Netzwerkzugang wiederholt. ONNX-Telemetrie wird ausdrücklich deaktiviert. Installation des optionalen Modells und Prüfung seiner festgelegten Revision/SHA-256-Digests erfolgreich. Gewichte liegen ausschließlich im Benutzer-Cache.

Dateiformat 6 liest weiterhin 1–5. Aktueller Umfang, Bedienung und verbleibende Format-/Hardwaregrenzen stehen in [EXPANSION.md](EXPANSION.md). Vollständige LightBurn-Gleichwertigkeit, verlustfreie Projektmigration und Produktionsfreigabe werden weiterhin nicht behauptet. Die offene Benutzerinstanz wurde nicht beendet.

## Version 0.7: Rasterqualität und Einstieg (10.11.2026)

Acht CTest-Programme vollständig bestanden, normal und mit AddressSanitizer/UndefinedBehaviorSanitizer (`detect_leaks=0`, keine Aussage über Leckfreiheit). Die bisherigen Import-, Job-, GRBL-Pseudoterminal-, Relief-, Material- und Editorprüfungen bleiben aktiv. Neu: Originalauflösung über 512 Pixel, ein Pixel breites Referenzdetail, Bild-/GCode-Rundlauf, Größen-/16-Bit-Grenzfälle und feste Floyd-Steinberg-/Atkinson-Hashes aus dem vorherigen Algorithmus. [Messverfahren und Rohdaten](RASTER_QUALITY.md) dokumentieren den separaten Vorher/Nachher-Rastervergleich.

Basic/Advanced wird auf unveränderte Projektdaten beim Umschalten geprüft. Weitere Regressionen decken vollständige Profilübernahme samt Objektzuordnung und Entfernen individueller Werte, unveränderte Daten bei ungeeigneten/ungültigen Profilen, Foto/Schnitt-Konflikt, erweiterte Einstellungen und zusätzlichen Operationsbedarf ab. Eine eigene Prüfung füllt die auf 50 Einträge begrenzte Undo-Historie: Auch dann nimmt ein einziges Undo die gesamte Profilübernahme zurück. Die Implementierung bestimmt, ob das Profil tatsächlich eine Änderung auslöst; ein Vergleich der Historienlänge wäre bei voller Historie falsch.

Der kombinierte UI-Test öffnet tatsächlich einen Hilfetext über Tab-Fokus, prüft sichtbare Zahlen/Einheiten, die versteckte Advanced-Ausgabe, Aufruf/Abbruch/Erstellung der 5-mm-Probe ohne automatischen Start, Moduswechsel und das Zurücksetzen der Materialbestätigung. Der Ablauf „Herstellerprofil zuerst, SVG-Import danach“ erhält dieselbe aktive Bearbeitung; die Probe erhält eine feste Kopie des kompletten Herstellerprofils. Dadurch werden weder deaktivierte Bearbeitungen noch Quellenfelder beim Leeren des vorherigen Projekts zurückgesetzt. Ein reines Software-Testgerät mit umschaltbarer Simulator-Kennung prüft den Basic-Start: vor Materialbestätigung gesperrt, danach verfügbar, nach Parameteränderung wieder gesperrt. Dieses Testgerät besitzt keinen Hardwaretransport. Bestehende Auswahl-, Gruppen-, Material-, Relief-, Werkzeug- und Schließen/Verwerfen-Abläufe bestehen weiter.

Sichtprüfung: Basic 1480 × 920 hell und 1180 × 760 dunkel, Advanced, Materialauswahl und Tooltip-Umbruch. Screenshots unter `assets/basic.png`, `basic-compact.png`, `tooltip.png`. Ausgeführte UI-Läufe enthalten keine QML-Laufzeitwarnungen. CMake kann bei der Qt-Plugin-Erkennung Warnungen zu fehlenden statischen Plugin-Linktargets ausgeben; die dynamischen Plugins werden in den tatsächlich ausgeführten UI-Tests erfolgreich geladen.

Keine serielle Verbindung oder Laseremission in dieser Erweiterung. Bestehende Benutzerinstanz nicht beendet. Herstellerwerte, Beispiele und eigene Anfangswerte sind sichtbar unterscheidbar; fehlende Material-/Fokusmessungen werden nicht automatisch geschätzt. Eine Nutzerstudie mit unerfahrenen Personen, reale Materialabnahme, Hintergrundberechnung und umfassende Importkompatibilität bleiben offen. [Basic-Bedienung und Grenzen](BEGINNER.md), [Rastergrenzen und nächste Qualitätsschritte](RASTER_QUALITY.md).

## Stabilisierung 0.8 (10.11.2026)

Neun CTest-Programme vollständig bestanden: normal, mit AddressSanitizer/UndefinedBehaviorSanitizer und als optimierter Build in einem frischen Ubuntu-26.04-Container mit explizit installierten Abhängigkeiten. Leckprüfung deaktiviert (`detect_leaks=0`). Der neue Containerlauf deckte eine zuvor nur lokal vorhandene Python-Abhängigkeit für den Materialkatalog auf; `python3-bs4` ist nun im gemeinsamen CI-Skript enthalten. GitHub Actions verwendet dasselbe Skript und eine Matrix mit/ohne Sanitizer.

Zwölf neue Stabilitätsfälle prüfen tatsächliche Hintergrundergebnisse gegen synchrone Referenzen, unabhängige Snapshots, Ereignisschleifen-Heartbeat, Abbruch und veraltete Generationen, Worker-Ausnahmen, Beenden mit laufendem Worker, Blattwechsel/Undo und gesperrten Start/Export, große Raster und getrennte SVG-Renderer, Dateisperren und Betriebssystem-Schreibfehler, Recovery-Sperre/Rückfall, Wiederherstellung nach getötetem Hilfsprozess, beschädigte Projekte und eingefrorene CSV-Stapel einschließlich Überschreibschutz. Der UI-Test bedient den echten Abbruch-/Neuberechnen-Knopf während einer 2048-Pixel-Rasterberechnung und behält die bisherigen Material-, Gruppen-, Relief-, Werkzeug- und Schließenprüfungen bei.

Gefundene und abgesicherte Fehler: Blattwechsel müssen auch den asynchronen Auftrag entwerten; abgeschlossene Worker müssen ihre Statusänderungen an die Oberfläche melden; die Wiederherstellung aus `.previous` darf diese gute Kopie beim nächsten Autosave nicht durch die beschädigte Primärdatei ersetzen. Eine explizite aktive Generation verhindert außerdem das Ersetzen eines Futures vor Verarbeitung seines Abschlussereignisses. Projektbackups werden nicht mehr vor dem erfolgreichen Schreiben der neuen Backup-Datei entfernt.

Die grafische Anwendung verwendet Hintergrundberechnung und vorbereitete Bild-/GCode-Daten. CSV-Aufträge verändern nicht mehr vorübergehend das geöffnete Projekt. Die Veröffentlichung erfolgt ohne Überschreiben per Linux-Umbenennung, eigene Veröffentlichungen werden bei regulärem Fehler/Abbruch zurückgenommen. Einzelheiten, Restgrenzen und weitere Schritte zur Beta: [STABILIZATION_08.md](STABILIZATION_08.md). Das Projektformat bleibt 7.

Keine reale serielle Verbindung, Bewegung oder Laseremission in dieser Runde. Reale Material-/Emissionsabnahme, ein auslieferbares Downloadpaket und externe Einsteigertests stehen weiterhin vor 0.9 an. Import, Projektserialisierung/-schreiben und einzelne Advanced-Editorbefehle bleiben synchron; eine vollständig verzögerungsfreie Oberfläche für jede Operation wird nicht behauptet.

## Testkandidat 0.9.0-beta.1 (10.11.2026)

Neu: Installation als Ubuntu-26.04-amd64-DEB mit Systembibliotheken und passendem Quellarchiv; Erststartführung mit Simulatorübung; Hilfe, Versions-/Revisionsanzeige und lokaler Diagnoseexport nach fester Feldliste. Zehn CTest-Programme einschließlich neuer Release-Prüfung. Der erweiterte echte Qt-Quick-Bedientest durchläuft Erststart, Hilfe, Speichern-Abbruch, fehlgeschlagenes Speichern, erfolgreiche Fortsetzung nach Speichern, Verwerfen und Escape bei einer alten Sicherung. Eine beim Rundlauf gefundene Nullpunkt-Ergänzung in leeren Qt-Pfaden ist durch kanonische Ausgabe leerer Geometrie behoben.

Das Paketprüfskript installiert nur Laufzeitabhängigkeiten auf einer frischen Ubuntu-Basis, prüft Nicht-root-Start, Bildformate/Schriften/Fensterdarstellung, Projekt- und GCode-Ausgabe, normalen Start ohne Dateiparameter, Neuinstallation, Entfernung und Erhalt separater Benutzerdaten. Kein Compiler/Qt-SDK im Laufzeitsystem. Erststart- und Hilfedialoge werden offscreen visuell geprüft; echte Desktop-/HiDPI- und externe Einsteigertests bleiben separat erforderlich. Die Prüfung startet keine reale Verbindung.

Reale Hardware in diesem Schritt: M8 als Aux-only mit Laser-Aus quittiert, Nutzer bestätigt Lüfter weiterhin aus; M9 stellt Aux wieder zurück, READY bei X0/Y0. Keine Bewegung oder Emission. Kraftpapier ca. 100×100 mm mittig und maximal 5 % Sollleistung sind verbindlich für spätere Tests. Vorbereiteter 5-mm-Testauftrag wird softwareseitig auf Leistungsgrenze und Lage geprüft, nicht an den Laser gesendet. Gravur-, Schnitt- und allgemeine Reliefabnahme bleiben offen. [Anleitung und Testprotokoll](beta/START.md).

Lokale Regression: alle zehn CTest-Programme normal sowie mit AddressSanitizer/UndefinedBehaviorSanitizer bestanden (`detect_leaks=0`, keine Leckfreiheitszusage). Erststart und Hilfe zusätzlich bei 1180×760 dunkel visuell geprüft. Die neuen Bedienungstests liefern keine QML-Laufzeitwarnungen. Die endgültige Paketrevision und ihre unabhängigen CI-Ergebnisse ergeben sich aus `BUILD-MANIFEST.json` und den GitHub-Actions-Läufen des jeweiligen Kandidaten.


## Produktionsreife T01 Leistungswechsel auf Bewegungsblöcken (12.11.2026)

GCode setzt Leistungsänderungen während eines bereits aktiven M4-Abschnitts auf denselben G1-Block wie die zugehörige Bewegung. Dadurch unterbrechen alleinstehende S-Änderungen den GRBL-Planer zwischen Vorlauf, Arbeitsstrecke und Nachlauf nicht mehr. Initiales M4 und M5 vor Leerfahrten bleiben bestehen. Der Regressionstest wechselt 0→100→0 %, fährt mit M5 zur nächsten Zeile und prüft dort 0→50→0 %; beide Rasterzüge beginnen mit Nullleistung.

Alle zehn CTest-Programme normal und mit ASan/UBSan bestanden; Leckprüfung deaktiviert (`detect_leaks=0`). Der Hardwarevergleich vom 11.11. ist gesonderte Materialevidenz, kein Teil dieser Softwareläufe. In dieser Runde kein Zugriff auf den ausgeschalteten Laser.


## Produktionsreife T02 Kommunikation und Zustände (12.11.2026)

Neue PTY-Fälle reproduzierten veraltete Startfreigaben nach ungültigen/mangelnden Statusdaten, fehlende erneute Konfigurationsabfrage bei verschachtelter Statusausgabe und eine nicht verworfene Controllerwarteschlange bei Door während eines Jobs. Der simulierte USB-Verlust deckte zusätzlich rekursive Fehlerbehandlung bis zum Stacküberlauf auf. Diese Fehler sind behoben.

Bereitschaft benötigt jetzt aus derselben Statusmeldung gültige Position und bestätigte Nullleistung, bekannte Konfiguration einschließlich $31=0 und inaktive gemeldete Eingänge. Nichtnull-Leistung im Leerlauf, überhöhter gemeldeter Leistungs-Override während eines Jobs sowie Alarm/Door im aktiven Auftrag brechen ab. Verlorene Verbindungen verwerfen den Job; ein neuer Anschluss übernimmt ihn nicht. Ein Fehler kann die Best-Effort-Abschaltung nicht rekursiv erneut betreten, und Antworten hinter einem Verbindungsfehler werden nicht als neue Freigabe verbraucht. Eine verspätete Run-Meldung hebt eine ausdrücklich angeforderte Pause nicht auf.

Während der sequenziellen Konfigurationsabfrage ist das Statuspolling angehalten. Verschachtelte Statusrahmen werden getrennt; ein davon betroffener Zahlenwert wird nicht zur Freigabe benutzt. Ein einmaliger sauberer Neuabruf ist erlaubt, wiederholte Verunreinigung schließt die Verbindung.

25 Qt-Testfälle im GRBL-Programm bestanden; alle zehn CTest-Programme normal und mit ASan/UBSan bestanden (`detect_leaks=0`). Die neue Abschlussprüfung wartet auf gültige Position und Nullleistung; der Test synchronisiert ausdrücklich auf das Ende der Quittierungen, bevor er die fehlerhafte Idle-Meldung einspeist. Diese Softwareprüfungen ersetzen keine erneute Geräteabnahme. Es wurde kein physischer Port geöffnet.


### T02 Arbeitsnullpunkt und physische Grenzen

Vier neue Fehlerfälle reproduzierten, dass gültige Arbeitskoordinaten nach G54-Versatz außerhalb des realen 200×150-mm-Raums landen konnten, sowie eine Bewegungsfreigabe aus WPos ohne bekannten WCO. Der Treiber prüft nun vor dem ersten Jobbefehl sämtliche tatsächlich ausgegebenen Ziele einschließlich des bestätigten Versatzes. Jog, Rahmenfahrt und Fokusposition verwenden dieselbe physische Begrenzung. Die erste Modell-Herkunft eines Pfads wird nicht mit der tatsächlichen Startposition verwechselt; negative Versätze bleiben bei physisch gültigen Zielen möglich. Die Prüfung berücksichtigt die gesendete Drei-Nachkommastellen-Geometrie.

31 GRBL-Testfälle und alle zehn Testprogramme normal sowie mit ASan/UBSan bestanden (`detect_leaks=0`). Die positiven und negativen Versatzfälle enthalten jeweils erlaubte und vollständig vor Ausgabe abgewiesene Jobs. Keine Hardwareaktion.


### T02 Frischer Offset nach Nullpunktwechsel

Eine weitere PTY-Reproduktion zeigte, dass eine vor G10 eingetroffene WCO-Meldung nach dessen Abschluss erneut zur Freigabe benutzt werden konnte. Nach den letzten Quittierungen des Nullpunktwechsels werden Position und Offset deshalb nochmals ungültig. Erst eine spätere bestätigte Offset-/Positionsmeldung erlaubt den Abschluss und neue Bewegungen. Stop, Fehler, neuer Anschluss und Handshake verwerfen die offene Nullpunktänderung.

Alle 32 GRBL-Testfälle normal und mit ASan/UBSan bestanden. Die anderen neun Programme waren unmittelbar zuvor beim vollständigen T02-Grenzprüfungsstand normal und mit Sanitizern grün; ein vollständiger Kandidatenlauf folgt nach den nächsten Änderungen. Kein physischer Gerätezugriff.


## Produktionsreife T03 Materialdaten und tatsächliche Befunde

Das neue Geräteprotokoll trennt digitale Ausgabe, sichtbare Rasterwirkung, Gegenlichtprüfung und vollständige Materialtrennung. Historische pauschale Aufbauangaben (100-mm-Papier, maximal5%, Lüfteraus) wurden im aktuellen Beta-Testprotokoll durch je Test auszufüllende Bedingungen ersetzt. Die tatsächlichen 80-mm-Papierproben, transparenten Zahlen/Felder und der mechanisch getrennte Ausschnitt stehen als begrenzte Befunde darin. Die genaue Gerätezuordnung M3Pro/M3 sowie fehlende Herstellerwerte bleiben offen. Herstellerkatalog und Herstellerparameter sind bytegleich unverändert.

Anleitung und Fähigkeitenanzeige unterscheiden externe Webcam-Prüfaufnahmen von der vorhandenen manuellen Referenzfoto-Registrierung und fehlender Live-Anbindung. Die Herstelleransicht bezeichnet ihre Werte als unverändert und allgemein nicht materialfreigegeben. Kern-, UI- und Katalogprogramme normal und mit ASan/UBSan bestanden (je drei Programme, `detect_leaks=0`). 47 lokale Dokumentationsverweise wurden im Quellbaum geprüft; die Auflösung im Installations-/Downloadpaket folgt separat in T09. Keine Hardwareaktion und keine Änderung an Benutzerbibliotheken.


## Produktionsreife T04 Durchgängiger App Ablauf

Ein eigenes Workflow-Testprogramm verwendet die tatsächliche Machine-Fassade mit Hintergrundplanung. In heller und kompakter dunkler Ansicht führt es Herstellerprofilwahl, SVG-/PNG-Import, Dezimalkomma-Eingabe, Speichern, GCode-Export, Vorschau, vollständige laserfreie Simulatorrahmenfahrt, Start/Pause/Fortsetzen und Wiederöffnen aus. Der laufende Simulatorjob erreicht seinen ursprünglichen Endpunkt, obwohl der Entwurf inzwischen verschoben und neu berechnet wird. Gespeicherte Projektbytes und exportierte GCode-Zeilen bleiben beim Wiederöffnen gleich.

Die Dateidialoge werden geöffnet; das ausgewählte URL-Ergebnis wird in den tatsächlichen QML-onAccepted-Handler eingespielt. Die Interaktion mit einem nativen Desktop-Dateiauswahldialog bleibt ein separater Desktopfall. Der Qt-Test aktiviert das Hauptfenster nach dem Dialog wieder, bevor er echte Tastatureingaben an das fokussierte Zahlenfeld sendet.

Offline-Läufe besitzen nun eine feste HardwareAccess-Sperre: keine Portaufzählung oder echte Verbindung für Screenshot, GCode-Export und Selbsttest. Dieselbe Sperre isoliert den Workflow-Test. Zwei zusätzliche Prozess-Reproduktionen zeigten, dass Screenshot und GCode-Export vorher eine alte Benutzerbibliothek migrierten. Alle drei Offline-Modi verwenden nun temporäre Bibliotheks-/Einstellungsdaten; die vorgelegte alte Bibliothek und ihr Backupzustand bleiben unverändert. Normale interaktive Starts behalten die Benutzerbibliothek.

Alle elf CTest-Programme normal und mit ASan/UBSan bestanden (`detect_leaks=0`). Referenzdialog in kompakt/dunkel und Geräteansicht hell wurden zusätzlich anhand gespeicherter Aufnahmen geprüft. Zwei zusätzliche Regressionen reproduzierten falsche Basic-Parameter vor dem ersten Motiv und eine nach Ablehnung weiterhin ausgewählte Materialzeile. Basic zeigt nun die erste aktivierte Operation, sofern keine aktivierte Operation ausgewählt ist; Advanced behält die gezielte Operationswahl. Nach einer Profilwahl bindet sich die Materialanzeige wieder an das tatsächlich übernommene Profil. Beide UI-/Workflow-Programme wurden nach diesen Korrekturen normal und mit ASan/UBSan erneut bestanden; die neue helle Profilaufnahme zeigt 5000 mm/min und 100 % statt der deaktivierten Schnittwerte. Es wurden ausschließlich Simulator und PTYs verwendet.


## Produktionsreife T09 Herkunft und Dokumentationsauslieferung

Die Versionsnummer liegt zentral in `cmake/Version.cmake`. Ein bei jedem Build ausgeführtes CMake-Ziel erzeugt Versions-/Revisionsangaben; unveränderte Angaben behalten den Header-Zeitstempel. Vollständige Git-Revision, uncommittierte Änderungen und Git-Worktrees werden erkannt. Archive führen `SOURCE_REVISION` mit; fehlerhafte Revisionsdateien stoppen den Build. Sechs isolierte Pythonfälle prüfen diese Verträge einschließlich Launcher mit vorhandener Binärdatei: vor jedem Start konfigurieren/bauen, bei Fehler keine alte Datei starten.

Das Paket und der Download führen Dokumentation, Abbildungen und Lizenzen mit gleicher relativer Struktur. Drei flache Einstiegstexte verweisen auf die vollständigen Beta-Dokumente. Linkprüfung: 101 lokale Ziele im Quellbaum und 104 in der Dokumentations-Installationsstufe bestanden. Prüfsummen erfassen alle Downloaddateien rekursiv; ein nichtleeres Ausgabeziel wird abgelehnt. Der unabhängige Laufzeittest vergleicht installierte Version und Quellrevision mit dem Manifest und bietet einen optionalen Upgradepfad vom alten Paket. Alle 13 CTest-Programme bestanden nach diesen Änderungen normal und mit ASan/UBSan (`detect_leaks=0`). Der abschließende Paketlauf wird erst nach einem sauberen Commit als eigener Nachweis erfasst.


## Produktionsreife T08 Skalierung als Softwarefall

Der vollständige Workflow lief zusätzlich mit `QT_SCALE_FACTOR=2` bei kompakter dunkler Ansicht und mit `QT_SCALE_FACTOR=1.5` bei heller Ansicht durch, einschließlich Dateihandlersignalen, fokussierter Dezimalkomma-Eingabe, Vorschau, Simulatorlauf und Projektrundlauf. Beide Fälle bestanden; die kompakte dunkle Editoraufnahme wurde visuell geprüft. Dies prüft Qt-Skalierung mit Offscreen-/Software-Backend. Native Dateiauswahl, reale Wayland-/X11-Sitzung, Monitorwechsel und beobachtete externe Einsteiger bleiben offen.


## Kandidatenabnahme beta.2

Der genaue Paketstand `18ce2e67e691d1d6f117132f22d4479e9243ec4d` bestand den sauberen Docker-Bau und die frische Laufzeitprüfung mit vorheriger beta.1-Installation, Upgrade, Manifestabgleich, Nicht-root-Start, Neuinstallation/Entfernung und Erhalt von Benutzerdaten. Alle 145 Quelldateien im Archiv sind bytegleich zum genannten Commit. 104 Dokumentationsverweise und alle Downloadprüfsummen bestanden. Beide GitHub-Pipelines für den Arbeitszweig sowie den PR sind grün. Details, Prüfsummen, Nachweise und verbleibende Freigaben stehen im [Kandidatenprotokoll](beta/CANDIDATE_BETA2.md). Keine Geräteaktion während der Nacht; keine 1.0-Freigabe.

## Produktionsreife T05/F01 Projektangaben

Der Entwicklungsstand `0.9.0-beta.3-dev` ergänzt bearbeitbare Projektnamen und Klartextnotizen. Beide gehören zu Dirty-Zustand, Undo/Redo, Dateispeicherung und Wiederherstellung. Reine Metadatenänderungen verändern weder Werkzeugwege noch GCode. Der technische Diagnoseexport bleibt ohne Namen und Notizen. Projektimport übernimmt Objekte/Operationen und bewahrt die Metadaten des Zielprojekts; Öffnen lädt dagegen den vollständigen Projektstand.

Format 8 führt das erforderliche Notizfeld ein und liest weiterhin Formate 1–7. Historische Titel bleiben beim Lesen unverändert. Ungültige Eingaben, fehlende/überlange Notizen und künftige Formatversionen werden atomar abgelehnt. Die tatsächliche beta.2-Binärdatei aus dem geprüften DEB öffnete eine Format-7-Kontrolle und lehnte Format 8 mit Status 2 ohne Ausgabedatei ab. Der neue Leser öffnete dieselbe Format-8-Datei; der GCode war bytegleich zur Kontrolle. Es wurde kein Hostpaket installiert.

Der normale Workflow prüft den Dialog in heller und kompakter dunkler Ansicht mit echten Tasten: Entwurf verwerfen, Escape ohne Abwahl im Hintergrund, gesperrte globale Projektshortcuts, Eingabegrenze, zweizeiliger Klartext mit sichtbaren HTML-Zeichen, Übernahme und Dateirundlauf. Die abschließende kompakte dunkle Aufnahme einschließlich Kompatibilitätshinweis wurde visuell geprüft. Kernfälle prüfen zusätzlich Legacy-Titel, Wiederherstellung und Import-Undo/Redo; der Diagnosetest schließt explizite private Titel/Notizen aus.

Alle 13 CTest-Programme normal und mit ASan/UBSan bestanden (`detect_leaks=0`). Nach den letzten Dialog-/Importanpassungen wurden die vier betroffenen Programme core/ui/workflow/release in beiden Varianten erneut bestanden. 114 lokale Dokumentationsverweise sind gültig. Keine Geräteverbindung, Bewegung oder Emission. Der beta.2-Kandidat bleibt unverändert auf Revision `18ce2e6`; Vorlagen, Dateiverlauf und weitere Funktions-/Geräteabnahmen bleiben offen.

## Produktionsreife T05/F01 Dateiverlauf

Nur erfolgreiche Projekt-Öffnungen und -Speicherungen erzeugen einen lokalen Verlaufseintrag. ApplicationServices führt bis zu zehn eindeutige lokale URLs in den Anwendungseinstellungen; doppelte Pfade und Symlink-Aliase werden zusammengeführt. Import, Export, Wiederherstellung und fehlgeschlagene Zugriffe nehmen keine Datei auf. Nichtlokale oder relative gespeicherte URLs werden verworfen. Fehlende Dateien bleiben als nicht verfügbar erkennbar. Leeren entfernt keine Projektdateien und verändert das geöffnete Projekt nicht.

Der echte Workflow prüft beide Ansichten mit Speichern/Verwerfen/Abbrechen beim Wechsel. Die Ziel-URL bleibt richtig, obwohl eine vorherige Speicherung die Verlaufsliste umsortiert. Fehlende Dateien sind nicht anwählbar, Escape ändert weder Hintergrundauswahl noch Maschinenzustand; Leeren erhält die Projektdateien. Die kompakte dunkle Dialogaufnahme wurde visuell geprüft. Diagnoseexport und alle drei Offline-CLI-Modi erhalten keine persönlichen Verlaufspfade beziehungsweise verändern die vorgelegte Benutzereinstellung nicht.

Eine zusätzliche Fehler-/Reparaturprüfung reproduzierte einen dauerhaft erhaltenen QSettings-Fehlerstatus. Ein bewusst wiederholter Versuch nach Fehler verwendet jetzt einen neuen Settings-Handle. Der Test prüft sowohl den echten Fehler als auch die anschließende Reparatur ohne Neustart. Ein Verlaufsschreibfehler macht eine erfolgreich gespeicherte Projektdatei nicht nachträglich zu einem fehlgeschlagenen Speichervorgang und zeigt keine falsche Erfolgsleerung an.

Die Gesamtprüfung bestand alle 13 Programme normal und mit ASan/UBSan (`detect_leaks=0`). Nach der zusätzlichen Fehler-/Reparaturkorrektur bestand das gesamte Release-Testprogramm erneut in beiden Varianten. 114 lokale Dokumentationsverweise sind gültig. Keine Hardwareaktion; der bestehende beta.2-Kandidat bleibt unverändert. Vorlagen bleiben als weiterer F01-Teil offen.

## Produktionsreife T05/F01 Vorlagen und Dateizuordnung

Eigene `.blazertemplate`-Dateien verwenden einen getrennten Formatmarker und enthalten den vollständigen Projektstand einschließlich eingebetteter Motive, Operationen, Blätter und Notizen. Speichern einer Vorlage markiert das geöffnete Projekt nicht als gespeichert. Neu aus Vorlage erzeugt einen unabhängigen ungespeicherten Entwurf mit leerer Undo-/Redo-Historie. Materialbestätigung und Probenstatus werden nicht übernommen. Der Vorlagenpfad gelangt nicht in den Projektverlauf. Normales Projektspeichern schützt die Vorlage auch über Symlink-Aliase; bewusstes Überschreiben einer Vorlage legt `.bak` an.

Kernfälle prüfen Wiederverwendung nach Entfernen der ursprünglichen SVG-/Bilddateien, gleiche Werkzeugwege, sämtliche gemeinsamen Format-/Projektgrenzen, fehlerhafte und künftige Formate, Schreibsperre, Sicherung und unveränderten Ausgangsstand bei Fehler. Die Offline-CLI erzeugt aus einer Vorlage den erwarteten GCode und verändert weder Benutzerbibliothek noch persönliche Einstellungen. Der Paket-Selbsttest umfasst einen Vorlagenrundlauf und gibt die von Qt ermittelte Vorlagen-MIME-Kennung aus. Die Kennung wurde mit einer isolierten tatsächlichen Registrierung als `application/x-blazer-template` bestätigt; die Prüfung im neuen installierten Paket folgt separat.

Eine zusätzliche echte UI-Regression zeigte einen alten Speicherpfad nach Neu aus Vorlage. Die Dateizuordnung liegt nun getrennt vom Projektinhalt: erfolgreicher Zugriff setzt den Pfad, Neu/Beispiel/Vorlage/Recovery leeren ihn, Fehler und Undo führen nicht zu einer fremden Datei. Wiederherstellung bleibt auch bei inhaltlich identischen Daten ausdrücklich ungespeichert.

Ein leer gesetzter Auswahlwert allein beseitigte den sichtbaren alten Dateinamen im wiederverwendeten Qt-Dialog nicht. Der Test prüft deshalb das tatsächlich sichtbare Eingabefeld; der [Qt-6.10.2-Getter](https://raw.githubusercontent.com/qt/qtdeclarative/v6.10.2/src/quickdialogs/quickdialogs/qquickfiledialog.cpp) ergänzt selbst leere URLs um die Dateiendung und eignet sich dafür nicht als alleiniger Nachweis. Jeder Speicheraufruf erhält jetzt einen frischen Dialog mit dem aktuellen Dokumentpfad beziehungsweise leerem Namen. Der letzte Ordner bleibt nutzbar. Zu alte Rückrufe werden ignoriert. Die vom [Qt-Quick-Backend](https://raw.githubusercontent.com/qt/qtdeclarative/v6.10.2/src/quickdialogs/quickdialogsquickimpl/qquickplatformfiledialog.cpp) ans Fenster gehängten eigenen Chooserobjekte werden vor Freigabe dem Wrapper zurückgegeben; aktive oder fremde Objekte bleiben erhalten. Der Workflow prüft nach mehreren Vorgängen eine begrenzte Zahl eigener Dialogressourcen.

Der reale Workflow in heller und kompakter dunkler Ansicht umfasst Menü, Abbrechen/Verwerfen/Speichern, abgelehntes Projektspeichern in eine Vorlage, richtigen Dateinamen, frische Projektzustände und Erhalt der Vorlage bei eigener Speicherung. Das kompakte Menü wurde visuell geprüft. Der Quellstarter reicht Dateipfade über `%f` weiter und behandelt Sonderzeichen nach der [Freedesktop-Exec-Spezifikation](https://specifications.freedesktop.org/desktop-entry/latest/exec-variables.html). Ein isolierter Gio-Start führt ausschließlich einen eigenen Stub aus und bestätigt Pfade mit Leerzeichen, Unicode und Sonderzeichen; MIME-Registrierung, wiederholte Installation und Schutz fremder Starter-/MIME-Dateien sind geprüft. Es wurden keine Benutzerstarter verändert oder echte Benutzeranwendungen gestartet.

Der Stand vor der zusätzlichen Wiederherstellungsprüfung bestand alle 13 CTest-Programme normal (90,34 s) und mit ASan/UBSan (107,94 s, `detect_leaks=0`). Darin enthalten sind acht Python-Build-/Starterfälle. Alle 114 lokalen Dokumentationsverweise sind gültig. Keine Geräteverbindung, Bewegung oder Emission. F01 ist damit softwareseitig geprüft; native Desktopbedienung und externe Anfänger bleiben T08. Der breite übrige Funktionskatalog und die echten Geräte-/Materialabnahmen bleiben offen.


Die zusätzliche Startprüfung reproduzierte einen Datenverlust: Ein bereits ungespeicherter neuer Entwurf konnte die ältere Autosave-Datei überschreiben, während deren Wiederherstellungsentscheidung noch offen war. Recovery sperrt neue Sicherungen jetzt bis zum erfolgreichen Wiederherstellen oder ausdrücklichen Verwerfen. Haupt- und Vorversion bleiben bis dahin unverändert. Fehlgeschlagene Wiederherstellung und fehlgeschlagenes Entfernen lösen die Sperre nicht. Nach bestätigter Entscheidung läuft Autosave wieder; nach Sitzungsende lösen spätere Dokumentänderungen keine neue Sicherung aus.

Der UI-Fall prüft sauberen Start und ungespeicherten neuen Entwurf: Escape beim Wiederherstellen, abgebrochenes Beenden, abgebrochenes vorgeschaltetes Speichern, erneute offene Wiederherstellungsentscheidung und blockierte Hintergrundshortcuts. Alle Prüfdaten liegen in temporären Verzeichnissen; persönliche Wiederherstellungsdaten wurden nicht geöffnet oder verändert.

Nach der Recovery-Erweiterung sind alle 13 Testprogramme normal und mit ASan/UBSan geprüft. Die Gesamtläufe bestanden die zwölf übrigen Programme; der UI-Test wurde nach Ergänzung einer expliziten Fokus-Synchronisierung abschließend separat in beiden Varianten bestanden (14,68 s / 22,77 s). Die Registrierung eigener QML-Typen liegt nun im Test-Setup, sodass auch einzelne UI-Fälle ausführbar sind. Der Test wartet nach Dateiauswahl auf den tatsächlich zurückgekehrten Tastaturfokus; es wurde kein zusätzlicher Produkt-Fokuszwang eingeführt. Die beiden neuen Stabilitätsfälle prüfen echte Timer sowie erfolgreiche und fehlgeschlagene Recovery-Entscheidungen. 114 lokale Verweise und `git diff --check` bestehen. Keine Geräteaktion und kein neues abgenommenes Installationspaket.


## Beta.3 – Abhängigkeit der isolierten Desktopprüfung

Der erste GitHub-Gesamtlauf fand eine fehlende Testabhängigkeit: `xdg-mime query filetype` braucht im generischen Desktopmodus `mimetype` aus `libfile-mimeinfo-perl`, um die registrierte Shared-MIME-Datenbank abzufragen. Ohne dieses Backend fällt es auf das im Minimalcontainer ebenfalls fehlende `file` zurück und endet mit Status 4. Der Fehler wurde im isolierten Builder ohne `file` identisch reproduziert; nach Installation von `libfile-mimeinfo-perl` bestehen alle acht Python-Fälle. CI installiert das Backend jetzt ausdrücklich; der optionale Desktopfall prüft seine Verfügbarkeit. Die tatsächlichen Paket-Selbsttests einschließlich Qt-MIME-Kennung und Upgrade bestanden bereits. Für den abschließenden Kandidaten ist eine neue exakte Paketrevision mit erneuten GitHub-Prüfungen nötig.


## Geprüfter Kandidat beta.3

Der abschließende Kandidat stammt aus `4da2e97a82594bf85836210d521c797a946c95e0`. Alle 149 versionierten Quelldateien sind vollständig/bytegleich im Archiv und behalten ihr ausführbares Kennzeichen. Die GitHub-Gesamtprüfung besteht alle 13 Programme normal (90,25 s) und mit ASan/UBSan (100,50 s, `detect_leaks=0`); normale und Sanitizer-Regression sowie Paket-/Runtime-Pipelines sind auf Arbeitszweig und PR grün. Der zuvor fehlende Shared-MIME-Abfragebackend ist ausdrücklich installiert und geprüft.

Frische lokale Laufzeitprüfung mit Upgrade von beta.2, tatsächlicher alter Projektdatei und persönlichen Testeinstellungen, Nicht-root-Start, exakter Herkunft, Qt-Vorlagen-MIME-Kennung, Vorlagen-/Projektrundlauf, Neuinstallation und Entfernung bestanden. Legacy-Projekt und Einstellungen blieben bytegleich, alter/neuer Leser und neue Vorlage erzeugten denselben GCode. 117 lokale Verweise im Download und installierten Paket sowie sämtliche Prüfsummen bestanden. Protokoll und Summen: [Kandidat beta.3](beta/CANDIDATE_BETA3.md). Kein Hostupgrade und keine Geräteaktion; kein Nachweis tatsächlicher nativer Desktopbedienung, externer Anfänger oder vollständiger Geräte-/Materialfreigabe.


## T05/F02 Arbeitsblätter – Entwicklung beta.4-dev

Vor Korrektur reproduzierten Fälle drei falsche aktive Seiten beim Löschen eines fremden Blatts, verlorene Gruppen bei Auswahlkopie, die feste Kopfzeile „Blatt 01“ und sichtbare alte Objektzeilen nach einem neuen leeren Blatt. Die Korrekturen erhalten die nicht betroffene aktive Seite und aktualisieren die Modellrollen für die Objektliste. Zielwahl und tatsächliche Anzeige/Ausgabe sind in der Oberfläche getrennt beschriftet; Umbenennen, Kopieren und Löschen beziehen sich auf das gewählte Ziel.

Die Kernfälle prüfen sechs Lösch-/Nachbarvarianten mit Undo/Redo, vollständige Blattkopien mit Bild/SVG/Relief/Text, ausgeblendeten Objekten, Gruppen, Objektabweichungen und zusätzlichen Operationen. Dateirundlauf und Recovery erhalten aktive Seite, Einbettungen und bytegleichen GCode auch nach Entfernen der ursprünglichen Motivdateien. Kopierte Geometrie und Gruppen bleiben unabhängig. Teilgruppen und Auswahlkopien ins eigene Blatt sind eingeschlossen. Neue Namen/Grenzen sowie 100-Blatt-, 2000-Objekt- und Einbettungsbudget werden atomar geprüft, einschließlich unveränderter Historie bei Fehler.

Der normale Workflow prüft die tatsächlichen Buttons, Zielwahl ohne Ausgabewechsel, Umbenennen eines anderen Blatts, vollständige Blattkopie, Auswahlkopie, Löschen, globale Undo/Redo und lokale Texteingabe-Undo. Ein während Pause kopiertes und aktiviertes neues Blatt ersetzt den eingefrorenen Simulatorauftrag nicht. Der helle Ablauf und die Sichtprüfung sind bestanden; die vollständigen normalen/Sanitizer-Prüfungen und die kompakte dunkle Sicht folgen vor Abschluss. Projektweite Operationen bleiben ausdrücklich gemeinsam, neue Motive und Gruppierungen erhalten eigene Kennungen. Keine Hardware benutzt.


Eine zusätzliche Prüfung zulässiger Maximalnamen reproduzierte nach der ersten grünen Gesamtregression einen angeschnittenen Startknopf und eine überlaufende Fußzeile bei 1180×760. Projekt-/Blattnamen teilen sich nun den verfügbaren Kopfbereich; Fußzeile und Dropdown kürzen lange Namen sichtbar, der vollständige Name ist im Hinweis und im Produktionsbereich lesbar. Auch lange Zeichenfolgen ohne Leerzeichen umbrechen. Namen mit HTML-Zeichen bleiben Klartext. Der Workflow prüft reale Knopf-/Fußzeilengrenzen, tatsächlich gemalte Textbreite und den geöffneten Hinweis; die kompakte dunkle Aufnahme wurde vor und nach Korrektur verglichen. Danach folgen erneute Gesamtprüfungen des korrigierten Stands.


Die erweiterte Prüfung fand zusätzlich eine tatsächliche Status-/Auswahlkopplung: `Document::changed` meldet auch Fortschritt der Hintergrundberechnung. Die direkte Bindung von Zielindex und neu erzeugtem Dropdownmodell an diese Meldung setzte eine bewusst gewählte andere Seite auf das aktive Blatt zurück. Eine gezielt ausgelöste reine Statusmeldung reproduzierte den Sprung von Ziel 1 auf Ziel 0. Das Dropdownmodell wird nun nur bei tatsächlich geänderten Blattnamen erneuert; die Zielauswahl folgt nur einem tatsächlichen Wechsel der aktiven Ausgabe und bleibt sonst erhalten. Der Workflow prüft den Schutz sowohl bei geschlossenem als auch geöffnetem Dropdown.

Der Offscreen-Maustest berechnet nach einer auslösenden Zeigerbewegung seine Zielposition erneut, bis die Geometrie stabil ist: Ein verkleinerter langer Blattname verschob den Auswahlknopf nach dem ursprünglichen Koordinatenabgriff. Menüöffnung/-schluss werden ausdrücklich abgewartet. Mit diesen vollständigen Gesten bestehen beide Workflowansichten ohne Screenshot-Zwischenschritte normal (62,04 s) und mit ASan/UBSan (68,67 s). Es wurde kein Klickhandler direkt aufgerufen. Der abschließende Bau und die betroffenen Programme ui/workflow/release laufen vor Sicherung des Stands noch einmal.


Der F02-Endstand ist softwareseitig geprüft. Die Gesamtprüfungen umfassen alle 13 Programme in beiden Varianten; die nachträglichen UI-Korrekturen sind abschließend durch die drei betroffenen Programme release/ui/workflow in beiden Varianten geprüft (normal insgesamt 80,69 s, ASan/UBSan 97,77 s; `detect_leaks=0`). Darin bestehen beide vollständigen Oberflächenabläufe ohne Screenshot-Zwischenschritte (62,00 s / 68,77 s). Kernprüfungen enthalten zusätzlich die tatsächliche aktive SVG-Ausgabe und gleichen SVG-/GCode-Inhalt der Blattkopie. Die kompakte dunkle Ansicht mit langen Namen und geöffnetem Klartexthinweis sowie die tatsächlich gebaute Anwendung mit einem zwei Blätter umfassenden Format-8-Projekt wurden visuell geprüft. 126 lokale Dokumentationsverweise und Diff-Whitespace-Prüfung bestanden. Keine serielle Verbindung, Bewegung, Homing, Kamera oder Emission. Native/externe Bedienung bleibt T08; kein neuer Paketkandidat nach der unveränderten beta.3-Revision 4da2e97.
