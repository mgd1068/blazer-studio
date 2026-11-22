# ManualTravelCheck: geometrischer Restwegvertrag

Reiner Offline-Baustein für einen bereits vorliegenden `JobPlan`. Keine Geräteabfrage,
Bewegung, Planänderung oder physische Freigabe. Er erfindet weder Bedienermessungen
noch einen realen Ursprung nach Handverschiebung. Die Herkunft und Aktualität der
Restwege sowie alle separaten Maschinen-/Ausgabegrants bleiben Aufgabe des Aufrufers.

## API

`ManualTravelCheck::sourceFingerprint(plan, cancel)` liefert einen binären SHA256-Wert
(32 Bytes), gebunden an alle Bewegungsfelder und das unveränderliche Ausgabeprofil.
Ungültige, abgebrochene oder unzulässig große Pläne liefern einen leeren Wert.

`Request` enthält ausdrücklich:

- `headAnchor`: optionaler Punkt **in virtuellen Plankoordinaten**, kein physischer
  Maschinenpositionsbeleg. Weglassen bedeutet unbekannt, nicht 0/0.
- `available`: vier bekannte freie Wege in mm, `negativeX/positiveX/negativeY/positiveY`.
  Jeder Wert bezeichnet den freien Weg vom aktuellen Kopf bis zur tatsächlich
  bestimmten mechanischen oder bewusst enger gewählten Grenze. Nicht ausgefüllte
  Einzelwerte muss der Aufrufer als unbekannt/NaN behandeln, nicht als erratene Zahlen.
- `safetyMarginMm`: ausdrücklich gesetzt, auch wenn null. Pro Richtung wird diese
  Reserve vom freien Weg abgezogen. Sie gilt auch für einen unbewegten Kopf:
  bei Reserve 2 mm und nur 1 mm Abstand zur −Y-Grenze scheitert die Prüfung selbst
  dann, wenn keine Y-Bewegung geplant ist.
- `expectedSourceFingerprint`: exakter zuvor erfasster Quellplan-Fingerabdruck.

`check(plan, request, cancel)` liefert einen privaten, unveränderlich lesbaren
`Result`: `valid/error/hasRequirements`, `required/available/usableAfterMargin`, Sicherheitsabstand
sowie Quell- und Request-Fingerabdruck. `matches(plan, request, cancel)` muss beim
Verbrauch erneut geprüft werden; Änderungen an Geometrie, Feed/Power, Profil,
Kopfanker, einem Restweg oder der Reserve machen das alte Ergebnis unbrauchbar.
Ein passendes Ergebnis ist weiterhin nur Planungsdaten, niemals eine Ausgabefreigabe.
Richtungsbedarf darf nur bei `hasRequirements()` angezeigt werden; bei fehlerhaften
Eingaben oder Abbruch sind die erforderlichen Wege noch nicht bestimmt.

## Geometrie und Grenzen

Der allererste `from`-Punkt muss komponentenexakt dem expliziten Kopfanker entsprechen.
Alle nachfolgenden Segmente müssen komponentenexakt anschließen. Fehlende Anfahrten
werden nicht ergänzt, nominelle 0→Start-Fahrten nicht herausgefiltert. Jedes `from`
und `to` zählt, einschließlich ausgeschalteter Anfahrten/Leerfahrten, Rasterüberlauf
und einer gegebenenfalls bereits im Plan vorhandenen Rückfahrt. Es wird keine neue
Rückfahrt hinzugefügt. Für gerade XY-Segmente liegen die Achsextrema an den Endpunkten.

Für jeden Endpunkt ist `local = point - headAnchor`. Die erforderlichen vier
Richtungswege enthalten sowohl diese unveränderten Rohwerte als auch die unabhängig
auf 0,001 mm formatierten **lokalen ABS-Koordinaten** mittels `GrblWireNumbers`.
Es wird weder der Kopfanker gerundet noch `wire(point)-wire(headAnchor)` gerechnet.
G91-Schrittrundungen werden nicht aufsummiert. Eine Rohgrenze darf auch dann nicht
überschritten werden, wenn ihre gerundete Ausgabe nach innen fällt.

`required <= available - safetyMarginMm` muss für alle vier Richtungen gelten;
Gleichheit ist zulässig. Ein negativer nutzbarer Restweg bleibt sichtbar und führt
zur Ablehnung. Fehler nennen jede überschrittene Richtung und die betreffenden Werte.

Budget: maximal 500.000 Bewegungen; endliche virtuelle Koordinaten und lokale Wege
bis ±2000 mm; endliche freie Wege und Reserve zwischen 0 und 2000 mm. Unbekannte,
negative oder nicht endliche Angaben, nicht unterstützte Ausgabeprofile, fehlerhafte
Bewegungsparameter und Cancellation werden abgewiesen. Die virtuellen Koordinaten
sind ausdrücklich **nicht** an die vermeintliche physische Profil-Bettposition gebunden.

Zusätzliche konservative Plausibilitätsgrenze: die Summe gegenüberliegender freier
X-Wege darf die nominelle Profilbreite nicht überschreiten, die Y-Summe nicht dessen
Höhe. Das ist nur eine Eingabekontrolle, kein Nachweis tatsächlicher Achswege oder
Kopfposition. Bei größeren tatsächlichen mechanischen Wegen können ausdrücklich
bekannte kleinere sichere Untergrenzen verwendet werden; das Profil allein liefert
niemals einen Restweg. Die Messdatenquelle und ein späterer Hardwareablauf sind
nicht Bestandteil dieses Bausteins.

## Grenze gegenüber späteren Ausgabeadaptern

Diese Prüfung beschreibt **lokale** ABS-Koordinaten. Ein späterer Adapter, der den
virtuellen Kopf intern um einen positiven G54-Betrag verschiebt, muss zusätzlich
seine tatsächlich formatierten Zielkoordinaten relativ zum bestätigten Kopf gegen
alle Restwege prüfen. `wire(head + offset) - head` bzw. `wire(head)` ist nicht
allgemein `wire(offset)`; die Rundungsphase kann sich ändern. Beispiel: ein
verschobener Kopf bei 0,0004 mm und lokaler Offset 0,0044 mm kann ein globales
Ausgabeziel 0,005 mm erzeugen. Dieses Helper-Ergebnis bestätigt einen solchen
verschobenen Encoder deshalb nicht. Die Vereinigung aus Roh- und tatsächlichen
Wirewegen des späteren Adapters bleibt dessen separater autoritativer Vertrag.
