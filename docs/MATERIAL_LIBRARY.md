# Materialbibliothek

Links unter **Material** ein Profil auswählen. Dadurch werden Gravur- und Schnittparameter samt Ausgabeschaltern ins aktuelle Projekt übernommen. **Bearbeiten** öffnet Namen, Maschinenmodell, Modulleistung, Materialstärke, Geschwindigkeit, Leistung, Durchgänge, Gravurintervall und Notizen. Dezimalkomma ist erlaubt.

**Profil speichern & übernehmen** speichert die Bibliothek und aktualisiert das Projekt. **Abbrechen** verwirft den Entwurf. **+** legt eine eigene Kopie an; im Menü **⋯** lassen sich Profile duplizieren und nach Bestätigung löschen. Auch eine vollständig geleerte Bibliothek erlaubt neue Profile.

Ausgeliefert werden ein deutlich gekennzeichnetes ungetestetes Beispiel sowie zwei bearbeitbare Kopien der Hersteller-Schnittwerte für M3 Pro / 5 W / Sperrholz 3 und 5 mm. Bei diesen Kopien ist Gravur deaktiviert; die Gravurfelder enthalten eigene Entwurfswerte. Die Herstellerseite nennt M3 Pro, das eingerichtete Gerät M3. Die Oberfläche zeigt diese offene Zuordnung an. Die vollständigen 64 Quellzeilen bleiben separat unter **Herstellerwerte ansehen** erhalten.

Die Bibliothek liegt im Qt-Anwendungsdatenverzeichnis, auf diesem Rechner normalerweise `~/.local/share/BlazerStudio/Blazer Studio/materials.json`. Speicherung erfolgt beim Ändern atomar. Bei einer beschädigten oder unlesbaren Datei zeigt die App eine Meldung und überschreibt das Original nicht. Schreibfehler verändern den vorherigen Bibliotheksstand nicht.

Ein Projekt speichert eine eigene Kopie des angewendeten Profils. Spätere Bibliotheksänderungen oder Löschen verändern diesen Stand nicht automatisch; erneutes Auswählen übernimmt den aktuellen Bibliotheksstand. Individuelle Bearbeitungsänderungen werden als Abweichung angezeigt. Die Profilübernahme ist im Projekt rückgängig zu machen; Bibliotheksänderungen selbst haben keinen Undo-Verlauf.

Projektformat 2 verwendet Millimeter und Ursprung unten links. Alte Projektdateien der Version 1 werden beim Öffnen umgerechnet und beim nächsten Speichern als Version 2 geschrieben. Profilrevisionen werden mitgeführt; eine vollständige Versionsverwaltung, Fotos, Suchfilter, Import/Export fremder Bibliotheken und Materialtestgeneratoren bleiben weitere Ausbauschritte.

![Materialprofil bearbeiten](../assets/material-editor.png)
