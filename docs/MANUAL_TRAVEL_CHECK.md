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

## Unveränderliche tatsächliche Ausgabe: PreparedLocalJob

`PreparedLocalJob::prepare(plan, request, cancel)` ergänzt den reinen Baustein um
`PreparedControllerJob` und eine enge Prüfung seiner **tatsächlich ausgegebenen**
GCode-Ziele. Der Eingabeplan ist bereits vollständig in virtuellen G54-Koordinaten
platziert, einschließlich des gewählten Current-Position-Ankers. Der Aufrufer
liefert als `headAnchor` den frisch nach seiner G10-Transaktion bestätigten WPos;
eine eingegebene Wunschposition ist kein Ersatz. Das erste `from` muss exakt passen.
Dieser Helper kann Herkunft und physische Richtigkeit dieser Daten nicht beweisen.
Er platziert nichts erneut, verändert kein Profil und ergänzt keine Fahrten.

Die erste Beta verlangt exakt **2 mm Reserve je Richtung**. Der Bedarf ist die
konservative Vereinigung aus allen rohen `from/to`-Punkten, der bisherigen lokalen
ABS-Rundungsprüfung und `parsedAbsoluteWireTarget - confirmedHead`. Auch erste
laserfreie Anfahrt, Overscan und eine bereits im Plan vorhandene Rückfahrt zählen.
Ein Ziel bei 0,0048 mm wird beispielsweise als 0,005 ausgegeben: bei bestätigtem
Kopf 0,0004 beträgt dessen tatsächlicher Abstand 0,0046 mm, obwohl die rohe lokale
Differenz nur 0,0044 mm ist. Eine verfügbare Strecke von 2,0045 mm reicht mit Reserve
also nicht. Weder Kopf noch Differenz werden zum Bestehen der Prüfung gerundet.

Die private Wireprüfung akzeptiert ausschließlich den bekannten aktuellen
GRBL-XY-Encoder mit G21/G90/G54/G17/G94, laserfreier erster G0-Anfahrt und expliziten
G0/G1-XY-Zielen. Andere Modi, Teilziele, G53, G91, Z, unbekannte Wörter oder
überschrittene Zeilen-/Bewegungsbudgets werden abgelehnt. Bekannte Kommentare und
das abschließende zielose `G0` sind keine Bewegungen. Ein neuer Encoder benötigt
eine neue ausdrücklich geprüfte Anpassung, keinen permissiven GCode-Import.

Das Ergebnis besitzt nur const-Lesezugriff: `prepared()` liefert ausschließlich
bei Erfolg den unveränderten Streaming-Snapshot, sonst `nullptr`. Getrennt lesbar
sind Roh-, tatsächlicher Wire- und vereinigter Richtungsbedarf, verfügbare/nutzbare
Wege, Roh-/Wirebounds, erste Anfahrt und `firstNonzeroMotionLineIndex()` (nullbasiert
in `prepared()->lines()`, −1 ohne Zieländerung). Dieser Index beschreibt allein
eine Anforderung, keinen Nachweis erfolgter Bewegung oder einer Zeitgarantie.

`sourceFingerprint()` und `profileFingerprint()` entsprechen dem vorbereiteten
Controllerauftrag; `manualTravelSourceFingerprint()` bindet beide gemeinsam.
`requestFingerprint()` bindet alle Restweg-/Kopfdaten samt Quelle.
`localFrameFingerprint()` bezeichnet **nur die Messdatenidentität**, keine
bestätigte Geräte-Frame-Autorität. Der Worker muss zusätzlich seine eigene frische
Session-/Frame-Nonce, passende Koordinaten, Bedienabsicht und Absaugung binden.
`matches(plan, request, cancel)` prüft denselben unveränderten Quellplan und Request
außerhalb des Echtzeitpfads. Keine dieser Hashes erlaubt reale Bewegung oder setzt
die globale Maschinenreferenz auf bekannt.

### Separat vorbereitete Rahmenfahrt

`frameLines()` enthält bereits vor einer Rahmenbereitschaft das vollständige
unveränderliche Ausgabeprogramm: M5/S0, G21/G90/G54, sechs explizite G1-Ziele mit
S0/F600 (vier Ecken der tatsächlichen Wire-Boundingbox, Schluss und Rückkehr zum
Kopfziel), abschließend M5/S0. Auch Anfahrt und Rückkehr werden mit Roh-, lokaler
und tatsächlicher absoluter Wirequantisierung gegen dieselben vier nutzbaren
Restwege geprüft; `frameRequiredUnion()` zeigt diese konservative Vereinigung.
Bei einem bestätigten Kopf von 10,0004 mm kann die Rückkehr als X10,000 eine neue
negative Strecke von 0,0004 mm anfordern. Diese wird nicht als exakt identische
Kopfposition dargestellt oder verworfen.

Rahmenfreigabe ist unabhängig: Nur ein gültiger Auftrag mit nicht leeren
`frameLines()` hat ein vorbereitetes Rahmenprogramm. `frameError()` erklärt eine
Ablehnung; ein ausschließlich zusätzlicher Rahmenfehler entwertet den Auftrag
nicht. `frameFingerprint()` bindet exakt diese Zeilen an Job-, Request- und
Messdatenidentität. Der Worker darf die Zeilen nicht erst bei Start neu erzeugen;
seine eigene Session-/Frame-/Bedienbindung bleibt weiterhin erforderlich. Ein
vollständig degenerierter Rahmen ist Daten mit unveränderten Zielen, kein
Nachweis tatsächlich erfolgter Bewegung.
