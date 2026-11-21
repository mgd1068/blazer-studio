# Explicit geometry review, save, and insertion bridge

This application bridge completes the backend actions needed by an Art Library
UI. It never copies source operations, imports device/material profiles, grants
proof or motion/extraction permission, uses the system clipboard, or connects a
physical endpoint. The underlying geometry-only scope and conversion losses in
[ART_LIBRARY_BACKEND.md](ART_LIBRARY_BACKEND.md) still apply.

## Controller API

Construct `ArtLibraryController(Document*, Machine*, ArtLibrary*, QObject*)` on
the UI owner thread. Properties are `review` (map), `message`, and `blockReason`,
with `changed` notification. It exposes:

- `reviewSelection()` captures visible selected Artwork values on the active
  page and prepares one frozen converted geometry payload. It returns `valid`,
  `error`, fixed-label `issues`, `conversionRequired`, opaque `token`, widthMm,
  heightMm, normalized boundsMm, objectCount, thumbnailPng and thumbnailSource.
  The latter is a local `data:image/png;base64,...` URI, not an external source.
- `saveSelection(name,tags,expectedToken,acceptConversion=false)` saves only the
  cached payload, only for the exact 64-lowercase-hex token, explicit acceptance
  of required conversion losses, and an unchanged selected source. Success
  consumes the token. There is no re-encode/recapture in the save action.
- `clearReview()` discards the cached geometry, source values and token. A new
  review attempt replaces the previous one, including when the attempt is blocked
  or invalid. Old tokens cannot be resurrected by undoing a source edit.
- `insert(id,xMm,yMm)` retrieves a stored UUID and calls the Document insertion
  primitive below. It rejects busy/firing Machine state and pending document
  planning, batch or image export before retrieving/converting geometry.
- `updateMetadata(id,name,tags)` and `remove(id)` delegate to immutable stored
  entry identities. They do not edit the current document or saved instances.
- `thumbnailSource(id)` lazily retrieves only the requested entry's preview.
  Its 16-entry URI cache can serve existing pictures while busy, but no new
  decoding/rendering occurs during busy/firing/planning/export state. Missing,
  protected or deleted libraries return no image; no network access occurs.

The review stores at most one bounded encoded asset and its immutable source
selection. Source comparison uses exact numeric rectangle components, exact path
elements/fill rules, exact serialized QVariant style/settings values, stable IDs,
groups/roles/page and embedded asset content. It deliberately does not use Qt's
fuzzy QRectF/QPointF equality for tiny geometry edits. Immutable asset pointers
avoid hashing large unchanged image/SVG data on every notification. Source values
are private: source power, operations, device state, source positions and project
names are never returned in a review map or embedded in its opaque token.

A changed selected source or selection invalidates the review permanently; a
new explicit review is necessary. Unrelated document notifications, title/notes,
unselected geometry edits and planning completion do not discard an unchanged
source. Global source layer parameters are not captured. Generic notifications
still refresh blockReason so the UI does not remain falsely blocked after a
background operation finishes. During a busy operation the existing review may
remain visible; save and insert re-check guards themselves.

A complex asset may have valid geometry but no bounded thumbnail. Its review
contains an explicit preview-unavailable issue; no geometry is truncated or
silently repaired to manufacture a preview. Display all labels as plain text.
The UI must reset conversion consent when the token changes and must validate
raw placement text, not silently reuse a previous committed number when the
user's current text is invalid. This numeric syntax is not available to a C++
method that receives only doubles.

## Atomic Document insertion

`Document::insertArtGeometry(bytes,xMm,yMm)` is a C++ method, not a QML shortcut
around the controller's Machine guards. Coordinates mean the **lower-left corner
of the entire normalized asset frame**, in project mm with Y up. For a desired
center placement, the UI explicitly subtracts half the reviewed width/height.
No scaling, automatic centering, bed clamping, or physical machine origin is used.

The method decodes new object/group UUIDs, translates each frame by the requested
point, assigns the active target page, and rejects any frame outside the current
work area. All objects remain non-output. Only sourceRole `graphic` becomes the
pending selection; source T1/T2 helpers are excluded. A helper-only asset inserts
with an empty pending selection. No source layer/settings/extra operations are
created or applied. Existing file URL, title/notes, material, layers, profile,
placement intent and runtime origin confirmation remain untouched.

Before any mutation it validates the combined proposed snapshot, project object/
image budgets and a one-million stored path-element budget. Active dragging is
rejected instead of implicitly creating another history step. On success it
creates exactly one checkpoint and updates items/selection; on failure no model,
file, history or partial objects are committed. Nested insertion from reset
callbacks is rejected. Undo/redo treat insertion and subsequent explicit local
operation assignment as separate steps.

In normal all-output scope the preexisting GCode remains byte-identical until
explicit assignment. If the document already uses cutSelectedGraphics, changing
the selection to new non-output templates may conservatively make that selection-
only preview empty. The placement intent is preserved and no new output is
introduced. Existing copied immutable JobPlans remain independent; the controller
blocks editing through this workflow during an active Machine job.

## Independent tests

`tests/artlibrarycontroller_test.cpp` checks manually expected inserted mm bounds,
roles, UUID/group separation, target settings/file/placement preservation, exact
one-step undo/redo, unchanged all-output GCode and immutable prior JobPlan, active
page and object-capacity constraints, invalid/outside/NaN/drag failure atomicity,
selection-only behavior, explicit conversion consent, exact/consumed/stale review
tokens, unrelated notifications and unselected edits, tiny bounds/path edits,
busy/focus/planning/batch/image-export guards, targeted thumbnail caching, stored
metadata actions and callback reentry. Machine fixtures use HardwareAccessDisabled
and simulator state only. No physical endpoint, container or package is accessed.

The root integration owns Main/QML/CMake registration and real dialog acceptance.
This slice changes Document.h plus new bridge/controller/test/docs files only.
