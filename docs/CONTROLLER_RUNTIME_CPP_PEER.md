# C++ wrapper for the independent runtime timing peer

`tests/support/runtimepeer.h` provides header-only
`RuntimeTestSupport::RuntimePeer`, using QtCore QProcess and the existing
`tests/support/runtime_pty_peer.py`. It is test support, not production runtime
code. It does not implement a second firmware/PTY actor or open any serial/PTY
endpoint: it only returns the readiness endpoint created by its own child.

## API

```cpp
RuntimeTestSupport::RuntimePeer peer;
auto ready = peer.start(sourceDir + "/tests/support/runtime_pty_peer.py");
QVERIFY2(ready.ok, qPrintable(ready.error));
auto port = peer.port(); // The adapter under test may open only this generated PTY.
auto configured = peer.control({{"action", "set"},
                               {"values", QJsonObject{{"statusEnabled", false}}}});
auto scheduled = peer.schedule(150, {{"action", "report"}});
// Consume scheduling replies BEFORE deliberately blocking the GUI.
auto begin = RuntimeTestSupport::RuntimePeer::monotonicNs();
std::this_thread::sleep_for(std::chrono::milliseconds(400));
auto end = RuntimeTestSupport::RuntimePeer::monotonicNs();
auto journal = peer.snapshot(); // events[].atNs can be compared directly to begin/end.
auto exit = peer.shutdown();
```

Replies carry `ok`, `object` and a bounded static `error`; `diagnostic()` exposes at
most 2048 bytes of the child's retained stderr for local test debugging. Every
control gets a wrapper-owned ID; caller-provided IDs are replaced, responses must
match exactly. Readiness must state ready/syntheticOnly, match the actual QProcess
child PID and give a bounded `/dev/pts/<number>` string. JSON PIDs are never used
for process signaling. No endpoint supplied by a caller is opened by this helper.

The wrapper owns no QObject callbacks that must run while the GUI is blocked.
Use it sequentially on its constructing test thread, outside measured starvation
intervals. The child processes scheduled operations independently. Control/start
waits default to 3000 ms, with accepted caller bounds 1–10000 ms, 64-KiB frames,
64-KiB aggregate stderr and a bounded 4096-byte retained diagnostic tail. Protocol
errors poison the channel and stop its own child. Local invalid arguments are
rejected before submission and do not poison an otherwise healthy peer.

Shutdown closes the control write channel (EOF); the real peer exits cleanly.
After the default 500-ms grace, only that QProcess child is killed, with at most
1000 ms additional wait. Destructor uses the same path. Explicit shutdown waits
are clamped to bounded ranges. A failure path may include this bounded cleanup
in addition to its control deadline. There is no global PID/name cleanup and no
unbounded waitForFinished(-1). Construct/use/destroy on the same test thread.

Synthetic malformed-protocol scripts in the wrapper tests never open endpoints;
they only exercise readiness/correlation/output/time-budget and shutdown failures.
They do not duplicate the actual Root timing peer or simulate motion/flow.

## Tests and root CMake integration

`tests/runtimepeer_cpp_test.cpp` uses a QGuiApplication offscreen. Its 400/2000-ms
rows schedule child status events, then use plain sleep to block the GUI: a GUI
QTimer remains undelivered while child journal timestamps fall strictly inside
the sleep interval. A scheduled hangup likewise occurs before GUI return. Other
cases cover readiness, caller-ID replacement, bounded journal, repeated lifetime,
EOF, invalid actions, peer failure, forged PID/path/marker, malformed/oversized
JSON/stderr, mismatched correlation, hung child and forced own-child shutdown.

Root owns CMake registration. On Linux (the validated `/dev/pts` platform):

```cmake
qt_add_executable(blazer-runtimepeer-cpp-tests tests/runtimepeer_cpp_test.cpp)
target_link_libraries(blazer-runtimepeer-cpp-tests PRIVATE Qt6::Gui Qt6::Test)
target_compile_definitions(blazer-runtimepeer-cpp-tests PRIVATE
    BLAZER_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME runtimepeer_cpp COMMAND blazer-runtimepeer-cpp-tests -platform offscreen)
set_tests_properties(runtimepeer_cpp PROPERTIES TIMEOUT 30)
```

The wrapper/header requires CLOCK_MONOTONIC, matching Python time.monotonic_ns on
the Linux PTY test platform. These checks establish fixture independence and
bounded test orchestration only. They do not yet prove ControllerRuntime safety,
physical motion, airflow or GUI STOP-button delivery while the GUI is stalled.
Dependent lease/TTL/STOP/epoch tests must exercise the actual runtime against this
peer once its API and private ControllerRuntimeTestAccess friend are available.
No synchronous production option, public hardware grant, source mutation, installed
application access or real device was used.

Independent validation on 2026-11-21: 11/11 Qt results passed normally and with
AddressSanitizer/UndefinedBehaviorSanitizer for this wrapper/test executable
(`ASAN_OPTIONS=detect_leaks=0`; no leak-freedom claim). The original five Python
peer tests remain Root-owned and unchanged. No production library is needed to
build these fixture checks: QtCore/QtGui/QtTest plus Python standard library suffice.
