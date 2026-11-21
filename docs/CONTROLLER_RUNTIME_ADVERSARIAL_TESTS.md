# Independent ControllerRuntime acceptance tests

`tests/controllerruntime_adversarial_test.cpp` exercises the actual production
runtime facade/worker and GRBL core against Root's independent Python PTY process.
It depends on Runtime checkpoint ac27197 (or its later integrated equivalent),
PreparedControllerJob, the C++ RuntimePeer wrapper, and peer option
`runOnPoweredCommand` (6bdf6e7). The fixture option retains synthetic Run after
powered dispatch so natural completion cannot mask a lease/watchdog failure.
It is not a physical plant, airflow model or commissioning acceptance.

## Cases implemented

- Serial/core/poll/ACK/focus/work-zero/accessory-watchdog QObject affinity is the
  worker thread, distinct from the test GUI. Default motion/extraction remain blocked.
- Actual requestStart(PreparedControllerJob::Ptr) cannot dispatch before private
  fresh contracts; after synthetic setup it emits the real program and owns a lease.
- A 20000-edge immutable candidate continues ACK-driven streaming and status polling
  during 400/2000-ms plain GUI sleep, with worker-context flow refreshes and no reset.
- Fresh NotFlowing and Unknown trigger a peer-observed reset within 250 ms, before
  the GUI returns from 400/2000-ms sleep. A later Flowing observation does not clear
  the fault or release extraction. No resume/accessory-off command is fabricated.
- Flow expiry occurs at its actual 3000-ms lifetime while status/ACK stay healthy;
  status expiry at 1500 ms while a worker QTimer keeps flow fresh. Both upper and
  lower bounds prevent a generic earlier GUI-liveness stop from faking causality.
- A private worker barrier queues a genuine prepared start, then STOP latches before
  release. STOP returns promptly without worker synchronization; no XY/M3/M4 from
  the cancelled start reaches the peer. The revocation generation advances.
- A positively exercised jog is followed by a queued jog held past its actual
  250-ms TTL; expiry prevents dispatch. An explicit worker FIFO fence plus ready
  status avoids inspecting the peer before a potential write could be acknowledged.
- Close/reopen onto another synthetic peer advances connection/revocation epochs.
  Old generation with current connection, and current generation with old connection,
  are injected separately into the private queue and rejected. A new jog still works.
- Destruction of an active runtime lease is bounded and stops its worker without
  GUI-owned callbacks, automatic resume or guessed accessory-off commands.

`ControllerRuntimeTestAccess` is the existing named private friend. It installs
synthetic motion/flow evidence and queries core state **only in queued worker
callbacks**. The helper semaphore/barrier is confined to tests; there is no
production synchronous mode or public/QML grant API. Scheduled flow timers are
children of the worker. All authority remains uncommissioned by default.

The child journal's global CLOCK_MONOTONIC `atNs` is compared with the test's
blockBegin/blockEnd. No qWait, processEvents, QProcess wait or GUI callback runs
inside a measured GUI starvation interval. A GUI QTimer stays undelivered during
the interval. A real GUI button click cannot be delivered through a blocked event
loop; these cases prove autonomous worker faults and mailbox revocation instead.

A test-only RuntimeOwner keeps the independent peer alive until asynchronous
facade teardown has destroyed its worker, including on QVERIFY early returns.
This prevents a premature fixture EOF from manufacturing a transport error.
Relevant cross-thread/timer/socket/missing-slot warnings fail tests.

## Reproducible integration

Root owns CMake. Linux-only example:

```cmake
qt_add_executable(blazer-runtime-adversarial-tests tests/controllerruntime_adversarial_test.cpp)
target_link_libraries(blazer-runtime-adversarial-tests PRIVATE blazer-core Qt6::Test)
target_compile_definitions(blazer-runtime-adversarial-tests PRIVATE
    BLAZER_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME runtime_adversarial COMMAND blazer-runtime-adversarial-tests -platform offscreen)
set_tests_properties(runtime_adversarial PROPERTIES TIMEOUT 90)
```

Initial independent validation: all 16 Qt results passed against a detached,
unchanged ac27197 source worktree, minimal actual Runtime+GRBL+Prepared module
build, 16056 ms. Observed flow-fault→peer-reset latencies were 10–30 ms; flow
expiry 3047 ms; status expiry 1548 ms. These measured software-fixture bounds are
not hard real-time guarantees, physical stop times or distances. No source/Main/
Machine/CMake changes or external port enumeration/opening occurred in this lane.

## Deliberately unresolved coverage

The existing core's general timeout is restarted by healthy Run/Jog/Hold/Home
status reports. Therefore an independent absolute ACK-loss deadline is not yet
proved by these passing cases; it needs an explicit policy and a separately causal
ACK-loss test. Status/flow success must not be mislabeled as ACK-loss coverage.

Additional staged cases remain focus native-input proof/deadline, work-zero expiry,
wire error/hangup/flooding, repeated destruction/FD baseline, and external typed
sensor epoch/binding rejection when an actual adapter ingress exists. These tests
add no sensor adapter or hardware grants. Raw GRBL `ok` carries no epoch/ID; host
epoch checks must not be misrepresented as firmware acknowledgement authentication.

### ACK deadline counterexample and conservative policy

A separately added case `missingAcksCannotBeKeptAliveByUnrelatedHealthyStatus`
starts a real 20000-edge prepared stream, then suppresses ACKs after at least
20 peer-observed commands. Flow is refreshed on a worker timer and status remains
fresh Run; the test explicitly confirms an outstanding ACK. After 9200 ms with
the GUI responsive, it sleeps the GUI for 2000 ms and requires the independent
10000-ms host ACK bound to expire before GUI return.

Against unchanged ac27197 the case fails in 11467 ms: no reset is observed. Fresh
Run status renews the core's general timeout indefinitely. The original passing
16-case normal/SAN evidence remains separate and is not presented as ACK coverage.
Root subsequently accepted a distinct conservative 10-second **host** ACK limit;
that can abort slow/planner-full operations and is not a firmware/mechanical
shutdown guarantee. Motion owns the product fix and its separate explicit host
Hold remaining-budget policy. The test remains a red counterexample until that
fix is validated on a new immutable checkpoint. Evidence and exact proposed test
patch are retained in `blazer-controller-runtime-tests-20261121/accessory-lane/`.
