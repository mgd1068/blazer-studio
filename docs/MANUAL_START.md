# Start ab Handposition

Der Handstart verwendet einen für die aktuelle Verbindung erfassten lokalen
Startpunkt. Die globale Maschinenposition bleibt nach dem Verschieben von Hand
unbekannt. Alte Positionszähler und die Profilmaße bestimmen keine Restwege.

## Vorbereitung

1. Motiv, Größe, Material und Bearbeitung vorbereiten. Unter **Gerät & Positionierung**
   **Start ab Handposition …** öffnen. Das Öffnen startet keine Einrichtung und keine Fahrt.
2. **Handpositionierung beginnen** wählen. Der Controller wird zunächst nur gelesen.
   Die Software verändert keine Motoreinstellungen und gibt Motoren nicht automatisch frei.
3. Den Kopf ausschließlich bei tatsächlich freien Achsen von Hand setzen. Nicht gegen
   Haltekraft drücken. Die vier freien Restwege in den tatsächlichen Maschinenrichtungen
   X−, X+, Y− und Y+ konservativ messen und in Millimetern eintragen. Der gesamte
   angegebene rechteckige Fahrraum muss frei sein. Unbekannte Werte bleiben leer.
4. Messwerte übernehmen. Motorfreigabe beim Handverschieben, Achsrichtungen und freien
   Raum, Kopfstillstand mit Händen außerhalb sowie geschlossene Haube tatsächlich
   prüfen und bestätigen. Die feste Reserve beträgt **2 mm pro Richtung**.
5. **Hier den lokalen Startpunkt übernehmen** bewusst betätigen. Diese Aufnahme kann
   den Controller zurücksetzen und setzt einen lokalen Arbeitskoordinatenbezug; sie
   fordert keine XY-Fahrt an. Leere Controller-Startupblöcke, passende Einstellungen,
   geschlossene Haube und frische quittierte Rückmeldungen sind erforderlich.
   Ein Alarm oder eine offene Haube wird nicht automatisch entsperrt.

Globale Softlimits mit unbekannter globaler Kopfposition sind für diesen Ablauf
nicht kompatibel. Die Anwendung schaltet sie nicht eigenständig ab. Eine Ablehnung
zeigt den Grund; kein wiederholtes Starten gegen einen ungeklärten Zustand.

## Motiv platzieren und Wege prüfen

Den gewünschten der neun Motivanker wählen. Dieser Punkt des Motivs liegt am
gewählten Kopfstandort. Die Anwendung plant sämtliche Wege einschließlich Anfahrt,
Leerfahrten und Rasterüberlauf. Zusätzlich prüft sie die tatsächlich formatierten
Controllerkoordinaten; Rundung darf keine Grenze überschreiten.

**Verfahrwege prüfen** bereitet den Auftrag vor. Die Tabelle vergleicht benötigte
Wege mit den gemessenen Restwegen abzüglich Reserve. Eine Änderung des Motivs,
Ankers oder der Bearbeitung verwirft die alte Auftragsbestätigung. Erst **Diese
Wegeprüfung bestätigen** bestätigt genau den angezeigten vorbereiteten Auftrag.
Keine dieser Aktionen startet eine Bewegung.

Die **Rahmenfahrt** ist eine eigene ausdrücklich ausgelöste laserfreie Bewegung mit
Rückkehr zum Ausgangspunkt. Auch ihr kompletter Weg einschließlich der gerundeten
Rückkehr wird vorab geprüft. Ein passender Bearbeitungsauftrag bedeutet deshalb
nicht automatisch, dass die gesonderte Rahmenfahrt verfügbar ist.

## Start, Pause und Ende

Unter **Gerätelüfter und Aufsicht** den beaufsichtigten Gerätebetrieb wählen und die
Anwesenheit für den konkreten Vorgang bestätigen. Eine externe Absaugung wird in
diesem Modus nicht gesteuert und nicht als Pflichtgerät abgefragt. Gehäuse- und
Lasermodullüfter beim ersten realen Auftrag tatsächlich beobachten; die Software
kann deren Lauf nicht rücklesen oder als gemessen bestätigen.

Erst **Auftrag ab angezeigter lokaler Lage starten** startet den vorbereiteten Auftrag.
Berechnung und Codierung erfolgen vor der Startfreigabe. Nach Pause verlangt
Fortsetzen einen eigenen bewussten Tastendruck und die jeweils nötigen aktuellen
Bestätigungen. **STOPP** bleibt erreichbar.

Nach dem Auftrag kann der Kopf am letzten Bearbeitungspunkt stehen. Ein weiterer
Auftrag ab **Aktueller Position** wird an dieser dann angezeigten lokalen Lage neu
geplant und benötigt eine neue Wegeprüfung und Bestätigung; eine automatische
Rückfahrt nach Auftragsende wird nicht eingeschaltet.

Nach erneutem Handverschieben, Reset, Trennung, Türereignis oder Verlust einer
benötigten Controllerbestätigung muss der lokale Bezug neu eingerichtet werden.
Ein Positionszähler erkennt ungemeldete Handbewegung nicht. Vor erneutem Verschieben
immer einen neuen Handpositionierungsablauf beginnen.

Das Schließen des Dialogs erhält einen bereits bestätigten lokalen Bezug, damit das
Motiv bearbeitet werden kann; eine unvollständige Aufnahme wird verworfen. **Handstart
verwerfen** beendet den lokalen Ablauf. Es stellt keine frühere globale Referenz
wieder her. Für absolute Ausgabe ist eine erneute reguläre Einrichtung nötig.

Die erste Variante umfasst lokale Ankerplatzierung, Rahmenfahrt, Auftrag, Pause,
Fortsetzen und STOPP. Globale Jog-/Nullpunktfunktionen, Benutzerursprung und aktive
Print-and-Cut-Registrierung gehören nicht zu diesem lokalen Ausführungspfad.

## Nachweise und Grenzen

[Restweg- und Drahtkoordinatenvertrag](MANUAL_TRAVEL_CHECK.md) beschreibt die
reinen Planprüfungen. UI-Tests und ein simuliertes Controllergerät prüfen
Softwareabläufe. Sie messen keine mechanischen Restwege, Motorfreigabe, Absaugung
oder reale Genauigkeit. Diese Beobachtungen bleiben am tatsächlichen Gerät nötig.
