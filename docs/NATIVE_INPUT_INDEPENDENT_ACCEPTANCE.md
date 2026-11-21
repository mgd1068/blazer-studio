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
