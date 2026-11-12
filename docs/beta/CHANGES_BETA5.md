# Änderungen im Softwarekandidaten beta.5

Zielplattform bleibt Ubuntu 26.04 amd64. Der Kandidat ist keine öffentliche Gerätebeta und keine 1.0-Freigabe. Die genaue Revision, Version und Laufzeitabhängigkeiten nennt `BUILD-MANIFEST.json`, Prüfsummen stehen in `SHA256SUMS`. Dieser Stand benötigt seinen eigenen Installations-/Update-/Entfernungsnachweis; frühere Kandidatennachweise werden nicht übernommen.

## Hinweis bei jedem normalen Start

Deutsch und Englisch erscheinen gleichzeitig vor Einführung oder Wiederherstellung, auch bei bereits abgeschlossenem Erststart. Der Hinweis darf nicht dauerhaft ausgeblendet werden. Escape und Außenklick schließen ihn nicht; **Verstanden / I understand** bestätigt für diesen Start. Hardwaregesperrte nichtinteraktive CLI-Modi werden nicht durch einen Dialog blockiert.

**Laser niemals unbeaufsichtigt betreiben / Never operate the laser unattended.** Verbindungsabriss oder hängender Controller kann den Laser eingeschaltet lassen. Software kann Laser-Aus dann nicht garantieren. Während der gesamten Bearbeitung am Gerät bleiben und zur direkten Stromunterbrechung bereit sein. Die Bestätigung ersetzt weder dauernde Aufsicht noch geräteseitige Schutzmaßnahmen.

## Verbindung und Ressourcenabbau

`Machine` verwendet eine gemeinsame Controller-Verbindungsschnittstelle für Status und Bedienung. Bisher bleibt ausschließlich serielles GRBL implementiert; reale Verbindungen bleiben auf den bisherigen M3-Ausgabevertrag begrenzt. Unbekannte Controller, Transporte und ungültige Parameter erhalten keinen Ersatztreiber.

Der konkrete GRBL-Adapter führt auch direkte Sitzungszerstörung vor dem Ressourcenabbau durch die bestehende Schließlogik: Leerlauf M5 S0, aktiver/pausierter Auftrag oder Fokus Hold/Soft-Reset. Bereits getrennte Verbindungen senden nicht erneut; Rückrufe aus dem sterbenden Objekt werden unterdrückt. Dies bleibt ein bestmöglicher Sendeversuch und beweist nach Kommunikationsverlust keinen physischen Aus-Zustand.

Der [beta.4-Kandidat](CANDIDATE_BETA4.md) enthält diese nachträgliche Korrektur nicht und bleibt unverändert. Auftragsgeometrie, M3-Ausgabe, Herstellerprofile und Projektformate 8–11 bleiben wie in den [Änderungen zu beta.4](CHANGES_BETA4.md) beschrieben. Kein allgemeiner Leistungs-Offset.

## Verbleibende Freigaben

Reale Betriebs-/USB-/KVM-/Schutzkontaktprüfungen, vollständiger Schnitt, Relief, Werkstückqualität und beobachteter Einsteigerlauf bleiben offen. Weitere Controller, Geräteverwaltung und Modellabnahmen sowie der vollständige Funktionsumfang bleiben im [Produktionsplan](../PRODUCTION_READINESS.md) und [Mehrgeräteplan](../MULTI_DEVICE_PLAN.md) erhalten. Siehe [Testprotokoll](TEST_PROTOCOL.md), [bekannte Grenzen](KNOWN_LIMITATIONS.md), [Paketprüfung](RELEASE_BUILD.md).
