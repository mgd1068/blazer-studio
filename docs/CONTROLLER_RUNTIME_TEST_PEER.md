# Independent runtime PTY timing peer

`tests/support/runtime_pty_peer.py` creates its own synthetic PTY in a child
process. It accepts no external endpoint and uses only the Python standard
library. It is a timing/transport fixture, not a physical GRBL or airflow model.
No fixture response grants a production machine contract.

Why a separate process: the existing Qt test controller's QSocketNotifier runs
in the GUI/test thread. Blocking that thread also stops its simulated firmware,
so it cannot establish that runtime status/ACK handling works during a GUI stall.
The child process continues independently and timestamps wire observations with
`time.monotonic_ns()` before the parent returns from its deliberate block.

Start with `python3 -u tests/support/runtime_pty_peer.py`. Read one JSON readiness
line containing `port`, `pid`, and `syntheticOnly: true`; open only that generated
PTY from the adapter under test. Control messages are one JSON object per stdin
line. Each synchronous response contains the caller's `id`.

| Action | Fields / result |
| --- | --- |
| `set` | `values`: state, position/offset XY pairs, statusEnabled, ackEnabled, resetEnabled, runOnPoweredCommand |
| `schedule` | delayMs 0–10000; operation with action set/reply/report/disconnect |
| `report` | Send synthetic status using current configured values |
| `reply` | data: explicit ASCII protocol input for adversarial adapter tests |
| `snapshot` | Return counts plus newest 256 events with ms/atNs/kind/value |
| `disconnect` | Close the PTY master; retain the control channel and journal |
| `quit` | Exit; control EOF also exits |

Schedule operations and consume their acceptance replies **before** blocking the
parent. Scheduled operations produce no stdout acknowledgment. Wire events are
retained in the bounded journal, not streamed into an unread stdout pipe. Read a
snapshot after the GUI returns, then compare event `atNs` with the parent's
monotonic return timestamp. This distinguishes pre-return wire activity from
delayed Qt delivery. Do not schedule future operations through GUI callbacks.
Do not request repeated snapshots while the parent cannot consume them.

The explicit `runOnPoweredCommand` timing option defaults to false. When enabled,
a positive S word on M3/M4/G1 sets synthetic Run; Run remains until reset or an
explicit state update, even after M5. This lets a test distinguish ACK delivery
from controller completion. It is not a firmware planner or physical model.

The peer responds to the bounded handshake, status queries, settings/modal
queries, ACKs and Hold/Reset/Resume. It records override bytes without assuming
firmware processing semantics. It does **not** model physical movement, laser
power, air extraction, GRBL planner completion or homing switches. Flow tests
need an independent worker-context synthetic producer and separate contract
fixtures. This fixture alone proves no runtime improvement; tests must exercise
the actual production runtime and inspect the independently recorded outcome.

Run `python3 tests/runtime_pty_peer_test.py -v` (also CTest `runtime_pty_peer` on
Unix). Six fixture checks cover handshake/realtime, scheduled state/status while
the parent sleeps, independently timed hangup, explicit synthetic busy mode, status silence with continuing
ACKs, fragmented input and bounded journal retention. Processes exit on EOF;
test failures have bounded waits and kill only the specific child if necessary.
