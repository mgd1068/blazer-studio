# GRBL tests on the production runtime factory

`tests/grbl_test.cpp` retains all 122 original slot/data functions and their 225
Qt results. It adds one independent in-flight reservation regression (226 expected
results in total). No case is skipped/deleted, no production synchronous mode or
public/QML commissioning grant is introduced.

Explicit `GrblSession` instances remain raw-core tests. Machine and
ControllerSessions::create cases now require the actual ControllerRuntimeSession.
No Machine.session→GrblSession static_cast remains. The test-only named friend
ControllerRuntimeTestAccess queues every private core/lease read or mutation to
its owning worker. A bounded semaphore is a sequential test query/fence, never a
production runtime option. Context/field views copy data back; they never expose
a live core pointer to GUI code. Frozen before/after assertions explicitly retain
bool values, not live field-view bindings.

Synthetic reference/motion/homing/override/extraction contracts remain explicit
private fixture assumptions. Positive focus cases use a privately sealed synthetic
native-press proof and the real queued beginFocus worker path; they do not turn
legacy Machine.focus(true) into a public bypass or claim actual native-input/UI
acceptance. Prepared-start tests wait for the same jobPrepared admission condition
as the real UI; preparation completion never auto-starts a job.

The existing GUI-owned Controller remains the synthetic peer for these original
core/interface regressions. Its new opt-in runOnPoweredCommand timing flag changes
only the synthetic reported Run state after a positive powered program command;
default behavior is unchanged. This removes the false ordering of reporting Run
before an asynchronously prepared start was admitted. It is not physical motion,
laser, planner-completion or airflow simulation. Independent GUI-starvation tests
remain in controllerruntime_adversarial_test.cpp against the separate process peer.

Asynchronous close/stop/focus-off/useSimulator assertions await their observable
completion. The former GUI implementation-cache assertion is replaced with a real
Machine.changed consumer capturing processingSafetyErrors, preserving the intent
that idle airflow expiry notifies consumers without incoming status traffic.
The synthetic Controller keeps itself alive during asynchronous worker teardown
when its facade has already been destroyed, but intentional peer destruction with
a still-live facade remains a transport loss. This avoids fixture-created late
serial errors or hidden teardown dependencies on a destroyed peer.

## In-flight handoff regression

The new inFlightRuntimeDispatchKeepsHeavyWorkGuardReserved case inserts a private
barrier in the real core's pre-write path, after the runtime popped the normal
request. GUI reads of occupied/pending-or-busy/document heavy-work guard must still
show reservation while the worker is held. Cleanup latches STOP before releasing
the barrier, so no delayed command from the fixture is allowed to escape.

On checkpoint 6ffc675 this deterministically returned false/false/false and failed
in 162 ms. Related original heavy-work rows could reject Save but immediately
allow Import during the same handoff gap. Motion's dispatchInFlight fix 29d59ab
keeps reservation under the mailbox mutex across pop, nested publications, and
final dispatch snapshot. Against unchanged 29d59ab, the barrier returns
true/true/true; all ten original heavy-work rows plus the other previously failing
asynchronous cases pass (targeted 19/19, 7657 ms).

This lane changed only tests/docs. It opened only test-generated synthetic PTYs;
no external endpoint enumeration, hardware connection, installed application,
user project, host package or controller configuration was touched. These are
software protocol/lifetime regressions, not physical machine/fan acceptance.

Current full normal/SAN results are reported separately after completion. Root
owns CMake integration; this file replaces the existing tests/grbl_test.cpp target
without requiring a new production/test-mode build option.
