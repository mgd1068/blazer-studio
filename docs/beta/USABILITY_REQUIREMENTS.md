# Einfacher Standardworkflow

Verbindliche Produktanforderung vom 25.11.2026:

- Basic ist der Standard und richtet sich an Anfänger.
- Ziel: Datei laden, gespeicherten Laser verwenden, Start klicken. Die Geräteauswahl ist nur beim Einrichten oder Wechsel nötig.
- Technische Prüfungen und notwendige Vorbereitung laufen automatisch.
- Nur fehlende entscheidungsrelevante Angaben und echte Fehler unterbrechen.
- Grafik und Text hinzufügen, verschieben und skalieren bleiben direkt erreichbar.
- Controllerdetails, Kalibrierung, Diagnose und seltene Optionen stehen unter Advanced.
- Ein gespeichertes Geräteprofil ersetzt wiederholte technische Einrichtungsschritte.
- Startabsicht, aktuelle Gerätezustände und vollständige Fahrgrenzen werden im Backend geprüft; eine einfache Oberfläche ersetzt diese Prüfungen nicht.

Die Umsetzung erfolgt schrittweise. Automatische Referenzfahrt beim bewussten Verbinden ist ein Baustein; ein bereits vollständig umgesetzter durchgängiger Drei-Schritt-Startworkflow wird damit nicht behauptet.

Ab beta.10.7 bleibt die zuletzt gewählte lokale Geräteinstanz mit ihrem Profil über Programmstarts und Dateiladen erhalten. Eine Datei darf die physische Gerätewahl nicht ersetzen oder ihre Geometrie an den Laser skalieren. Bei einer geänderten Gerätezuordnung wird die alte Auswahl nicht automatisch übernommen.

Im normalen beaufsichtigten Gerätezyklus gilt der frische tatsächliche Startklick zugleich als Anwesenheitsbestätigung für genau diesen Auftrag. Separate Moduswahl und Anwesenheitskästchen entfallen. Ein echter Fehler wird dadurch nicht quittiert. Explizit konfigurierte Sensor-/externe Zubehörmodi behalten ihren eigenen Vertrag; sie werden nicht automatisch ersetzt. Lüfterlauf und Luftstrom werden nicht als gemessen behauptet.

Basic erlaubt Start direkt aus der Bearbeitung; die Vorschau bleibt optional. Das bestehende bewusste Verbinden und gegebenenfalls gespeicherte Auto-Homing bleiben Voraussetzung. Ein Startklick bei getrennter Verbindung stellt noch keine automatische vollständige Verbindungs-/Homing-/Auftragskette her.

Ab beta.10.8 übernimmt der Start bei verbundenem und passend kalibriertem Gerät auch die Referenzfahrt und gegebenenfalls den bestätigten Anlauf in den Innenraum. Eine separate Referenzaktion ohne Auftrag bleibt im Basic-Gerätebereich gut sichtbar. Erfolgreiche Aufträge kehren erst nach bestätigtem S0/Idle zum gespeicherten Maschinen-Home zurück; STOPP/Fehler lösen keine Rückfahrt aus. Die offene vollständige Verbindungs-/Startkette für noch getrennte Geräte wird damit weiterhin nicht als umgesetzt behauptet.
