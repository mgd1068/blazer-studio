# Native project compatibility review (F06/F04)

LightBurn/MillMage project files contain executable processing intent as well as
artwork. The native reader is a bounded **geometry importer**, not a project
migration tool. The two-argument `NativeDesign::read(bytes, millmage)` now fails
closed. Legacy `Document::importFile(native)` therefore rejects the source until
an explicit reviewed geometry workflow is used. No silent fallback is provided.

## Audit findings

The earlier reader accepted CutSetting, CutSetting_Img, Operation, CNCProjectSetup,
VariableText, and UIPrefs without migrating their semantics. CutIndex was used
only for non-output tool classification; other operation assignment was lost.
Laser cut/fill/image mode, power, speed and its units, repeats/suboperations,
kerf/Z offsets, output/air-assist flags, source device data, CNC tool/feed/spindle
settings, and source commands did not become corresponding Blazer settings.
This remains unsupported and is now reported before insertion. Disabled source
output does not automatically remain disabled when artwork is assigned to new
target operations; the review explicitly calls this out.

LightBurn root MirrorX/MirrorY had previously been ignored. True mirror flags
are now rejected in both native formats pending verified implementation. Unknown
root, shape, and geometry-container options and unknown root sections are also
rejected: explicit geometry import cannot authorize silently different geometry.
Root coordinate-unit declarations are rejected until that native units contract
is verified. Source origin/start-mode metadata is reported as unmigrated and is
never treated as a physical work origin.

Text with stored contours is imported as contours and loses editability. Text
without them requires a separate explicit system-font substitution opt-in; the
legacy family/height fallback can change weight, spacing, alignment, glyphs, and
shape. An empty BackupPath cannot bypass this requirement. MillMage still
requires stored text contours even if font substitution is requested.

Embedded PNGs retain geometry only. Source rasterization/laser image modes and
processing settings are not migrated. Nonneutral gamma/brightness/contrast/
enhancement settings and unknown bitmap options reject the source. A File
attribute beside a valid embedded PNG is ignored provenance, never opened, and
is reported without exposing its value. External-only images, href/src/URL
references, scripts, import/include resources, DTD/entities, and XML processing
instructions are rejected. This code does not execute source commands or access
external files.

## API and review workflow

`NativeDesign::ReadOptions` contains `intent` (default
`ImportIntent::PreserveProjectSemantics`, unsupported) and
`allowSystemFontSubstitution` (default false). Explicit geometry conversion is:

```cpp
NativeDesign::read(bytes, millmage,
    {NativeDesign::ImportIntent::GeometryOnly, allowFontSubstitution});
```

`Result` contains `elements`, `error`, and `compatibility`. A rejected result
contains no geometry. `NativeDesign::compatibility(bytes, millmage, options)`
provides bounded loss preflight without decoding geometry/images. A scanner
`canImportGeometry` is candidate eligibility only: `geometryValidated` remains
false. Review should use explicit `read` to validate actual geometry; only full
success sets `geometryValidated` true. Re-read the file before insertion and
require its exact `sourceSha256` to equal the reviewed source. A changed source
requires fresh review. Reconfirm font permission independently of geometry-only
intent. Do not persist or import controller/material grants from the report.

`CompatibilityReport::toVariantMap()` exposes:

- `format`: LightBurn or MillMage; `sourceSha256`: lowercase SHA-256 of exact bytes.
- `issues`: bounded entries with `code`, `severity`, `scope`, `sourceIndex`
  (zero-based document-order shape index, otherwise -1), `field`, `label`, `count`.
- `complete`, `geometryOnlyRequired`, `blocked`, `canImportGeometry`,
  `geometryValidated`, and always `semanticMigrationSupported: false`.

Stable loss codes include `laser-operations-not-migrated`,
`cnc-operations-not-migrated`, `power-not-migrated`, `feed-units-not-migrated`,
`passes-suboperations-not-migrated`, `auxiliary-output-not-migrated`,
`offsets-not-migrated`, `source-disabled-output-not-preserved`,
`operation-assignment-not-migrated`, `source-origin-not-migrated`,
`source-commands-discarded`, `variable-text-not-migrated`, `text-editability-lost`,
`font-substitution-required`, and `bitmap-processing-not-migrated`.
Hard failures have separate unsupported-resource/geometry/version/work-limit
codes. Reports contain fixed labels and known field identifiers, never raw source
attribute values, names, notes, commands, or paths. Render labels as plain text.

The scanner bounds input at 16 MiB, depth 64, 100000 XML elements, and 128
aggregated issues. The existing geometry reader additionally limits shapes,
vertices/elements, images, pixels, and combined embedded data. A report limit
blocks import rather than dropping warnings. Parsing/version/unsupported geometry
failure never publishes partial elements. These are defensive format checks,
not a lossless native compatibility claim or physical output acceptance.

## Tests and remaining integration

`blazer-nativecompatibility-tests` covers strict default rejection and preserved
source hashes; power/feed/units/repeats/subops/output loss reports; independent
font opt-in including empty backups; mirrors, units, unknown options/resources;
private-value suppression and count aggregation; geometry validation versus
preflight; embedded-only provenance; atomic legacy Document rejection; and
MillMage operations/tool roles. Fixtures are independently authored, not vendor
project archives. Existing parser geometry fixtures explicitly select the limited
geometry and font workflow. Successful Document geometry-insertion regression
cases must use the new reviewed entrypoint provided by the root integration;
legacy `importFile(native)` is intentionally denied.
