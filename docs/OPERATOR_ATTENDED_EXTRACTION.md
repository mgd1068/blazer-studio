# Supervised external extraction without a sensor

This explicit opt-in models a human statement about independently operated external
extraction and the operator's continued attendance. It is not measured airflow,
a verified M3 enclosure-fan command, Air Assist, or a hardware safety guarantee.
Explicitly selected SensorVerified retains its sensor contract; unknown configured mapping/flow still blocks. The normal unconfigured device-cycle start path is described below.
No M8/M9 command is added, and no existing command/physical sensor state is forged.
The installed M3's internal fan mapping remains unresolved.

The trusted worker alone owns the AccessoryContract and opaque OperatorWitness.
Do not serialize/import witnesses or expose mode selection as a portable project
permission. Runtime/physical commissioning, native input and coordinate evidence
remain independent. Selecting this mode or submitting an attestation never starts
processing. The UI must name the external equipment, require extraction-on and
continued attendance explicitly, expose "Luftstrom wird nicht gemessen", and
require a separate native Start/Focus/Resume action afterward. Unattended use and
live overrides remain unavailable in this mode. If extraction is unavailable or
the operator leaves, the integration calls operatorNoLongerAttending and suspends
processing. Software cannot detect an unreported departure or failed external fan.

## Worker API

```cpp
using A = AccessoryContract;
// Explicit trusted session setup; default policy has not silently changed.
accessory.selectOperatorAttendedExternal(sessionBinding);
A::OperatorContext ctx{sessionBinding, operationBinding, A::OperatorAction::StartJob};
auto witness = accessory.submitOperatorAttestation(ctx, extractionOn, operatorPresent,
                                                    observedAtMs, nowMs);
// Keep the witness in the worker. The UI receives only state/notice/expiry.
if (witness && accessory.operatorAdmissionError(ctx, *witness, nowMs).isEmpty()) {
    // Only AFTER the independent physical/native/job checks, never automatically:
    bool lease = accessory.beginOperator(ctx, *witness, nowMs);
}
```

Both bindings are strict64lowercase SHA256hex identifiers. Session binds the actual
connection/device/profile/commissioning identity. operationBinding identifies a
concrete start/job instance or focus attempt, not just repeatable GCode content.
It remains stable for that running job's Pause/Resume. Each resume requires a NEW
opaque witness (unique issuer and sequence), with action=Resume and the same job
binding. The worker should generate these identifiers and validate actual Prepared
identity/arguments independently; the UI cannot grant them from free text.

Human statements have a30s pre-admission freshness limit (explicit local software
policy), using the worker's monotonic clock. They reject future, regressing,
negative and stale timestamps. They are single-use, including a denied begin
attempt, and cannot transfer to another contract, copied contract, session, job
or action. A new statement replaces the old pending statement. The ordinary
begin(now) is unavailable in this mode; callers must use beginOperator with the
exact context and witness. preflightError may describe pending readiness; only
operatorAdmissionError/beginOperator performs definitive context admission.

A running attended lease does not expire as though it were a3s sensor observation;
it represents the operator's explicit undertaking to remain. No timer renews a
fake Flowing sample. Enclosure state remains Unknown, command remains Unknown,
verified(EnclosureExtraction) stays false, and request/observe cannot establish
an enclosure output/sensor in this mode. Required Air Assist still needs its own
independent verified contract. overridesError always denies the attended mode.

Pause retains the active job lease and invalidates pending consent. Resume only
accepts a fresh statement for that paused job; a focus pulse cannot resume.
Complete/Stop invalidate witnesses and end the lease but retain safe-clearance
requirements. Fault invalidates all accessory evidence; explicit safe clearance
and a new statement are required. Disconnect additionally discards session
binding, so a reconnect needs explicit new session setup. None emits OFF.
releaseAfterSafeClearance never creates a new witness. discardOperatorWitness
only retires pending consent when a source/preparation snapshot changes; it does
not disturb an already explicit frozen running job.

## Tests

operator_extraction_test exercises unchanged blocked defaults, human-vs-sensor
state separation, no output/ACK spoofing, validity and clock boundaries, one-shot
consumption/supersession, cross-instance/copy/session/job/action rejection,
Pause/Resume, focus, absence and lifecycle invalidation. Existing sensor contract
cases run unchanged. Tests only link Qt Core/Test; no serial, display, hardware
input or emission capability exists in these tests.

## Single-click supervised device-cycle start (beta.10.7)

For the ordinary device cycle, a sealed fresh native StartAttended input also supplies attendance for that exact prepared job. The worker validates controller state, calibration, coordinates, geometry and prepared identity before selecting this mode and privately creating/consuming the presence statement. Original input age is preserved (250 ms admission); no pending general permission is exposed. Programmatic calls, stale or replayed inputs do not authorize output.

This path is eligible only for device-cycle mode or the pristine unconfigured default. Explicit sensor selection, configured sensor mappings and external extraction mode cannot be replaced by it. It never calls releaseAfterSafeClearance. Actual faults/disconnects still invalidate execution. Pristine startup, planned verified S0 homing and deliberate Idle/S0 disconnect without an active lease do not fabricate an accessory fault. Fan/airflow states remain unknown. Focus and Resume retain their existing distinct admission.
