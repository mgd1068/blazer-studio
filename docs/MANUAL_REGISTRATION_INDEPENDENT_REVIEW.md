# Independent manual Print-and-Cut review

Reviewed immutable f866b149b5dcaf325829c3419df11167d2de9d9e, including
MANUAL_PRINT_AND_CUT/FUNCTION_ACCEPTANCE, pure transform and source binding,
controller tokens, Document/async plan/export/frame integration and Main material
confirmation key. No product source changed. This is the manual offline two-point
subset, not full F46 parity, physical alignment or hardware authorization.

## Confirmed wire-boundary regression

The existing general GRBL encoder checks unrounded coordinates against profile
bounds, then writes three decimals. Manual registration also accepts raw bounds.
A valid profile made by taking DeviceProfile::m3Reference JSON and replacing only
workspace.width with200.0006 accepts an endpoint at200.0006, then emits the literal
`G1 X200.001 Y20.000 F600.000`, outside that profile. This occurs with and without
registration. It is independent of the separately commissioned machine envelope
and does not prove physical movement was admitted.

`tests/manualregistration_precision_test.cpp` independently parses XY words from
actual ControllerProgram text. It never calls formatter/quantization helpers to
compute expected values. Safe positive controls use endpoint200.0004 and must
remain usable (wire200.000). Unsafe cases may reject during registration or
encoding, but must not return accepted out-of-bed program text. Registration
reference pairs are design(10,20)→(30,20), target(180.0006,20)→(200.0006,20), rigid,
maximumResidualMm=.5. A real Document with a20x10 rectangle at10,20 on cut layer1
also currently accepts that intent through applyManualRegistration.

Baseline result:4passed/3failed/0skipped in4ms. Root owns a general encoder wire-grid
bounds check. Parity owns binding encoded-program validation into registration
review/apply/job validity. This test-only change adds no controller grants, port
operations, clamping, scaling repair or production synchronization mode.

## Reviewed lifecycle contracts

The source snapshot binds exact geometry/processing, active page and effective
selection to canonical output, profile and session-specific offline frame. Source
COW identities keep generic callbacks cheap, while apply and background planning
verify exact digests. Applied source changes retain a blocked registration rather
than substitute absolute output. Explicit history/file transitions remove the
session mapping and announce that removal; this is an intentional documented
policy rather than restoration of historical machine state. Runtime mapping is
excluded from saved files and undo snapshots.

The Main confirmation key includes the stored registration intent fingerprint,
which includes canonical source context and transformation. Canonical planning
precedes adding a known head approach, so a different head/rapid.from does not
change that requirement. Scaled transformation changes it. Matching historical
material records still do not check the confirmation box. Registered job frames
consume transformed nonrapid paths including overscan; helper/selection frames
are denied while the mapping is active. Source and target coordinates are named
offline-G54-intent data and cannot satisfy reference, airflow or output contracts.
No further concrete lifecycle issue was found in this focused static review.

Evidence: benslab/analysis/blazer-printcut-independent-20261121. Immutable normal
and sanitized core libraries were read-only copied from Parity's post-commit f866
builds before linking the independent test. This is a source/software oracle, not
a release or hardware acceptance result.
