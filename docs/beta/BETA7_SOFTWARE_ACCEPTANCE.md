# beta.7-dev: geprüfte Softwarelieferung

Stand: 22.11.2026. Ausgelieferte Version: **0.9.0-beta.7-dev**, Ubuntu/Kubuntu 26.04 amd64, Qt 6.10.2. Der installierte und veröffentlichte Quellstand ist **`9a6abbc72af8ac7ebbf6523c53e0f91b4176a90b`**. Dieser Bericht wurde nach der Installation ergänzt; maßgeblich für die Binärdatei bleibt die Revision im Buildmanifest und im Release-Tag.

[Download und Release](https://github.com/mgd1068/blazer-studio/releases/tag/v0.9.0-beta.7-dev) · [vollständiger CI-Lauf](https://github.com/mgd1068/blazer-studio/actions/runs/44958939625)

## Änderungen

Controller, Serialport und Überwachung laufen im eigenen Worker. STOP und Close übertragen ihren ausstehenden Zustand unter demselben Mutex in die Dispatchreservierung; diese endet erst zusammen mit dem tatsächlich veröffentlichten Ergebnis. Ein deterministischer Test belegt die zuvor mögliche kurzzeitige falsche Leerlaufmeldung sowie deren Korrektur. Die ursprünglichen zehn Guard-Szenarien bleiben unverändert erhalten.

Start, Positionierung, Fokus, Fortsetzen und Overrides benötigen frische native Eingaben und den passenden Controllerkontext. Fortsetzen verlangt eine frische gemeldete Hold-Bestätigung. Die tatsächlichen dezimalen GRBL-Ausgabekoordinaten werden gegen den Arbeitsraum geprüft, auch wenn Rundung einen geometrisch gültigen Randpunkt nach außen verschiebt. Die manuelle Zwei-Marken-Registrierung verändert tatsächlich Offline-Vorschau, Simulation, GCode und Auftragsrahmen.

## Nachweise

| Prüfung | Ergebnis |
|---|---|
| Lokaler vollständiger normaler Lauf | **59/59 CTest-Gruppen**, 330,64 s |
| Lokale kritische ASan/UBSan-Gruppen | **17/17**, 133,83 s; Leak-Erkennung deaktiviert |
| GitHub CI | Vollständige normale und instrumentierte Läufe erfolgreich |
| Eigene Gegenprüfungen | Native Eingaben, Callback-Lebensdauer, begrenzte Ereignisjournale, Koordinatenpräzision und STOP-Übergabe |
| Paketbau | Gepinntes isoliertes SDK, ohne Netzwerk oder Geräte; 99 Dateiprüfsummen bestätigt |
| Neuinstallation und Upgrade | Beide erfolgreich; Reinstallation, Entfernung und Datenerhalt geprüft |
| Originaler Umrandungsfall | 200 × 150 mm, vier Kanten, 700 mm; Aus-Fahrt zum Anfang, keine Füllung, kein geratener Zubehörbefehl oder Rückfahrziel |
| Lokale Installation | beta.5.2-local durch beta.7-dev ersetzt; installierter Binärhash, Selbsttest und Offline-Export erneut geprüft |
| Benutzerzustand | Einstellungen und Originalprojekt bytegleich erhalten; Startmenüeintrag aktualisiert; Rückfallpaket und Datenkopien gesichert |

Die Softwaretests verwenden Simulator und eigene PTYs. Es wurden keine Befehle an einen realen Laser gesendet. Der installierte Selbsttest meldet ausdrücklich `simulation=true` und `hardwareTested=false`.

Ein früherer Paket-Zusatztest scheiterte an nicht traversierbaren Fixture-Verzeichnisrechten für den normalen Containerbenutzer. Korrigierte Rechte sind im endgültigen Testaufbau berücksichtigt; beide endgültigen Paketläufe bestanden ohne Wiederholung. Weitere CI-Korrekturen erhalten die ursprünglichen Prüfungen: Build und Tests laufen als normaler Benutzer, der erste Ausgabebefehl wird über den unabhängigen kumulativen Peerzähler statt aus einem begrenzten Journal belegt, und Scrolltests messen nach verzögerten Layoutänderungen erneut.

## Artefaktidentität

- DEB SHA256: `89045374f057d9120206b6665754f0dcd6d1f582ff54654b187c047a1bd0ae12`
- Installierte Programmdatei SHA256: `e8837faf05c29f72bc531d79cdbb0f1577e67fa60090968bc288537255412527`
- Sourcearchiv SHA256: `64ec20f479d8c74d6134b8b9d05036a2186adca4d6d66ffa0f21a588da469a1e`

Das vollständige Linux-Bundle enthält Dokumentation, Buildmanifest und interne `SHA256SUMS`. Die separate `RELEASE-SHA256SUMS` bezieht sich auf die tatsächlichen GitHub-Dateinamen der vier Downloadartefakte.

## Verbleibende Grenzen

**Vollständige LightBurn-Parität und reale Maschinenabnahme bleiben offen.** Die [Funktionsmatrix](../FUNCTION_ACCEPTANCE.md) und [bekannten Einschränkungen](KNOWN_LIMITATIONS.md) bleiben maßgeblich. Tatsächliche Referenz, G54/WCO, Verfahrraum und Lüfter/Luftstrom benötigen weiterhin belastbare Nachweise; die erforderlichen realen Bewegungs- und Laserfreigaben werden durch diese Softwaretests nicht erteilt. Eine Hold-Meldung allein beweist keinen physischen Stillstand.

Wayland-Tastaturaktionen bleiben mangels ausreichendem Eingabezeitnachweis gesperrt. Print and Cut ist ein [manueller Offline-Teilumfang](../MANUAL_PRINT_AND_CUT.md), ohne Kamerakalibrierung oder automatische Markensuche. Weitere Controller-, Kamera-, Rotary-, Z- und Galvo-Familien sowie die übrigen offenen Matrixpunkte sind nicht durch die Paketfreigabe abgenommen. Die [STOP-Reservierung](../RUNTIME_STOP_HANDOFF.md) ersetzt keinen unabhängigen physischen Not-Aus.
