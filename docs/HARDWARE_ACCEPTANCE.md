# Geräte und Materialabnahme

Stand: 12.11.2026. Diese Ergebnisse betreffen den vorhandenen Blaze X M3 mit nominellem 5-W-Modul. Sie sind keine pauschale Freigabe des Herstellerkatalogs. Die Hardwareversuche fanden vor den anschließenden Änderungen an der Kommunikation statt; dieser neue Treiberstand ist erneut beaufsichtigt am Gerät abzunehmen.

## Aufbau und Aussagekraft

Ausgelesen wurden Grbl-ESP32 1.3a, 115200 Baud, 200 × 150 mm, Millimeter, G54, `$30=1000`, `$31=0`, `$32=1` und aktiviertes Homing. Der Fokus wird manuell eingestellt und war laut Bediener eingestellt. Das Gehäuse war geschlossen und eine externe Absaugung vorhanden. Ein späterer USB-Ausfall wurde durch den PC-Wechsel am KVM erklärt. Ein unabhängig schaltbarer Lüfterbefehl ist damit nicht nachgewiesen.

Die Versuche verwendeten C++-Prüfprogramme mit Blazer-Dokument-/Jobkern und einem protokollierenden Geräteadapter. Sie bestätigen nicht den vollständigen Bedienungsablauf der normalen grafischen Anwendung. Rohprotokolle mit Gerätekennungen verbleiben außerhalb des öffentlichen Repositorys.

Das abschließend verwendete Kraftpapier war 80 × 80 mm groß; Grammatur und Dicke sind unbekannt. Die Herstellerreferenz nennt **M3 Pro**, nicht M3. Leistung wird als digitaler S-Sollwert beurteilt. Eine optische Wattmessung fand nicht statt.

## Kraftpapier Rasterprobe

Je Feld 2 × 2 mm, 0,10 mm Zeilenabstand, ein Durchgang und 7 mm laserfreier Vor-/Nachlauf. Der eine Durchgang ist eine eigene Testentscheidung: Die Herstellerquelle nennt keine Gravur-Durchgangszahl. Nummern und die zusätzliche Kennzeichnung wurden als Konturen bei 25 % und 375 mm/min ausgegeben.

| Vorschub | 50 % | 75 % | 100 % |
|---|---|---|---|
| 4000 mm/min | Feld 1 | Feld 2 | Feld 3 |
| 5000 mm/min | Feld 4 | Feld 5 | Feld 6b |
| 6000 mm/min | Feld 7 | Feld 8 | Feld 9 |

Alle neun Felder wurden einmal gefahren und waren sichtbar. Feld 6a ist eine zusätzliche frühere Probe bei nominell 100 %/5000 mm/min mit den alten getrennten Leistungsbefehlen. Feld 6b verwendet Inline-S-Leistungswechsel. Nur diese 100 %/5000/0,10-mm-Kombination entspricht der Kraftpapier-Gravurzeile der Herstellerliste.

Bei 4000 und 5000 mm/min liegen gemeinsame volle Vorschub-/S-Rückmeldungen vor. Für 6b wurden acht Meldungen mit F5000/S1000 erfasst. Bei 6000 mm/min sind voller Vorschub und voller S-Wert in getrennten Meldungen erfasst; die kurzen Arbeitsstrecken und zeitlich versetzten Rückmeldungen belegen dort nicht unabhängig die Gleichzeitigkeit.

**Physische Gegenlichtprüfung:** Nach Bedienerangabe sind sämtliche Zahlen sowie Felder 3, 6a und 6b gegen eine Lampe transparent. Für die übrigen Felder wurde keine Gegenlichtbewertung angegeben. Transparenz zeigt erhebliche Abtragung oder Ausdünnung; sie ist ohne weitere Untersuchung kein vermessenes Loch und kein Nachweis einer geeigneten reinen Oberflächengravur. Aus dem dunklen Kamerabild allein war diese Qualität nicht erkennbar.

## Rechteckiger Schnittversuch

Ein 30 × 40-mm-Rechteck um die Rasterprobe wurde einmal mit M4, 100 % und 1000 mm/min gefahren. Dieser Wert ist ein eigener Versuch: Der Herstellerkatalog enthält keinen Kraftpapier-Schnittwert. Der gesamte Umfang von 140 mm wurde abgearbeitet; anschließend wurden Idle, FS0,0, M5/S0 und Parkposition X0/Y0 bestätigt.

Nach Bedienerprüfung war der Schnitt ungefähr zu 90 % vollständig; eine Ecke und Teile gerader Strecken hingen noch zusammen. Leichter Druck reichte zum Trennen. Diese Prozentangabe ist eine qualitative Einschätzung, keine gemessene Schnitttiefe oder Schnittlänge.

**Abnahmeurteil:** vollständiger Laserschnitt nicht bestanden. Die mechanisch unterstützte Trennung darf nicht als erfolgreiche Durchtrennung gelten. Das Ergebnis ist nicht auf Holz oder andere Dicken übertragbar. Das getrennte Papier ist keine unveränderte Ausgangslage für eine automatische Wiederholung.

## Stand der Geräteabnahme

| Fall | Nachweis | Urteil |
|---|---|---|
| H01 Konfiguration und Leistungsskala | Gerätekonfiguration und digitale S20/S100/S500/S1000 geprüft | Historisch bestätigt; neuer Treiberstand am Gerät offen |
| H02 Laserfreie Positionierung | Homing, Begrenzungsfahrten und 20-mm-Referenzquadrat | Bewegungs-/Statusnachweis vorhanden; unabhängige Maßtoleranz offen |
| H03 Materialwirkung | Konturen und Raster auf Kraftpapier, Gegenlichtbefund | Wirkung bestätigt; reine Oberflächenqualität nicht abgenommen |
| H04 Pause, Stop und Verbindungsverlust | PTY-Regression; beobachtete KVM-Trennung und manuelle Erholung | Kontrollierte reale Betriebsabnahme offen |
| H05 Vollständiger Schnitt und Relief | Fast durchgehender Papierschnitt; kein allgemeiner Reliefversuch | Schnitt nicht bestanden; Relief nicht durchgeführt |
| H06 Normaler App-Ablauf am Gerät | Hardwareprüfprogramme; separate UI-/Simulatorprüfungen | Reale gemeinsame Abnahme offen |
| H07 Kamera | Feste FHD-Webcam, externe Aufnahmen und vorläufige Ebenenzuordnung | Kein abgenommenes integriertes Live-Kamerasystem |

## Fortsetzung mit Bediener

Für jeden neuen Durchgang werden Material, Dicke beziehungsweise Grammatur, Fokus, genaue App-Revision, Bearbeitung, Leistung, Vorschub, Rasterabstand und Durchgänge eingetragen. Herstellerwerte und eigene Ergänzungen bleiben unterscheidbar. Die vollständige Trennung und die Gegenlichtprüfung werden am Werkstück beurteilt; eine sichtbare Kontur im Kamerabild genügt nicht.

Die ersten Betriebsfälle mit dem neuen Treiber werden ohne Emission geprüft. Eine echte Wiederverbindung erfordert eine neue Konfigurations-/Statusprüfung; ein alter Auftrag wird nicht automatisch fortgesetzt. Schutzkontaktprüfungen richten sich nach der Geräteanleitung und umgehen keine Sperre. Während der Abwesenheit des Bedieners finden keine Geräteaktionen statt.

Softwarefehler werden mit reproduzierbarem Fall behoben; erneute Materialproben sind gesonderte beaufsichtigte Tests. Der [Plan zur Produktionsreife](PRODUCTION_READINESS.md) führt die verbleibenden Abnahmen.
