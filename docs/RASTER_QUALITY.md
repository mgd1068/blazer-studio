# Rasterqualität und Messung – erster Ausbau 0.7

Bilder und Reliefkarten behalten beim Import ihre ursprüngliche Pixelauflösung. Die frühere stille Verkleinerung auf 512 × 512 entfällt. Die Grenze beträgt **4096 Pixel je Seite und 16 MiPixel je Bild**, zusätzlich gelten die Speichergrenzen unten. Unterstützte 16-Bit-Tiefenkarten behalten die höhere Graustufenpräzision.

Importierte Bilder beginnen bei X10/Y10 mit höchstens 100 mm an der längsten Seite. Kleinere Bilder behalten den bisherigen Anfangsmaßstab von 0,1 mm je Pixel. Die Oberfläche zeigt Quellauflösung und die aus der platzierten Breite berechnete Quell-DPI. **Die Ausgabeauflösung ergibt sich weiterhin aus den Bearbeitungswerten**, insbesondere dem Linienabstand. Ein hochauflösendes Original allein erzeugt noch keinen entsprechend feinen Laserjob.

## Speicher und Format

- Maximal 32 MiPixel dekodierte Bilder im Dokument; Kopien zählen für dieses Budget mit.
- Maximal 40 MiB eingebettete Grafikdaten; ein sehr großes oder schlecht komprimierbares PNG kann deshalb auch unterhalb 4096 × 4096 abgelehnt werden.
- Maximal 64 MiB für die Projektdatei einschließlich Base64 und Metadaten. Autosave und Projektimport verwenden dieselbe Dateigrenze.
- Projektformat **7** liest Formate **1–6**. Neu gespeicherte Projekte benötigen 0.7 oder neuer; ältere Programme sollen sie ausdrücklich ablehnen.
- Die bestehenden Jobgrenzen bleiben bestehen: höchstens 500000 Bewegungen und die jeweiligen Raster-/Schichtbudgets. Überschreitungen erzeugen einen ungültigen Job statt einer stillschweigenden Detailreduktion.

Die Fehlerverteilung von Floyd-Steinberg und Atkinson verwendet drei wiederverwendete Zeilen statt einer Fehler-Matrix über das ganze Bild. Dadurch wächst dieser zusätzliche Puffer mit der Bildbreite. Zeiger auf die Fehlerzeilen sowie unveränderliche Masken- und Modusschalter werden vor den inneren Schleifen ermittelt. Ein- und Ausgabebitmap sowie der berechnete Auftrag liegen weiterhin im Speicher. Das ist **noch kein vollständig gestreamter, asynchroner Jobaufbau**; große Berechnungen können die Oberfläche weiterhin aufhalten.

## Reproduzierbarer Vergleich

`blazer-raster-benchmark [Seitenlänge] [dither|atkinson|threshold|grayscale]` verarbeitet einen deterministischen Grauverlauf mit Struktur, gibt SHA-256, Laufzeit der Bildverarbeitung und Prozess-Spitzenspeicher als JSON aus. Linux-Spitzenspeicher wird aus `/proc/self/status` (`VmHWM`) gelesen, andernfalls aus `getrusage`.

Gleicher Treiber, Compiler und Optimierung für beide Implementierungen:

```bash
git show 6c2f079:src/imageprocessing.cpp > /tmp/blazer-imageprocessing-v06.cpp
c++ -std=c++20 -O2 -fPIC -I src tools/raster_benchmark.cpp /tmp/blazer-imageprocessing-v06.cpp $(pkg-config --cflags --libs Qt6Gui Qt6Core) -o /tmp/blazer-raster-before
c++ -std=c++20 -O2 -fPIC -I src tools/raster_benchmark.cpp src/imageprocessing.cpp $(pkg-config --cflags --libs Qt6Gui Qt6Core) -o /tmp/blazer-raster-after
/tmp/blazer-raster-before 4096 dither
/tmp/blazer-raster-after 4096 dither
```

Die 0.6-Bildfunktion wird dabei isoliert mit großen Bildern aufgerufen; die damalige Oberfläche importierte tatsächlich nur bis 512 × 512. Gemessen wird weder der vollständige Jobaufbau noch die reale Übertragung oder Gravur. Zeiten hängen vom Rechner und seiner Auslastung ab. Rohdaten und Messumgebung stehen unter [benchmarks](benchmarks/raster-2026-11-10.json). Es ist kein Vergleich mit kommerzieller Software.

Median aus je drei Läufen pro Variante und Bildgröße:

| Bild | Vorher: MiB / ms | 0.7: MiB / ms | Ausgabe |
|---|---:|---:|---|
| 512 × 512 | 21.1 / 88 | 19.1 / 38 | SHA-256 identisch |
| 2048 × 2048 | 96.3 / 1560 | 64.2 / 654 | SHA-256 identisch |
| 4096 × 4096 | 336.4 / 6450 | 208.3 / 2827 | SHA-256 identisch |

Der Speichergewinn ist deutlich; aus diesen wenigen Laufzeiten wird keine allgemeine Geschwindigkeitszusage abgeleitet.

## Regression und nächste Qualitätsschritte

Die Rastertests prüfen den Erhalt eines nur ein Pixel breiten Details jenseits der alten 512-Pixel-Grenze, identische Bilddaten und GCode nach Speichern/Laden, explizite Größenablehnung und den 16-Bit-Randfall. Referenz-Hashes für Floyd-Steinberg und Atkinson stammen aus dem vorherigen Code; beide Ausgaben müssen unverändert bleiben.

Die vorhandenen Importtests für SVG, DXF, PDF-Konvertierung und teilweise LightBurn-Geometrie bleiben aktiv. Sie sind kein Beleg für vollständige Formatkompatibilität. Als weitere Meilensteine bleiben abbrechbare Hintergrundberechnung, end-to-end Messungen größerer Aufträge, ein breiterer Referenzkorpus für Importe sowie gemessene Scan-/Materialkalibrierungen am M3 offen. Eine überlegene Produktionsqualität gegenüber kommerziellen Programmen ist noch nicht nachgewiesen.
