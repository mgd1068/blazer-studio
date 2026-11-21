# Independent native focus input acceptance

These tests concern the NativeInputIntentAdapter focus provenance collector, not
physical human verification or hardware output acceptance. All constructed input
is private friend-fixture input, with platform registration disabled. QQuickWindow
runs offscreen. No real mouse/key injection, controller port or serial connection
is used. Generic Start/Jog/Home/Frame/Zero provenance is outside these assertions.

`tests/nativeinputintent_adversarial_test.cpp` independently checks ten explicit
clock mapping cases including conservative timestamps, exact250ms/251ms boundary,
future, fresh calibration, zero/oversized values, both sides of32-bit wrap and
ambiguous half-wrap. Expected observed times are constants independent of the
production mapping helper. Additional cases check original deadline after delayed
consumption, repeat/synthetic rejection with a positive one-shot control, release
before consume, registration change, visibility, enabled state, window reparenting
and destroyed controls.

Two deterministic red regressions on immutable91d4adf and successor32895ca:

1. Capture valid X11 pending input, then increase the fixture's recorded BOOTTIME−
   MONOTONIC baseline by1000ms while leaving both actual clocks unchanged. This
   exercises a valid nonnegative but inconsistent continuity baseline.
   Consume incorrectly succeeds because only new mapping checks clock continuity.
2. Capture an X11 keyboard press with the registered control focused; focus another
   item before consume. Consume incorrectly succeeds because keyboard provenance
   and current focused control were not checked after capture.

Expanded32895ca result:16passed/2failed/0skipped. Release/synthetic/expiry positive
and negative controls pass; the two red assertions must become green without
relaxing test semantics. The standalone review build links the actual adapter
source and Qt Quick/DBus/Test, with no product synchronous mode or public grants.
CMake registration of this new independent test remains integration-owner work.

This layer cannot establish physical movement, airflow, user identity or release
latency by itself. Runtime worker expiry/revocation/output gates need separate PTY
end-to-end evidence; existing runtime fixture stamps are explicitly synthetic.

A fixture correction changes its original `baseline -= 1000` to `baseline += 1000`:
the former can become negative on a never-suspended host, explicitly disabling the
baseline check. This is a synthetic clock-continuity mismatch, not a real suspend.
The corrected test against immutable adapter fix ae49013 passes all18 results both
normally (349ms) and with ASan/UBSan (363ms). Leak detection is disabled for Qt
process globals; no leak-free or hardware acceptance claim follows. Evidence is
under `benslab/analysis/blazer-native-input-independent-20261121`.

The followup adapter4610de3 requires a readable bool `pressed` property on the
registered item at consumption. Fixtures explicitly model accepted Button state;
missing/false/string values and a top overlay deny. Hiding the overlay permits the
same registered item, proving that those negative checks do not always deny.
Private dynamic properties are test data, not a production clock/grant API.

`tests/nativefocusruntime_test.cpp` drives actual Machine.beginFocus through the
adapter and original-time seal into a real worker with the independent child PTY
peer. Private worker-only synthetic envelope/modal/airflow evidence permits this
specific offline output oracle. No physical device or physical-human claim is
made. Six scenarios cover fresh finite burst, direct QML begin without pending
proof, stale before consume, stale after worker enqueue, public cancel before
consume, and public cancel after worker enqueue. The fresh path checks the literal
seven-block finite S20/0.2s pulse, M5 before power and M5 S0 after dwell, no XY or
M8/M9, and no second pulse without a second capture. A destruction fence keeps the
child peer alive until worker teardown.

On4610de3 the explicit public cancel-before-consume row is red: Machine.endFocus
revokes the runtime generation, but leaves the collector pending, so later begin
seals that old input against the new generation. This is an explicit cancellation
retirement gap. Actual native release already revokes the collector, and a real
Button's pressed=false separately prevents consumption; this red fixture keeps
pressed=true intentionally to isolate the public cancel contract.

## Final focused verification

With adapter ae49013/4610de3 and Machine cancel fix35c9bcc, independent adapter
checks pass20/20 normally (345ms) and ASan/UBSan (349ms), including direct collector
deletion from its continuity-revocation callback. Original adapter suite passes
9/9 under ASan/UBSan (793ms). The actual Machine-to-worker focus suite passes8/8
normally (2098ms) and with the complete linked core instrumented by ASan/UBSan
(2872ms),0skipped. The explicit cancel regression becomes green without changing
its meaning. Leak detection remains disabled; no leak-free claim is made.

The standalone review CMake projects live in the evidence directory only, without
product CMake edits. Both actual core builds use immutable product deltas on6c9ffca
plus the test commits; the separate Root offline-noRuntime branch is not required
by these Allowed+own-PTY fixtures. A pre-existing missing-field-initializer compiler
warning on ControllerStatus::extractionSafetyError remains in that historical core
base; it was not a Qt runtime warning or sanitizer finding. Root integration owns
registration, final complete tests, source freeze and packaging.

These results accept the specified synthetic focus paths only. They do not cover
generic Start/Jog/Home/Frame/Zero native action provenance, real OS event delivery,
external usability, physical controller timing, mechanical position or airflow.

## Generic API and caller lifetime followup

The original20 adapter results and all6 Machine focus scenarios remain. The new
per-control birth cutoff correctly rejected4 original window fixtures because
their synthetic conservative event time (`now-12ms`) predated immediate control
registration. The fixture now waits until the synthetic event is newer than
registration; expected age/deadline values and every assertion remain intact.
This models a fresh synthetic event rather than weakening the production cutoff.
Machine focus arming is now asserted explicitly before each negative scenario,
so cancellation/expiry checks cannot pass merely because capture failed.

Two additional actual Machine cases cover direct owner deletion during native
consumption (focus and generic Jog). A private clock-baseline discontinuity
emits invalidation; a synchronous changed receiver deletes Machine. Both calls
must return safely and emit no powered command. The generic candidate is an
explicit private synthetic post-acceptance activation; the actual Machine
binding/consume/seal path is retained. No real OS input or hardware is involved.

Exact owner product `000970c` normal verification: nativeinputintent_adversarial
and nativefocusruntime CTest2/2 passed4.18s (20 and10 Qt results). Full-core
ASan/UBSan for this new owner-deletion followup is still running; earlier8-case
sanitizer evidence above is not represented as a pass of these additional cases.
