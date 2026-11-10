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
