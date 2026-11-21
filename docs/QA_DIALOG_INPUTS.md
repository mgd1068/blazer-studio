# Reproduzierbare Eingabeprüfung der neuen Dialoge

`dialog_input` lädt die tatsächlichen Dateien aus `qml/` über `tests/qml/tst_dialog_inputs.qml`. Es gibt keine eingebetteten oder eingefrorenen Dialogkopien. Der C++-Starter verwendet Qt6::QuickTest, Qt Quick Controls und den Offscreen-/Softwaremodus.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target blazer-dialog-input-tests
ctest --test-dir build -R '^dialog_input$' --output-on-failure
```

Der CI-/Paketbauer installiert `qml6-module-qttest` als Testabhängigkeit. Das Anwendungspaket erhält keine QuickTest-Laufzeitabhängigkeit. Der Test lädt die drei Dialoge `BarcodeDialog`, `DeviceManagerDialog` und `MaterialTestRecordDialog` samt den normalen `NumberField`-/Buttonkomponenten. Kontrollierte Backendspione protokollieren Aufrufe und liefern feste Daten; sie öffnen keine Geräte oder Dateidialoge und schreiben keine Produktdaten.

Die Prüfung verwendet echte Tastaturereignisse für numerische Entwürfe und Mausereignisse für die Aktionsbuttons. Sie umfasst:

- Noch nicht abgeschlossene Dezimalkommaeingabe, Zollumrechnung, explizites `mm`-Suffix und einen Einheitenwechsel während eines unveränderten Entwurfs. Der Backendaufruf muss den tatsächlich angezeigten Entwurf übernehmen.
- Negative Werte, NaN/Infinity, Exponentenüberlauf, Leertext, falsche Eingabe und Werte außerhalb des Modulbereichs. Der Anlegen-Button bleibt deaktiviert. Außerhalb des Arbeitsraums wird entweder bereits der Button gesperrt oder eine Backendablehnung sichtbar im offenen Dialog gehalten.
- Barcodeinhalt mit 129 Zeichen bleibt vollständig erhalten und wird abgelehnt, statt still auf 128 Zeichen gekürzt zu werden. Nicht-ASCII-Eingaben werden ebenfalls abgelehnt.
- Ausstehende Anschlussänderungen verhindern Geräteauswahl bis zum ausdrücklichen Speichern. Die anschließende Auswahl muss den gespeicherten Anschluss verwenden; kein Verbindungsaufruf existiert im Spion.
- Veränderte Geräte-/Parametersignaturen sperren eine Materialbeobachtung. Auch ein direkt ausgelöstes Buttonsignal darf den veralteten Parameterstand nicht speichern.
- Der Aufruf zum Speichern einer Beobachtung verwendet den rohen Dickenentwurf. 4001 Notizzeichen sperren den Button entsprechend dem Backendlimit von 4000. Historische Beobachtungen werden aus dem tatsächlichen verschachtelten Datenvertrag dargestellt.
- Gerätenamen und historische Materialtexte werden ausdrücklich als `Text.PlainText` dargestellt; HTML in Namen darf keine Formatierung oder Bildanforderungen auslösen.

Prüfung am 21.11.2026: **16 QtTest-Ergebnisse einschließlich Initialisierung/Abschluss, 0 Fehler, 0 übersprungene Fälle**. Die Ursachen der vorangegangenen UI-Fehler waren ein falscher Zugriff auf verschachtelte Ledgerdaten, ein abweichendes Notizlimit, stille Barcodekürzung, ungespeicherte Geräteformularwerte und automatische Rich-Text-Erkennung. Die Tests prüfen die korrigierten Verträge direkt an den Dialogdateien.

Dauerhafte Evidenz liegt unter `/home/mathias/benslab/analysis/blazer-rebuild-20261121/agent-qml-input-review/`, einschließlich Ergebnisprotokollen und SHA-256-Werten der geprüften Formdateien. Der unabhängige Agent prüfte diese Formstände in seinem eigenen Worktree; die Integration lädt anschließend die im integrierten Repository vorhandenen Dateien erneut.

Diese Prüfung beweist weder den Weg aus dem Hauptmenü zu den Dialogen noch die tatsächliche Persistenz, Barcode-Decodierbarkeit oder physische Sicherheit. Dafür gelten die integrierten Workflowtests und die getrennten Backend-/Decoder-/Geräteabnahmen. Keine installierte Anwendung und kein realer Geräteanschluss wurden für diese Prüfung verwendet.
