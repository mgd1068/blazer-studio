# Beaufsichtigter Gerätezyklus ohne behauptete externe Absaugung

`OperatorSupervisedDeviceCycle` ist ein ausdrücklich gewählter Presence-only-Modus.
Vor Start, Fokus und jedem Fortsetzen bestätigt die Bedienperson ihre tatsächliche
Anwesenheit. Der Worker stellt einen einmaligen 30-s-Witness aus, gebunden an
Session, Quelle/Profil, Koordinatenepoche und konkreten Vorgang; erst die getrennte
native Geräteaktion kann beginnen. Der Modus setzt keine Maschinenreferenz,
öffnet keine Türverriegelung und verändert keine Bewegungs-/Leistungsgrenzen.

Machine-API: `selectSupervisedDeviceCycle()`, `attestDeviceCycle(action,present)`
mit `start/focus/resume`, `revokeDeviceAttendance()`. Der Pfad ruft niemals
`attestExternalExtraction(true)` auf und verlangt kein vorhandenes externes
Pflichtgerät. Der bisherige Sensor- und externe Operator-Modus bleiben als
getrennte, ausdrücklich gewählte Möglichkeiten erhalten. Ein externes Witness
kann den neuen Modus nicht bestätigen und umgekehrt.

Die readonly `extractionSupervision`-Map enthält weiter `pendingAction`,
`remainingMs`, `activeLease`, `notice/reason`; neu `presenceOnly`,
`requiresExternalExtraction`, `clearanceRequired` und `deviceFans`. Letzteres enthält
zwei getrennte Rollen `enclosureExhaust` und `laserModuleCooling`, jeweils
`state=unknown`, `feedback=notReadBack`, `independentControl=false`. Es gibt kein
`running=true`, keine Fake-Flowing-Samples und keinen unabhängig schaltbaren
Fan-On-Knopf. Die gesendeten M4/M5-Befehle beschreiben den Gerätezyklus; sie beweisen
keinen tatsächlichen Lüfterlauf. Die realen Lüfter sind beim ersten Materialtest
am Gerät zu beobachten. Air Assist bleibt eine eigene Zubehörfunktion.

Während eines laufenden Vorgangs hält der Worker dieselbe aktive Job-/Focuslease
und Belegung wie zuvor. Pause entwertet die alte Bestätigung; Fortsetzen benötigt
den zugehörigen pausierten Auftrag und eine neue Anwesenheitsbestätigung. Ende und
STOPP verwerfen Witnesses; Fault/Disconnect verwerfen zusätzlich die gültige
Session-/Zustandsgrundlage. Nach einer Störung muss der wirkliche Gerätezustand
geklärt und die nötige Einrichtung erneuert werden. `confirmExtractionClearance`
hat dabei im neuen Modus die ehrliche generische Bedeutung „Sicheren Zustand am
Gerät menschlich geklärt“, keine externe Fume-/Anlagenbestätigung. Nach normalem
Auftragsende erzwingt dieser Modus keine externe Rauchfrei- oder Abschaltfreigabe.
Keine dieser Aktionen sendet einen geratenen M8/M9-Befehl. Live-Overrides bleiben
im beaufsichtigten Modus gesperrt.
