# Proposed native action extension (not implemented)

The current implementation protects focus-on-press. Start, Jog, Home, Frame,
WorkZero and Resume still require a separate typed action integration; registering
a control with the collector alone does not protect those entrypoints.

Recommended C++ contract:

```cpp
enum class NativeAction { FocusHold, StartJob, Jog, Home, Frame, WorkZero, Resume };
// QML registration only; this creates no intent or machine permission.
void Machine::registerActionControl(QObject *control, NativeAction action,
                                    QVariantMap fixedArguments);
// No QML authorization bool. Existing Machine entrypoints consume internally.
// Private immutable worker proof, created only after native collector validation:
struct NativeActionProof; // action, argumentDigest, interaction, device/button,
                         // context/connection/coordinate/revocation epochs,
                         // original activation deadline, issuer seal
```

Registration validates fixed arguments in C++, such as finite bounded Jog deltas.
At an accepted native press the collector freezes a small action binding from
Machine: prepared source/profile fingerprint and start mode for Start, exact
Jog parameters, current Frame region fingerprint, or the relevant reference
frame identity. No full geometry scan or encoding belongs in the event filter.
Queued preparation completion can never submit an action by itself.

Hold actions consume the original fresh press, as focus does today. Click actions
need an explicit two-phase interaction: a fresh native press must first be
accepted and arm the actual control; a later fresh native release/activation can
complete only that same armed device/button/control/window/context interaction.
The release must itself be within250 ms of its original OS time. A product-defined
bounded hold duration, for example5 s, avoids an unexplained250 ms click-hold
restriction. This would be a distinct release deadline, not a renewal of an old
unaccepted press. Release alone, old press first delivered after a GUI stall,
autorepeat, canceled/hidden/replaced controls and programmatic QML signals cannot
arm or revive anything. Controls must report actual accepted pressed state at
arm time; activation must be bound to the corresponding accepted release.

Machine recomputes the small current action binding when its entrypoint is
called and consumes the matching one-shot activation. Any source, profile,
arguments or coordinate-frame mismatch rejects and requires another interaction.
It then passes the actual immutable request plus a private sealed proof to the
worker. Worker admission must compare proof action and payload digest with the
request kind/body, check original remaining deadline and current epochs, and
recheck every motion/homing/flow/output preflight. Connection epoch alone is
insufficient: reset, unexpected startup, homing and G54/WCO updates must invalidate
or advance a separately defined coordinate/reference epoch. There is no public
or JSON grant. Stop/Close remain unconditional priority revocations.

Required independent tests include each action cross-used as another action,
argument mutation, prepare completion after canceled input, fresh release with
never-accepted old press, valid long hold followed by fresh release, release
queued past its own deadline, coordinate/profile/source changes between phases,
overlays/unpressed controls, keyboard autorepeat, window/context changes and
STOP racing queued dispatch. Actual PTY wire output is the final oracle; GUI
flags or proof construction alone are insufficient. Wayland keyboard stays
unsupported until a conservative original timestamp path is established.
