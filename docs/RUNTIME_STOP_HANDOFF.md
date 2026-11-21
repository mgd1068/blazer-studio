# STOP handoff reservation

STOP acceptance sets an atomic latch and removes queued normal work. Previously
the worker consumed that latch before changing the Core or publishing a snapshot.
If the last snapshot was Idle, a concurrent GUI read could observe no latch,
empty queue and no in-flight reservation, allowing synchronous document work
while the accepted STOP had not reached the controller.

`takeControl()` transfers the consumed STOP/Close latch into the existing
`dispatchInFlight` reservation under the same mailbox mutex used by status reads.
Nested Core publications cannot release it. `publish(true)` atomically publishes
the resulting state and releases the reservation. Shutdown also publishes the
actual closed snapshot before releasing it, so no reservation survives cleanup.
Command ordering, native/motion grants and reset bytes are unchanged.

The independent regression uses an owned PTY peer with reset banners disabled,
a private fixed clock interval and an unset-in-production handoff probe. A queued
unsealed Jog is discarded by STOP. The probe pauses exactly after latch transfer,
before Core.stop; it observes the actual Machine and Document guard, then checks
the reset wire counter and absence of Jog. It provides no motion/flow grant.

The pre-fix instrumented boundary reproduced:

| Observation | Before fix | After fix |
| --- | --- | --- |
| Core connected / resetting / fault | true / false / false | true / false / false |
| Raw STOP / last published busy | false / false | false / false |
| Dispatch reservation | false | true |
| Facade occupied / pending / document guard | false / false / false | true / true / true |
| Peer reset count before releasing probe | 1 | 1 |
| Peer reset count after releasing probe | 2 | 2 |

Thus the failure was a handoff gap, not an already fault-closed session. The
regression also requires that the guard stays blocked while reset is pending and
becomes free only after an explicit synthetic reset banner and fresh handshake.
RelWithDebInfo pre-fix failed in188 ms; the fixed three Qt results passed in222 ms
(CTest0.26 s). No physical device, real suspend or hardware validation is involved.
