# Offline geometry asset and Art Library backend (F19 slice)

This slice stores reusable artwork geometry. It does not insert into a Document,
change material/device profiles, use the system clipboard, import LightBurn
`.lbart` libraries, or grant output. Root integration will supply the explicit
capture/review UI and one-Undo insertion action. It must not label the whole F19
family complete before those workflows are tested.

LightBurn 2.2's Art Library likewise stores artwork without Cut Settings:
https://docs.lightburnsoftware.com/2.2/Reference/ArtLibrary/
Our tags and own `.blazerart` schema are separate application features, not
claims of LightBurn file compatibility.

## Geometry codec API

`GeometryAssetCodec::review(QList<Artwork>)` accepts an explicit value snapshot of
visible selected objects from one active page. The caller selects the active
page; mixed pages, hidden objects, duplicate/missing IDs, inconsistent groups,
unsupported types, and limits reject the entire selection. It returns an error,
fixed-label issue list, and `conversionRequired`. No partial selection is saved.

`encode(items, acceptAppearanceConversion=false, Cancellation={})` returns bytes,
contentHash, the review, and error. Conversion-required selections are refused
without the independent explicit flag. All vector captures require explicit appearance-conversion permission: fills,
stroke colors/widths and source rendering are not copied, so this is not a
visually identical artwork copy. Fill-rule topology is retained separately.
All vectors become path contours. Text
loses text editing and font dependence; SVG loses intrinsic colors/brush styling
and SVG editing. Primitives become editable paths. `JobPlanner::geometry` resolves
the existing actual-mm transforms and stored font appearance, with fresh strict
`SvgAsset::parse` validation. Unsupported SVG geometry fails atomically.

Images are baked through the existing `ImageProcessing::apply` and PNG asset
codec. Crop, masks, brightness/gamma/sharpness and the selected raster appearance
are baked into pixels, while physical object bounds stay unchanged under the
existing image-export convention. Original colors/pixels and live filters are
lost and must be disclosed before capture. Baked pixels use a fresh image to
strip textual source PNG metadata; metadata-bearing PNG payloads are refused on
decode. No source style/settings map survives.
Relief is unsupported in v1 and is never silently converted or omitted.

`decode(bytes, Cancellation={})` returns fresh `Artwork` templates, parallel
`sourceRoles` (`graphic`, `T1`, `T2`), `boundsMm`, canonicalBytes/contentHash, and
pixel/element counts. A lone selected group member is explicitly reported and
stored without a dangling source-group link. Every decode creates new object and group UUIDs. All items
are non-output: source `graphic` becomes T1; source T1/T2 remain their original
non-output roles. The caller must use sourceRoles to select only pending graphic
items for a subsequent explicit local operation choice. Settings and
extraOperations are empty, no engraveOutline/material/device/position grants
are carried, and image style is a fixed neutral grayscale basis for baked pixels.

Coordinates are world millimetres, Y up. The union of safe object frames is
translated to lower-left `(0,0)` and stored as `boundsMm`; all relative positions
and actual path dimensions remain intact. Zero-width/height paths receive the
same minimum 0.1-mm representation frame without moving their physical endpoints.
For example, horizontal lines 10 mm apart get a 10.1-mm enclosing frame and
endpoints at Y=0.05 and Y=10.05. A target insertion translates the decoded frames
as a unit to the explicitly chosen design point; it must not scale/clamp geometry
or infer a physical machine origin. Cubic controls may lie outside actual path
bounds; bounds validation checks the curve bounds, not its control-point hull.

The strict version-1 payload has format `blazer-geometry-asset`, version, mm
units, bottom-left origin, widthMm/heightMm, and indexed items. Path items contain
only local index, contiguous group index, source role, kind, fillRule, and path
coordinates. PNG items contain only index/group/role/kind, boundsMm, and embedded
PNG. Unknown fields, loose external links, processing dictionaries, mismatched
indices, noncanonical/singleton groups, malformed paths/images, and out-of-range
geometry are rejected. ContentHash is SHA-256 of the compact canonical geometry
payload, including roles/groups/embedded PNG, never library names/tags/dates.
It is a content identity, not mathematical equivalence or a safety certificate.

Limits: 2000 objects, 100000 stored/generated path elements, 16 MiB per encoded
asset, each image at most 4096 pixels per side/16 MiPixels, 32 MiPixels combined,
SVG source at most 1 MiB each/8 MiB processing work across the selection (copies
count), text at most 2048 characters, geometry at most 2000 mm.
Cancellation rejects without partial bytes/items; conversion within one bounded
source object is not interruptible. The caller may run pure codec work on an
immutable worker snapshot and must reject stale review/capture context itself.

## ArtLibrary API and persistence

`ArtLibrary(QString path={}, QObject* parent=nullptr)` defaults to
`AppDataLocation/art-library.blazerart`. Properties `entries`, `message`, and
`writable` notify through changed. Entries and `search(query)` return only small
metadata: id, name, tags, timestamps, contentHash, widthMm/heightMm, objectCount,
and a conversion notice. They never duplicate geometry/PNG payloads across every
list row. Search is Unicode-normalized/case-folded over names and tags.

- `add(name,tags,encodedGeometry)` validates the whole codec payload, commits it,
  and returns a fresh entry UUID. Adding the same geometry with another name
  creates a distinct entry; it does not silently overwrite an existing item.
- `updateMetadata(id,name,tags)` changes metadata only; contentHash stays identical.
- `remove(id)` removes only the stored library entry. Already decoded/inserted
  value copies remain independent.
- `geometry(id)` returns the exact validated immutable geometry bytes explicitly.
- `thumbnail(id)` generates a bounded neutral 256×256 PNG. The 16-entry in-memory
  cache is keyed by contentHash, not metadata. There is no persisted thumbnail
  payload or external image lookup to trust. Complex assets exceeding 256 objects
  or 20000 path elements receive an explicit unavailable-preview error. No geometry
  is dropped to fabricate a successful thumbnail.

Use the QObject on its owner thread. For asynchronous preview generation, first
obtain frozen geometry bytes on that thread, then run the pure
`GeometryAssetCodec::thumbnail` helper with cancellation. Display all names,
tags, errors, and notices as plain text. A thumbnail is artwork, not a toolpath.

The strict library schema has format `blazer-art-library`, version 1, library UUID,
and entries containing id/name/tags/createdUtc/updatedUtc/contentHash/geometry.
No paths, profile grants, source document settings, source notes, or external
resources are stored. Names are bounded to 100 characters, tags to 16×64;
case-insensitive duplicate tags are normalized. Library limits are 256 entries,
64 MiB serialized, 1000000 path elements, and 32 MiPixels across all entries.

A lifetime QLockFile protects cooperating instances. Existing corrupt, oversized,
non-regular/symlink, or concurrently locked libraries are protected. Disk existence
and SHA-256 must still match before mutation; external changes disable writes and
geometry/thumbnail access until reopening. QSaveFile atomically commits owner-only
0600 data with no direct-write fallback; failed writes do not publish in-memory
entries or replace the prior file. Valid edited files are user data, not
cryptographically authenticated evidence. No constructor invents an initial asset
or persists an empty library automatically.

## Independent offline acceptance

`tests/artlibrary_test.cpp` independently checks known normalized rectangle/path
coordinates and dimensions; role/group/UUID behavior and processing stripping;
text bounds; manually expected cropped threshold-PNG pixels and thumbnail row
orientation; independent OddEven/Winding interior and open-line checks; transformed SVG and separate zero-height lines; unsupported/hidden/
wrong-page/duplicate selections and schema/work budgets; persistent metadata,
Unicode search and immutable hashes; concurrent locks, corrupt/hash-tampered
files, external replacement, actual failed writes and explicit retry; duplicate
IDs, entry-count and oversized disk limits. These are not reciprocal roundtrips
alone. The test never constructs a Machine or opens a physical endpoint.

No Document/Main/Machine/CMake changes are included. Root must register the new
sources/test and supply the explicit capture and insertion workflow separately.
