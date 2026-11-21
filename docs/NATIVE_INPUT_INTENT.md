# Native focus input and clock domains

`NativeInputIntentAdapter` is a GUI event collector, not a hardware grant. Machine
registers its actual focus QQuickItem. The application event filter captures a
spontaneous mouse press or non-repeating Space/Enter press on that active,
visible, enabled control before QML runs. Fake QML signals, programmatic Qt
events, synthesized mouse input and repeated keys cannot create a candidate.
Overlapping registered controls are ambiguous and reject. Consumption is
one-shot and checks the original window/control/context again.

The original event deadline is 250 ms, reduced by conservative clock uncertainty.
GUI callback time never starts a new budget. `Machine.beginFocus()` consumes the
candidate and seals that remaining lifetime with the runtime's connection,
revocation, window and focus generations. The worker independently rechecks it
and every existing motion/output/flow requirement. Legacy `focus(true)` has no
alternative grant. Release/cancel/window deactivation/hide, control replacement
or destruction, profile/binding changes and STOP revoke the context. A context
cutoff also rejects an old press first delivered after revocation. Suspend is
detected through the change in CLOCK_BOOTTIME minus CLOCK_MONOTONIC.

Supported domains are intentionally explicit:

- **Local KWin Wayland 6.6.6 / Qt 6.10.2 / native DRM**: the actual Qt Wayland
  display's Unix socket peer credentials must have the same UID and PID as the
  unique owner of `org.kde.KWin`. Asynchronous DBus readback must identify the
  reviewed versions, Wayland mode and DRM backend. Installed packages or session
  environment alone cannot enable this domain. Ownership changes invalidate
  it. The input timestamp is compared modulo 32 bits against explicit Linux
  CLOCK_MONOTONIC; quantization and mapping cost reduce remaining lifetime.
  This path accepts pointer presses only. Keyboard input-method forwarding may
  reuse a prior seat timestamp and cannot provide this proof.
- **Qt XCB**: a private InputOnly window on Qt's actual XCB connection receives
  asynchronous PropertyNotify probes. The full request sequence, own window and
  property must match; synthetic notifications reject. The host timestamps
  before request and at delivery bound the server offset. Roundtrip above30 ms
  or sample age above500 ms rejects; an additional7 ms uncertainty allowance is
  deducted. A server clock jump invalidates pending intents. There is no GUI
  wait loop, nested event processing or DISPLAY-based server guess.
- **Other versions/backends or missing evidence** remain Unknown and reject.

The KWin check is a trusted local desktop model approved for this host, not
binary attestation. It does not prove a physical human or prevent a malicious
same-user client/compositor from supplying input. Wayland does not expose the
libinput provenance of individual pointer events. KWin EIS/fakeinput pointer
paths generate local steady-clock time when the compositor processes the
request, rather than proving its remote origin time. This adapter's scope is
the OS-compositor-generated event, not earlier remote generation. Keyboard is
denied on this backend because InputMethod can reuse prior seat time. Generic
Wayland is not assumed to share CLOCK_MONOTONIC. Proc mappings may be inaccessible for nondumpable KWin;
this adapter never elevates privileges to inspect them.

The collector already supports action-keyed `registerControl(control, action)`
and `consume(action)`. Further Start/Jog/Home/Frame/Zero integration must consume
inside the corresponding Machine entrypoint, then seal an action-specific
worker proof and original deadline. Returning an authorization bool to QML and
later dispatching independently would separate the evidence from its action and
is not a suitable extension. Registration alone does not protect those other
entrypoints in this focus-only slice. Their existing real commissioning gates
remain closed. A release-based action also requires a deliberately specified
press/release binding: this hold adapter revokes on release.

Primary sources: [Qt event timestamps](https://doc.qt.io/qt-6.10/qinputevent.html#timestamp),
[Qt native-event filtering](https://doc.qt.io/qt-6.10/qabstractnativeeventfilter.html),
[Qt6.10.2 Wayland input](https://github.com/qt/qtbase/blob/v6.10.2/src/plugins/platforms/wayland/qwaylandinputdevice.cpp),
[KWin6.6.6 libinput events](https://github.com/KDE/kwin/blob/v6.6.6/src/backends/libinput/events.cpp),
[KWin forwarding](https://github.com/KDE/kwin/blob/v6.6.6/src/input.cpp),
[KWin EIS time](https://github.com/KDE/kwin/blob/v6.6.6/src/plugins/eis/eiscontext.cpp#L137),
[KWin fakeinput time](https://github.com/KDE/kwin/blob/v6.6.6/src/backends/fakeinput/fakeinputbackend.cpp#L62),
[KWin input-method limitation](https://github.com/KDE/kwin/blob/v6.6.6/src/inputmethod.cpp#L833),
[Wayland timestamp limitations](https://wayland.freedesktop.org/docs/book/Protocol.html#input),
[X11 timestamp protocol](https://www.x.org/releases/X11R7.7/doc/xproto/x11protocol.html#glossary).

Tests use private synthetic input/clock seams and offscreen windows; they do not
click real controls, open hardware ports, establish human provenance or grant
motion/homing/airflow commissioning. Independent PTY worker tests remain needed
for deadline expiry after capture and before actual output dispatch.
