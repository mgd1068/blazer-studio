# Controller runtime ownership and limits

The production controller factory returns `ControllerRuntimeSession`. Its GUI
facade submits value requests and reads snapshots. There is no production
synchronous test mode. `ControllerRuntimeWorker` constructs `GrblSession`, its
serial port and timers on the worker thread. The same worker owns the actual
accessory lease, airflow checks and protective-stop watchdog. GUI timers only
refresh the display projection; they make no safety decisions.

`requestOpen` returns local admission, not an open port or a verified controller.
Every accepted Open has a correlated `connectionRequestFinished` result,
including cancellation. A successful result means only OS port-open; handshake,
reference and private commissioning evidence remain independent. `close` and
Machine's simulator switch are asynchronous: the real session stays visible
until the worker confirms closed. The normal window-close flow must wait for
that confirmation without blocking the GUI. Process `aboutToQuit` has a bounded
1500 ms last-resort join; this is not an asynchronous close acknowledgement or a
guarantee of physical shutdown.

The mailbox holds at most 16 normal requests, each with a 250 ms host admission
deadline and connection/revocation generations. STOP/Close use atomic latches
and increment revocation before waiting for any worker slot. The worker checks
these latches before reads and writes. A dequeued request retains a separate
in-flight reservation until its final snapshot is published, so GUI document or
profile guards cannot observe an empty queue and an obsolete Idle snapshot.
Nested Core notifications cannot release that reservation. Focus release uses
its own generation and never revokes an unrelated job.

Snapshots carry separate device-capture and worker-heartbeat times in the same
host monotonic domain. Reading a snapshot or consuming a queued GUI notification
does not renew either. Positive GUI readiness expires after 1500 ms device age
or 1000 ms worker-heartbeat age. Actual worker status/flow deadlines continue
while the GUI is blocked. A STOP click cannot be delivered while the GUI itself
cannot process native events; this does not provide an external emergency stop.

Large job scanning/encoding uses the bounded compute-only preparation component:
one active plus one latest candidate. Completion never starts output. The worker
rechecks current reference, work offset, exact wire-coordinate bounds and lease
at dispatch. Streaming uses an immutable line list with cursors; ACKs do not
detach or shift the complete list. Discarded prepared artifacts are retired on a
separate single-thread executor, outside the runtime and mailbox mutex.

GRBL status liveness is independent of ACK progress. Each actual line write has
a conservative 10000 ms host ACK budget; later writes, Run reports and fresh
airflow cannot extend the oldest outstanding deadline. Explicit host FeedHold
preserves the residual budget until resume. This policy may reject a genuinely
slow firmware/planner operation; it is not evidence that all GRBL firmware must
ACK within 10 seconds. The precise timer rechecks the monotonic deadline before
faulting. Receive work is limited to 32 outer lines per turn and 8 interleaved
status reports per line, with a bounded input buffer.

These bounds reduce event-loop stalls; Qt threads and timers are not hard real
time. OS scheduling, allocation, drivers, process failure and finite dispatch
work can still delay servicing. Runtime and airflow fixtures use a separate PTY
peer process so GUI sleep does not stop the synthetic controller as well.

Real motion, homing and override commissioning contracts still default closed.
No real airflow producer or trusted native-input clock adapter is supplied by
this thread refactor. Legacy `focus(true)` cannot bypass the sealed input proof;
Machine's real native focus entry remains closed until its OS event provenance
and clock domain are independently established. Timestamping a delayed callback
at receipt is insufficient, including for Start/Jog/Home/Frame. Firmware startup
blocks and physical coordinates remain commissioning requirements. None of the
PTY or sanitizer results establishes actual movement, optical shutdown,
extraction flow or hardware safety.
