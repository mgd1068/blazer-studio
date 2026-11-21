# GUI-thread synchronous work guard

This is a narrow admission guard, not a GUI real-time guarantee or a physical
safety release. ControllerSerial, GRBL polling/status handling, STOP dispatch,
focus deadline and Machine's extraction watchdog still share the GUI event loop.
Their timers cannot preempt synchronous GUI work. Current default physical motion
and extraction grants remain unchanged.

## Integration contract

`Document::addSynchronousWorkGuard(std::function<QString()>)` adds an owner-supplied
read-only, nonblocking callback. Empty means permitted; the first nonempty reason
blocks; exceptions block with a fixed reason. Callbacks must not dispatch events,
mutate Document, access devices/files, or perform expensive work. Registrations
are additive, so another service cannot silently replace an existing restriction.
Capture each owner using QPointer; a destroyed owner returns empty.

Machine owns the real-session predicate: include busy/firing and actual controller
leases (including handshake, reference/home, zero, reset and paused output until
that lease ends). A simulator does not block. A connected but genuinely idle
session need not prohibit preparation of the next design. Wire state changes to
`Document::notifySynchronousWorkGuardChanged()`. The read-only QML property
`synchronousWorkBlockReason` uses signal `synchronousWorkGuardChanged`. This patch
does not itself modify Machine or Main; its owner must register that predicate.

`permitSynchronousWork()` checks and reports the reason. Admission occurs before
file access, external process launch, clipboard access, mutation or expensive
conversion for import/open/save/templates, native preview/review import, SVG/model/
raster/relief import, clipboard, restore, synchronous exports, page copying, node
editing, art insertion, ArtLibraryController conversion/new-thumbnail admission and expensive command conversions/raster/boolean/nesting
operations. Direct `projectBytes()` returns empty when blocked. Node enumeration
and framing-bound queries return no result while blocked. Light selection, bounds,
metadata, naming and alignment editing remain available; the running plan remains
an immutable separately captured value.

The guard is not a substitute for existing planning/atomicity/stale-source checks.
Workers already operate on immutable values and retain their own cancellation and
stale-result rules. No generic pause/resume or hardware permission is added.

## Recovery

Recovery checks the same guard before full serialization and AtomicFile backup/
commit, and before restore/cleanup IO. Its `deferred` property indicates postponed
work. Dirty changes continue to update the live Document; nothing is marked clean
or discarded. A queued immediate retry follows guard notification; a 250-ms retry
is a fallback if a notifier is missing. Retry serializes the latest still-dirty
Document, not an earlier snapshot. A clean replacement cancels stale autosave;
a dirty replacement is saved as the new document. Explicit discard decisions can
be accepted while their filesystem cleanup waits. Recovery availability is cached
for the locked session rather than statting the filesystem on every QML read.

Closing or destroying Recovery cancels its timer. Destroying Document invalidates
the QPointer and closes the recovery session. No delayed callback revives it.
When session closure occurs while filesystem work remains blocked, existing
recovery files are retained rather than synchronously removed.

## Independent evidence

`tests/synchronouswork_test.cpp` uses only temporary files, an offscreen Qt GUI,
and a fake `inkscape` shell executable. The fake sleeps 300 ms then fails. With
admission allowed, the actual PDF import path delays a scheduled 20-ms GUI callback
until after the process finishes (308 ms in the initial run). With admission
blocked, no fake process launch marker is created, and the callback runs normally.
This reproduces host scheduling delay, not physical machine behavior.

Other tests cover additive guards and destroyed owners; blocked file/clipboard/
geometry operations without mutation or overwriting sentinel files; allowed light
next-design changes; actual 1.5-second autosave expiry while blocked; latest dirty
generation, document replacement, close/destruction and deferred discard cleanup.
Tests do not open serial ports or grant motion/extraction.

Integration test-target snippet (the root build owns CMake registration):

```cmake
qt_add_executable(blazer-synchronouswork-tests tests/synchronouswork_test.cpp)
target_link_libraries(blazer-synchronouswork-tests PRIVATE blazer-core Qt6::Test)
add_test(NAME synchronouswork COMMAND blazer-synchronouswork-tests -platform offscreen)
```

## Remaining architecture work

Rendering, Qt font/path layout, large model notifications, library/settings IO and
arbitrary filesystem latency can still delay the GUI thread. The guard does not
bound every GUI callback or make timer deadlines hard limits. Rendering caches and
bounded geometry views should avoid expensive refreshes while output is active.
Full IO offloading requires an immutable persistence snapshot and latest-generation
commit rules; moving only disk writes while leaving JSON/image encoding on the GUI
thread is insufficient. PDF conversion should eventually use async process signals
or a worker-owned process, bounded outputs and stale-result rejection. A dedicated
controller event-loop thread with queued value messages is the stronger isolation
boundary for serial handling/watchdogs. Calling processEvents inside expensive
work would introduce reentrant mutation and is not an acceptable replacement.
