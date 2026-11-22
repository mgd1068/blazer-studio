# Beaufsichtigter Gerätezyklus

Dieser Modus betrifft den normalen M3-Gerätebetrieb. Eine externe Absaugungsanlage
wird darin nicht gesteuert, abgefragt oder als Pflichtsensor vorausgesetzt. Der
bisherige Sensor-/externe Anlagenmodus bleibt eine getrennte ausdrückliche Option.

## Bearbeitung und Gerätelüfter

Der GRBL-Adapter beginnt mit M5/S0 und einer laserfreien Anfahrt. Ab der ersten
Bearbeitung bleibt M4 über interne G0-Leerfahrten ausgewählt. Der letzte S-Wert
bleibt modal erhalten; Änderungen werden am jeweiligen G1-Block ausgegeben.
Der unverändert vorausgesetzte Lasermodus `$32=1` deaktiviert die Laseremission
während G0. Das vermeidet zusätzliche M5/M4-Umschaltungen und Plannerstopps
zwischen Bearbeitungsabschnitten. Am regulären Ende bleibt M5/S0 vorgesehen.

[GRBL beschreibt diesen dynamischen Ablauf ausdrücklich](https://github.com/gnea/grbl/wiki/Grbl-v1.1-Laser-Mode).
Unabhängige virtuelle Plant-Tests kontrollieren, dass keine G0-Bewegung emittiert,
die G1-Leistungen stimmen, die erste Anfahrt ausgeschaltet und das Ende M5/S0 ist.
Die vollständige lokale Raw-/Wire-Restwegprüfung bleibt unverändert erforderlich.

Die reale Wirkung dieses Ablaufs auf Gehäuselüfter und Lasermodulkühlung ist
nicht separat rücklesbar. Die Oberfläche zeigt beide Rollen als unbekannt und
bietet keine erfundenen getrennten Lüfterknöpfe an. M8 ist nicht als Fan-Schalter
bewiesen; Hersteller-Türschutzparameter werden nicht als Lüfterregler verwendet.
Auch tatsächlicher Nachlauf nach M5/STOP ist nicht als geprüft ausgegeben.
Beide Lüfter müssen beim ersten realen Materialversuch beobachtet werden.

## Anwesenheit und Zustandsregeln

Nach Einrichtung unter **Gerätelüfter und Aufsicht** den beaufsichtigten
Gerätebetrieb wählen. Nach einer Störung ist zunächst der tatsächliche sichere
Gerätezustand ausdrücklich zu klären. Für Start, Fokus oder Fortsetzen wird
jeweils nur die tatsächliche eigene Anwesenheit am Gerät bestätigt. Die
Bestätigung ist einmalig, höchstens 30 Sekunden gültig und an Sitzung, Quelle,
Aktion und Koordinatenrahmen gebunden. Sie startet selbst weder Fahrt noch Laser.
Die jeweilige ausdrücklich ausgelöste native Gerätetaste bleibt erforderlich.

Es werden keine externen Flow-/Mappingwerte und keine Lüfter-Laufwerte erzeugt.
Während eines gestarteten Vorgangs bleibt die normale aktive Betriebsbindung
bestehen; Pause, Widerruf, Fehler, Trennung, STOPP und Zeitkontinuitätsverlust
behalten ihre Zustandsregeln. Vor Fortsetzen erneut Anwesenheit bestätigen.
Beaufsichtigung beenden fordert STOPP an. Die Anwendung kann bei verlorenem
Transport keine physische Abschaltung oder einen Lüfterzustand garantieren.

Türschutz, Maschinen-/lokale Referenz, Restwege, frische Controllerdaten und
native Startfreigabe werden durch diesen Modus nicht ersetzt oder gelockert.
