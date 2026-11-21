# Optional immutable technical diagnostics preview (F53)

The existing `diagnostics()` and `exportDiagnostics()` implementations remain
unchanged. Their privacy-default report does not include controller context,
raw logs, port identifiers, project names, source files, or material notes.

`ApplicationServices` additionally offers an explicit preview-and-export path:

- `previewTechnicalDiagnostics(bool includeController = false)` returns
  `{digest, report, json}`. `report` is exactly the existing diagnostics map by
  default. Only explicit `true` adds `controllerContext` from the real Machine
  facade's `controllerDiagnosticSnapshot()`; a plain Simulator cannot invent a
  controller trace. `json` is the exact frozen UTF-8 JSON text available to export.
- `exportTechnicalDiagnostics(QUrl file, QString expectedDigest)` writes that
  frozen JSON copy only when its current preview token matches exactly.
- `clearTechnicalDiagnosticsPreview()` discards the cache and invalidates its
  token. The service holds exactly one bounded JSON copy and one token, without
  persisting them in settings. Starting any new preview replaces the prior one.

The `digest` is an opaque 64-character lowercase hexadecimal **preview token**,
formed as SHA-256 of a fresh per-preview nonce followed by the frozen JSON bytes.
It is not an independent exported-file checksum. Even identical consecutive
previews have different tokens; a stale token cannot authorize the replacement
preview. No nonce or extra metadata is added to the default report itself.

A preview is limited to 64 KiB of serialized UTF-8 JSON. Oversized/empty output
returns an empty map, sets the service message, and leaves no exportable cache.
Opting into controller context does not modify future default diagnostics calls.
Controller context contains the producer's typed facade flags and bounded,
redacted session evidence; it must not be replaced with raw application logs,
port names, source text, arbitrary controller strings, or file paths. Numeric
controller evidence may include position and coordinate-offset information, so
users must be able to inspect the optional content before exporting it.

Export does not inspect the current document, refresh controller state, recompute
status ages, or serialize a new report. Changing the document or simulator after
preview therefore cannot silently alter exported evidence. The file represents
the historical reviewed snapshot, not a promise that those states remain current.
A new preview is required if the user wants updated evidence. Diagnostics never
grant hardware acceptance, motion, airflow, material proof, or emission permission.

Export accepts only an absolute local `.json` URL without a host, query, or
fragment. Existing targets must be regular files; symlinks and other special
files are rejected. A QLockFile protects cooperative concurrent writers, and a
QSaveFile commits the exact cached bytes atomically with owner read/write
permissions (0600) and no direct-write fallback. Failures preserve an existing
file. No backup copy, upload, mail, network operation, device connection, or
controller command is performed by this feature. Tokens are service-local; a
valid token from another service instance cannot export a nonexistent cache.

The root UI should show the returned JSON as plain text, keep controller context
unchecked by default, and pass the token belonging to that exact displayed
preview. Closing/discarding the preview should clear the service cache and its
local UI copy. Do not describe the token as a file checksum or silently generate
a new preview from the file-save action.

## Independent offline verification

`tests/technicaldiagnostics_test.cpp` checks the unchanged default schema and
privacy fields, explicit typed-controller opt-in, byte-identical frozen export
after document/simulator changes, owner-only permissions, unchanged legacy
export behavior, old-token rejection after replacement or clear, malformed and
nonlocal requests, directory/symlink/locked targets, atomic preservation, and
QML-invokable methods. Machine fixtures use `HardwareAccess::Disabled`; changes
to connection state affect only the simulator. No physical endpoint is opened.

The integrated application provides `Machine::controllerDiagnosticSnapshot() const`,
the unchecked-by-default preview dialog and local-file picker. `featureworkflow`
exercises the actual preview, opt-in toggle and byte-identical saved content in
normal/light and compact/dark windows; the service test remains separate.
