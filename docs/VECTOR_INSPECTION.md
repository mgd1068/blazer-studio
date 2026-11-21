# Bounded advisory vector inspection (F11)

The inspector reads an immutable current output snapshot and reports geometry
which merits inspection. It does not delete, join, close, move, simplify, select,
or repair objects. Findings and an empty report never grant safe output,
material proof, motion acceptance, or extraction permission.

## Runtime service

Expose `VectorInspectionService(Document*, parent)` to QML. Its `report`, `busy`,
and `message` properties notify through `changed`. Invoke `inspect(false)` for
all active-page output, `inspect(true)` for current selected stable object IDs,
and `cancel()` to discard an outstanding/current report. Empty selection is
rejected by the service; it does not silently inspect everything instead.

Inspection is explicit and asynchronous, with one worker and no pending queue.
Changing source content invalidates a published report and cancels a running
inspection; selecting different IDs also invalidates selection-only inspection.
Authoring revision, retained immutable source storage, and the exact serialized
project SHA-256 at completion prevent stale publication. Generic document status
and planning notifications do not trigger geometry scans or invalidate unchanged
source snapshots. Destruction cancels and joins the bounded worker. The optional
C++ Inspector constructor argument is a deterministic test seam, not a QML API.

The pure helper is `VectorInspection::inspect(JobInput, selectedIds, Cancellation)`.
For that lower-level helper an empty ID list means all output. It returns a
`Report`, whose `toVariantMap()` exposes `findings`, `complete`, `cancelled`,
`inspectedObjects`, `segmentCount`, `pairChecks`, and `advisoryOnly: true`.
Each finding has stable `code`, `severity`, `label`, `objectIds`, zero-based
`contourIndex` (-1 for object/global), and finite `xMm`, `yMm` in project mm.
Render labels and names as plain text. Codes are internal identifiers; labels
are human-readable German descriptions.

## Findings and scope

- `duplicate-cut-contour`: sampled cut-contour edges coincide, ignoring direction,
  starting point, object names, and IDs. Different objects and repeated subpaths
  can cause repeated cutting. Configured passes are never expanded or counted
  as accidental duplicates.
- `open-cut-contour`: a cut contour has distinct endpoints. This is advisory;
  an intentionally open cut line is legitimate. Primary outline engraving does
  not become a cut warning; an additional enabled Schnitt operation still does.
- `degenerate-contour`: no usable length/geometry or zero-length segments.
- `self-intersection`: a contour crosses itself, reuses an edge, or distinct
  subpaths of one object actually cross/overlap. Normal adjacent corners and a
  closing endpoint are excluded. Nested holes, disjoint subpaths, and blank gaps
  do not invent joining segments or intersections.
- `outside-work-area`: actual vector geometry extends outside the profile area.
- `geometry-unavailable`: geometry/operation cannot be interpreted completely.
- `work-limit`: bounded work ended before all checks; `complete` is false.

The existing `JobPlanner::geometry` resolves normalized paths, text, and SVG
transforms into the same actual world-mm geometry. Only visible enabled output
with positive power on the active page is inspected. Non-output T1/T2 helper
shapes, disabled operations, hidden/other-page objects, raster, and relief are
excluded. Quiet zones without geometry produce no cut findings. The inspector
examines source vector topology, not kerf offsets, raster/fill scanlines, job
ordering, inter-object partial intersections, or physical controller travel.

Curves are approximated by bounded adaptive cubic subdivision with 0.02-mm
flatness and control-polygon deviation checks. Duplicate detection compares
sampled edges quantized to 0.001 mm; geometrically equivalent paths with different
segmentations are not guaranteed to match. Intersection tests are numerical
approximations, not certified topology. Advisory limits include 1000 output
objects, 4000 source/generated path elements per object, 20000 flattened segments,
500000 intersection pair checks, 500 ordinary findings, and cubic depth 12.
SVG input is additionally limited to 64 KiB and 1000 XML elements; text to 1000
characters. Existing geometry conversion is not interruptible within a single
bounded object, but cancellation is checked between objects/subdivision/pairs.
A resource exception produces an incomplete report. Always show incompleteness
or cancellation prominently; no finding count should be presented as a safety
certificate.

## Validation

`blazer-vectorinspection-tests` covers differently identified/reversed duplicate
cuts, legitimate multiple passes, open marking/outline versus extra cuts,
bowties and crossing subpaths, transformed SVG coordinates, nested/disjoint
contours and helper quiet zones, adjacency/backtracking, bounds/empty contours,
selection, work caps, cancellation, generic notification stability, and stale
asynchronous results. All tests use offline document/snapshot data only.
