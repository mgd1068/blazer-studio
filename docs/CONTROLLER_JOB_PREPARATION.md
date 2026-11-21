# Bounded controller-output preparation

`ControllerJobPreparation` prepares immutable output data without touching a
controller. It accepts increasing, nonzero caller generations on its owning
thread. Exactly one snapshot can be computing and one latest snapshot can be
waiting. A newer request cancels the current computation cooperatively and
replaces the waiting snapshot; it never adds another task to an unbounded pool.
Unneeded raster previews are removed from the value snapshot without changing
the caller's plan.

The lazy compute-only QThread has no serial objects, GUI objects, QObject slots
or timers in run(). It calls `PreparedControllerJob::prepare` on copied values.
Completion can make data available, **never start a job**. The independent
controller runtime still owns every physical preflight, lease and dispatch.

The GUI API is `prepare(plan, generation)`, `cancel()`, `close()`, `preparing()`,
`reason()`, `generation()` and `result(expectedGeneration)`. Signals are
`changed()` and `finished(generation)`. Finished includes both successful and
failed data work. Consumers must re-read `result(id)` and `reason()` rather than
assuming a signal is a valid output or execution permission. Another signal
receiver may already have changed/cancelled the candidate. No older queued
notification can revive a cancelled, replaced or closed generation.

The small state mutex protects movement of values and identities only. Encoding,
scanning, discarded result destruction and superseded plan destruction occur
outside that mutex. This mutex is separate from the controller mailbox and is
never held by the controller runtime. Superseded values can be released on the
caller outside the mutex; this is not a hard real-time GUI latency guarantee.
Consumers must likewise avoid destroying large final output references in the
runtime or under the runtime mailbox lock.

Close permanently revokes admission, cancels active work, removes the owner
callback target and wakes the executor. Facade destruction does not join the
compute thread or delete a running QThread. The executor retains its plain data
state until the current finite/cancellable work unwinds, then queues its QObject
deletion in the creator thread. Application aboutToQuit requests close as well.
Queued callbacks use the QObject context and revalidate the state after delivery;
destroyed owners receive no result. This is cooperative cleanup, not a hard
wall-clock kill bound or protection against process/OS failure.

`controllerjobpreparation` tests exercise actual immutable output, a blocked
compute thread with 100 replacements (only generations 1 and 100 execute),
active/pending cancellation, stale and coalesced GUI notifications, two kinds of
signal reentrancy, rejected foreign-thread submission, preview exclusion,
invalid/throwing data work, destruction while work is deliberately held,
permanent close and repeated executor lifetime. Test-only barriers and an
internal work-function seam do not introduce a synchronous production mode.

This component's acceptance does not establish the complete runtime integration,
native input age, hardware coordinates, airflow, optical shutdown or LightBurn
parity. Those remain separate requirements.
