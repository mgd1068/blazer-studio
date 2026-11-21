# Per-device material test ledger

`MaterialTestLedger` stores explicit user observations from material tests.
A matching record is historical evidence only. It never sets `proofConfirmed`,
changes a material profile, or grants calibration, machine, motion, extraction,
or emission permission. Even a recorded `usable` result is a user's observation,
not an application measurement or certified setting.

## QML interface

Construct `MaterialTestLedger(QString path = {}, QObject *parent = nullptr)`;
empty path uses the application data directory's `material-tests.json`.
Expose the instance as a context property. Its `records`, `message`, and `writable`
properties notify through `changed`.

- `applicable(bindingKey, parameterHash)` returns matching records, newest
  submission first. All other device identities, binding revisions, and
  processing parameter signatures return no matches.
- `record(bindingKey, parameterHash, observation)` records an explicit form
  submission and returns a new UUID, or an empty string on validation/write failure.
  The observation keys are `date` (ISO YYYY-MM-DD, not future), `material`
  (required, up to 200 characters), `batch` (present, possibly empty, up to 200),
  `thicknessMm` (finite numeric value, greater than zero, at most 1000), `result`
  (`usable`, `failed`, or `inconclusive`), `notes` (required, up to 4000), and
  optional `photoUrl` (empty or an existing readable local regular-file URL).
  Unknown fields and remote photo hosts are rejected.

Each returned record contains `id`, `bindingKey`, `parameterHash`, and the
`observation` map. Photographs remain references: the ledger never uploads,
copies, decodes, or fetches a photo. Missing historical files do not invalidate
past observations; a new submission referencing a missing file is rejected.

Both binding and parameter hash must be exactly 64 lowercase hexadecimal
characters. The caller must supply the current DeviceLibrary binding token
(which includes instance identity, revision, and output profile) and hash the
active material, layer settings, basic operation, and output profile consistently.
A profile fingerprint alone must not be used as a physical device binding. An
unknown active device cannot produce a device-specific record. The ledger cannot
authenticate arbitrary strings supplied by an untrusted caller; do not add an
editable binding field to the user form.

Capture the binding and signature when the form opens; before recording, compare
them with the current active context and reject a stale form. Refresh the history
filter when either changes. Display records as historical observations and keep
the separate current-session material proof reset on binding/parameter changes.
Never make presence of a record automatically set the proof checkbox.

## Persistence

The version-1 JSON format contains `format: blazer-material-test-ledger`,
`version: 1`, and `records`. Limits are 1000 observations and 8 MiB on disk.
Loading validates the complete document, unique canonical UUIDs, exact field
sets, types, signatures, and observation fields before publishing any records.
Malformed files remain untouched and make the ledger read-only. Initialization
never invents a device, observation, or file.

A lifetime QLockFile protects cooperating application instances. Before each
write, file existence and its SHA-256 content hash must still match the opened
version. A detected external change blocks writes and makes `applicable` return
no matches until the ledger is reopened. QSaveFile atomically commits owner-read/
owner-write data, updating in-memory records only after successful commit.
Failed writes preserve the prior in-memory and disk version. These checks do
not authenticate a valid edited file or prevent an uncooperative external writer
from racing between the comparison and commit. Observations are user data,
not tamper-proof device evidence.

## Offline tests

The standalone `blazer-materialtestledger-tests` target links Qt Core and Qt Test
only. Tests cover persistence and owner-only permissions, unknown/malformed
contexts, device identity duplication and revision/signature changes, concurrent
openers, external mutation, strict corrupt/duplicate-record loading, observation
validation, and local/missing historical photo references. No controller,
application session, or physical device is accessed.

```sh
cmake --build BUILD --target blazer-materialtestledger-tests -j2
ctest --test-dir BUILD -R '^materialtestledger$' --output-on-failure
```
