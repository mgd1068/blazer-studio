# beta.6 development candidate: software acceptance

On 21 November 2026, revision
`4b73fb4e5b5d7e1a35fdf8928bda5df3bb98f7d7`, version
`0.9.0-beta.6-dev`, passed the following software checks. These results apply
only to this revision and artifact, not subsequent development.

## Source and artifact

- Normal build: 44/44 CTest groups passed in 90.60 seconds, with the independent
  barcode decoder required rather than skipped.
- AddressSanitizer/UndefinedBehaviorSanitizer: eight critical groups passed;
  the final expanded GRBL group also passed (214 QtTest results). Leak detection
  was disabled for Qt process-global allocations; this is not a leak audit.
- Release built from a clean standalone clone in the pinned isolated SDK,
  without network access or a writable source mount.
- DEB: `blazer-studio_0.9.0~beta.6~dev_amd64.deb`, SHA256
  `803bb147ca0d5693e72bbe7c57584586609e81c6b277effd51a4a12c8e892565`.
- Manifest and source archive identify the exact revision. All 83 recursive
  checksum entries and 258 local documentation targets passed verification.
- Platform: Ubuntu 26.04 amd64, Qt 6.10.2, GCC 15.2.0. This is not proof of
  compatibility with other distributions or Qt versions.

## Installed release acceptance

Separate disposable containers started from pinned clean Ubuntu image
`sha256:febc1a2eec1fdaad5db7e542ce1fefaee9656b6e3a177f0c703d616e77b86a62`.
Fresh installation and upgrade from final beta.5.2-local revision
`5e66eb1c8bdea14f14218866c035d2e159be2469` both exited successfully.

Both runs checked installation, non-root launch, exact manifest/version and
legacy-project/template GCode, reinstall, removal, menu/MIME/licenses, absence
of a development SDK, and preservation of user projects, templates, settings
and a sentinel. Screenshots were reviewed separately. No real device, host
display, session bus, Docker socket or real user home was mounted. No host
application installation or restart occurred.

The installed release binary had SHA256
`2dd7c0bf4e8f162e74e7b33dad7a7415ee144a246469991450fc4250ceebf40d`
in both containers. The independent archived incident oracle and its nine
adversarial tests passed against that binary. An unchanged copy of the user's
200 × 150 mm outline project produced a laser-off `G0 X0 Y0` before one `M4`,
then exactly four powered edges totalling 700 mm and final `M5 S0`, with no
fill/raster, accessory commands or implicit finish return. GCode SHA256:
`bac7df0330a83755874b4475b6c92bac3d5a2e14b8ea50299507631062b3d362`.

## Limits and evidence

Subsequent independent review found absolute wire-rounding and relative-jog
target gate defects in this source. They were fixed **after** this freeze in
`d0df81c`; see [coordinate precision regressions](../GRBL_COORDINATE_PRECISION.md).
This archived package does not contain that correction. Its passing package
checks must not be treated as a current deployment or physical-operation release.

The incident oracle proves logical absolute-mm/G54 output only. It does not
prove physical origin, WCO, signed travel limits, referencing, airflow or optical
shutdown. Real motion/output remains blocked by uncommissioned contracts. The
installed older beta.5.2-local collision/fan failure remains an unresolved
physical acceptance failure. Controller timers still share the GUI thread in
this candidate; heavy-action guards do not establish independent monitoring.
Offscreen rendering does not establish native Wayland/HiDPI or beginner
usability. Full LightBurn parity remains incomplete.

Local evidence is preserved outside the repository at
`/home/mathias/benslab/analysis/blazer-beta6-4b73fb4/`:
`verification/SOFTWARE-VERIFICATION.json`, `ARTIFACT_VERIFICATION.json`,
`runtime/results/FINAL_RUNTIME_SUMMARY.json`, per-mode logs, self-tests,
screenshots and evidence hashes. The source clone and packaged artifacts there
remain frozen; this document records results without modifying that package.
