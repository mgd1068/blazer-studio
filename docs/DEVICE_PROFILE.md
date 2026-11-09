# Blaze X M3 – importiertes Profil

Ausgelesen am 09.11.2026 aus der vorhandenen Installation, ausschließlich Dateizugriff. Die Originalkonfiguration wurde nicht verändert und kein serieller Anschluss geöffnet.

Quelle: `~/.config/LightBurn/prefs.ini`, ein Geräteprofil mit Anzeigename **M3**, Controller **GRBL**, Profilpfad `GRBL`. Die vorhandenen Projekthinweise `~/Projekte/Laser/Katzenschild/Hinweise.txt` nennen dazu **Blaze X M3 / 5-W-Modul** und denselben Arbeitsraum. Die Modulbezeichnung stammt aus diesen Hinweisen, nicht aus einer Hardwareabfrage.

| Eigenschaft | Hinterlegter Wert |
|---|---|
| Arbeitsraum | 200 × 150 mm |
| Controller/Transport | GRBL / seriell |
| Baudrate | 115200 |
| Letzter Anschluss | ttyUSB0 |
| Leistungsskala S-Max | 1000 |
| Manueller Fokuspunkt | 2 %; rechnerisch S20 bei dieser Skala |
| Jog-Befehl | GRBL `$J` im Profil aktiviert |
| Z-Steuerung | deaktiviert |
| Referenzfahrt beim Start | in LightBurn aktiviert; wird vom Entwurf nicht an Hardware ausgeführt |
| Firmware/GRBL-Version | aus dieser Datei nicht ermittelt |

Das bereinigte Profil liegt in `profiles/blaze-x-m3.json` und wird in die Anwendung eingebettet. Es enthält keine LightBurn-Lizenzinformationen. Die Werte beschreiben die eingerichtete Software, noch keine Live-Verifikation von Firmware, Nullpunktorientierung, Endschaltern oder Leistungsausgabe. Die Simulationsbeschleunigungen in LightBurn sind Schätzparameter und werden nicht als gemessene Maschinengrenzen ausgegeben.

Im Prototyp erfolgt jede Geräteaktion über `Simulator`. Es gibt weder eine serielle Implementierung noch eine GCode-Ausgabe. Die voreingestellten Schnitt-/Gravurwerte des Beispiels sind Layout-Demowerte und keine geprüften Materialrezepte.

Objektkoordinaten werden in diesem Entwurf ab der linken oberen Ecke gespeichert; der Maschinenmarker benutzt die linke untere Ecke. Die Bedienoberfläche kennzeichnet diesen Unterschied. Vor echter Geräteanbindung muss eine einheitlich geprüfte Koordinatentransformation eingeführt werden.

## Ergänzte Herstellerquelle

Die vom Nutzer genannte [Maschineneinstellungsseite](https://de.blazexlaser.com/pages/maschineneinstellung) ist als separate [Materialreferenz](MANUFACTURER_MATERIALS.md) erfasst. Sie nennt M3 Pro; eine Gleichsetzung mit dem eingerichteten M3 ist nicht bestätigt.
