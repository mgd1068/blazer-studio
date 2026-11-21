# Independent outline incident regression

This checks one saved 200 × 150 mm outline project and a logically exported GRBL
program. It does not operate a controller or establish a physical machine origin,
travel envelope, work offset, laser power, material suitability or airflow.

The saved incident project was read only. Format 12 contains one visible rectangle
on page 0/layer 0 at X0/Y0, width 200/height 150 mm, `engraveOutline=true`, no extra
operations or object overrides. The enabled layer is Gravur, with the saved working
values 100%, 5000 mm/min, one pass; `material.engrave` agrees, cutting is disabled.
These are persisted user working values, not a newly confirmed material test.

## Reproduce against a reviewed offline binary

Only use a source-reviewed binary whose `--export-gcode` path constructs Machine
with HardwareAccess::Disabled and uses transient Recovery/Settings/Libraries.
The expected binary SHA is mandatory and checked against a temporary executable
copy before launch. The app receives a byte-identical temporary project copy,
never the user's original path. Its sole task is `--open-project ... --export-gcode`.
All temporary files are removed after checking. HOME and CODEX_HOME are preserved;
TMPDIR and XDG_CONFIG_HOME/DATA_HOME/CACHE_HOME/RUNTIME_DIR point into the task's
private temporary directory. Qt runs offscreen with the software backend; no live
window, host installation, devices, package or container is involved.

```sh
python3 tools/check-outline-incident.py \
  --project '/home/mathias/Projekte/Laser/Rechteck/Nur Umrandung.blazer' \
  --binary /path/to/reviewed/offline/blazer-studio \
  --expected-binary-sha256 REVIEWED_BINARY_SHA256
```

To inspect an existing exported file without launching an app, replace the last
two options with `--gcode /path/to/export.gcode`. That mode performs only local reads.
The JSON result contains bounded structural facts and hashes, no raw project names,
notes, material provenance or filesystem paths.

## Independent acceptance criteria

The Python oracle imports no Blazer libraries or production planner/encoder.
It tokenizes a small strict GCode subset, handles modal X/Y/feed/power and word
ordering, and rejects unknown commands/syntax. The initial head position is unknown;
it is not silently initialized to model zero.

- Explicit initial M5; G21/G90/G54/G17/G94 established before any XY request.
- First XY request is output-off G0 with both X0 and Y0 stated, before output enable.
- Exactly one dynamic M4 enable and exactly four nonzero powered G1 edges.
- Independent expected rectangle-side set, connected closed contour and 700 mm total.
  Four edges/700 mm alone would incorrectly admit repeated sides; the oracle rejects it.
- All four edges use modal S1000/F5000, matching the saved 100% with S-max 1000.
- Exactly five XY requests in all: one approach plus four edges. No raster, extra
  pass, connector, overscan, zero-length powered request or finish return is admitted.
- Final M5/S0. M7/M8/M9, offsets, coordinate reassignment, unverified modal axes,
  M3/stationary enable or additional output enables are rejected. A bare final G0
  only selects the motion mode and is not interpreted as a return request.

The coordinate checks describe nominal absolute G54 millimetres in [0,200] ×
[0,150]. They do not establish where G54 zero lies physically or whether the actual
controller has a positive or signed machine envelope. Neither these commands nor
the absence of M8/M9 verifies extraction. Real runtime commissioning gates remain
closed independently of this logical program check.

## Recorded offline evidence (2026-11-21)

- Original input SHA256, unchanged before/after:
  `7d9c8ee530da49648c173fdd1e633ccab2ecd9ae3de0f031be5b3c994c91be02`
- Reviewed binary build revision:
  `b14704cb5a0b49868aaee92d2f7950d0d3293c95`
- Copied binary SHA256:
  `5bc18271011a3cba1604b266294e775dba0350f715d3d94fd2a3005c443e6bd7`
- Exported GCode SHA256:
  `bac7df0330a83755874b4475b6c92bac3d5a2e14b8ea50299507631062b3d362`
- Result: four powered edges, 700 mm, five XY requests, no fill/accessory/return.

A repeat with HOME/CODEX_HOME preserved gave identical input and GCode hashes.
The later frozen release binary must be checked separately with its own expected
SHA; this evidence does not claim that a future package was tested.

## Oracle regression tests

```sh
python3 tests/outline_incident_oracle_test.py -v
```

Nine independent unittest functions cover hand-authored expected paths/parameters,
modal word ordering and omitted axes, malformed comments/words, unknown units/WCS/
axes/offsets, incorrect approach ordering, equal-length wrong-side contours, raster/
repeat/connector/return, feed/power/shutdown/accessory and extra-enable mutations,
source-intent changes and work limits. A fake offline executable independently
checks isolated paths/environment, binary hash binding and temporary-input integrity.
No production encoder generates these expectations; no serial endpoint is used.
Root owns optional CMake registration of this Python test.
