# Nutzerwünsche und Zusatzvorschläge

Recherche: 09.11.2026. Grundlage sind konkrete Beiträge im LightBurn-Forum und auf der öffentlichen Wunschplattform. Die Auswahl zeigt wiederkehrende Arbeitsprobleme, ist aber keine repräsentative Befragung oder Rangliste. Ältere Wünsche werden mit der 2.2-Dokumentation abgeglichen. Bei Fider waren manche Direktaufrufe nur als JavaScript-Hinweis abrufbar; die inhaltliche Auswertung dieser Einträge stützt sich auf die vom Suchindex gelieferten Beitragsauszüge. Aktuelle Status-/Stimmenzahlen werden daraus nicht als gesichert übernommen.

## Was bereits zur Vergleichsbasis gehört

- **Mehrseitige Projekte:** langjähriger Wunsch, im [Beitrag 636](https://lightburn.fider.io/posts/636/multiple-tabs-inside-of-one-window) ausdrücklich für 2.2 als erledigt markiert. Auch [Beitrag 167](https://lightburn.fider.io/posts/167/multiple-pages-within-a-work-session) beschreibt den Bedarf an mehreren Platten eines zusammengehörigen Projekts. Deshalb Basisfunktion F02, kein exklusives Zusatzmerkmal.
- **Nesting, Mittellinien-Nachzeichnen, Kamera-Registrierung und REST-API:** gehören inzwischen zu [LightBurn 2.2](https://docs.lightburnsoftware.com/2.2/NewFeatures/). Werden im Zielkatalog berücksichtigt.
- **Undo-Verlauf, Vorlagen, Kameraunterstützung:** bereits in [2.1 angekündigt](https://forum.lightburnsoftware.com/t/lightburn-2-1-quick-nest-enhanced-camera-support-undo-history-and-more/189743). Autosave ist ebenfalls längst vorhanden; Wiederherstellung nicht als neue Erfindung darstellen.
- **Slot-/Laschenanpassung:** ältere [Wünsche zur Materialstärke](https://lightburn.fider.io/posts/876/be-able-to-adjust-tab-and-slot-assembly-based-on-actual-material-thickness) sind teilweise durch das heutige Werkzeug [Resize Slots & Tabs](https://docs.lightburnsoftware.com/2.2/Reference/ResizeSlots/) adressiert. Vollständige parametrische Konstruktion ist ein weitergehender Vorschlag.

## Priorisierte Zusatzvorschläge

Die folgenden Punkte sind Empfehlungen für Blazer Studio und noch keine bestätigten zusätzlichen Produktanforderungen. Eine Priorität bedeutet Nutzen für unseren Workflow, nicht eine gemessene Rangfolge der LightBurn-Community.

| ID | Beobachteter Bedarf und Quelle | Vorschlag für Blazer Studio | Empfehlung |
|---|---|---|---|
| Z01 | [Variablen und Konstanten](https://forum.lightburnsoftware.com/t/variables-or-constants-feature-or-future/177381): Materialstärke einmal ändern und abhängige Maße nachziehen | Projektvariablen mit Einheiten, Formeln und Abhängigkeiten; zuerst parametrisierte Maße/Generatoren, vollständiger Zwangsbedingungslöser später prüfen | Hoch; Datenmodell früh vorbereiten |
| Z02 | [Gruppenbearbeitung](https://lightburn.fider.io/posts/318/i-would-like-to-see-that-you-could-edit-groups), erneut [April 2026 diskutiert](https://www.reddit.com/r/LightBurn/comments/1suyjvs/groups_of_groups_break_lightburn/) | In Gruppen hineingehen, einzelne Elemente bearbeiten und Hierarchie beibehalten; Filter und Pfadoperationen mit klarer Auswahlsemantik | Hoch |
| Z03 | [Materialbibliothek erweitern](https://lightburn.fider.io/posts/3466/expand-material-library): Laser, Linse, Fokus und mehr Platz für Hinweise | Materialprofile mit Maschine, Optik, Fokus, Dicke, Quelle, Notizen, Ergebnisfotos und Version; Abweichungen zum Profil anzeigen | Hoch |
| Z04 | [Mehrere Bibliotheken](https://lightburn.fider.io/posts/3114/multiple-material-library) und [Unteroperationen](https://lightburn.fider.io/posts/4100/access-the-material-library-for-sub-layers) | Bibliotheken gleichzeitig durchsuchen und Profile direkt Unteroperationen zuordnen; keine blockierende Modalkette | Hoch |
| Z05 | [Bildmaßstab kalibrieren](https://lightburn.fider.io/posts/3253/calibrate-images-to-size) | Zwei Bildpunkte markieren und bekannte Strecke eingeben; Maßstab und später Perspektive getrennt behandeln | Hoch; kompakt umsetzbar |
| Z06 | [Jobwarteschlange](https://lightburn.fider.io/posts/691/put-job-in-queue) für wiederholte Produktionsarbeiten | Eingefrorene Jobstände sammeln, währenddessen weiterzeichnen; nächster Auftrag nach Werkstückwechsel bewusst freigeben | Mittel |
| Z07 | [Verbesserungsvorschläge](https://lightburn.fider.io/posts/4324/several-ideas-for-improving-lightburn): unerwartet übernommene Ebenenwerte, bessere Simulation und reversible Bearbeitung | Explizite Herkunft jeder Einstellung, „auf Profil zurücksetzen“, nicht destruktive Effekte und Schnittfugenansicht | Hoch |
| Z08 | [Parametrische Maße](https://forum.lightburnsoftware.com/t/variables-or-constants-feature-or-future/177381) und [Materialstärkenwechsel](https://lightburn.fider.io/posts/876/be-able-to-adjust-tab-and-slot-assembly-based-on-actual-material-thickness) | Lokale Generatoren für Boxen, Fingerzinken, Schilder und Schablonen mit Vorschau | Mittel; auf Z01 aufbauen |

Die Aussage aus dem Parametrik-Thread, dass dies nicht auf der damaligen LightBurn-Roadmap stand, stammt aus August 2025. Das ist keine Behauptung über unveröffentlichte Pläne im November 2026. Ebenso beweist ein älterer offener Wunsch nicht, dass jede Teilfunktion in 2.2 fehlt.

## Eigene ergänzende Empfehlungen

Diese Vorschläge sind aus unserem BlazerX-/Linux-Projekt abgeleitet, nicht als Umfrageergebnis auszugeben:

- **Z09 – Prüfbericht vor Ausgabe:** offene Füllkonturen, doppelte Schnitte, Arbeitsraumüberschreitung, falsche Einheiten und Überlauf markieren und direkt zum betroffenen Objekt springen. LightBurn hat bereits Geometrieprüfungen; unser Zusatz liegt im zusammenhängenden, erklärbaren Bericht.
- **Z10 – Material-Testbuch:** Testfelder, Parameter, Foto und Bewertung zusammen speichern und daraus ein nachvollziehbares Profil ableiten. Verknüpft Z03 mit den Materialtests aus der Basis.
- **Z11 – Projektarchiv:** Quelldateien, eingebettete Bilder, Profilstände und Kalibrierungsreferenzen als reproduzierbaren Auftrag sichern; Schriften beziehungsweise Ersatzschriften transparent behandeln.
- **Z12 – Native Linux-Qualität:** Wayland/X11, HiDPI, deutsche Dezimalkommas, stabile USB-Gerätezuordnung und verständliche Verbindungsdiagnose gezielt abnehmen. [Linux-Nutzer diskutieren weiterhin den entfallenen Support](https://forum.lightburnsoftware.com/t/open-source-linux-packaging-and-build-system/179160).
- **Z13 – Restmaterial verwalten:** nutzbare Restflächen und Sperrbereiche speichern und beim Nesting berücksichtigen; Erweiterung der vorhandenen Nesting-Idee, keine Behauptung, Materialrichtung sei bei LightBurn neu oder unmöglich.

## Architekturfolgen und Reihenfolge

Stabile Objekt-IDs, eine echte Gruppenhierarchie, Einheiten, mehrseitige Projekte, versionierte Profile und ein vom Editor unabhängiger Jobstand gehören früh ins Datenmodell. Für Formeln und spätere nicht destruktive Bearbeitung Erweiterungspunkte vorsehen, ohne gleich ein vollständiges CAD-System zu bauen.

Z02–Z05, Z07 und Z12 passen eng zur ersten alltagstauglichen Anwendung. Z01 früh entwerfen, seine Umsetzung aber gestuft planen. Warteschlange und Generatoren folgen auf zuverlässige Einzeljobs. Aus dieser Wunschliste folgt keine automatische Laseraktivierung oder Wiederaufnahme nach Verbindungsabbruch.
