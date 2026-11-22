# Accessory output and extraction contract

## Current supervised device cycle

The explicit `OperatorSupervisedDeviceCycle` mode requires a fresh one-shot
presence witness for the specific start/focus/resume operation. It does not
require, command or manufacture evidence for an external extraction appliance.
Both enclosure and laser-module fan observations remain unknown/not read back;
no independent fan-output mapping is claimed. The normal dynamic GRBL job cycle
is retained across laser-free G0 moves. See [device-cycle scope](DEVICE_CYCLE.md).
The sensor and external-appliance contracts below remain separate opt-in modes.


The real M3 enclosure fan remains unresolved. The failed 20 November hardware
acceptance is not superseded by these software changes. Neither a successful
controller ACK nor an enabled Air Assist checkbox proves that extraction is
running. No serial port, running application, or physical accessory was touched
for this work.

## Evidence and the removed assumption

The local LightBurn preference `AirAssistM7=false` and
`/home/mathias/Applications/001.nc` containing M8/M9 establish an auxiliary
command choice in an existing export. They do not establish the wiring or
firmware mapping of enclosure extraction. LightBurn's own documentation says
its setting selects M7 or M8 for **air assist**:
https://docs.lightburnsoftware.com/latest/Reference/DeviceSettings/BasicSettings/#air-assist
(accessed 21 November 2026).

The former encoder compared the entire output-profile fingerprint with the M3
reference and inserted M8/M9 for every matching job. There was no separate
extraction capability, verified mapping, or physical airflow observation. The
existing known-limitations warning already recorded M8 without confirmed fan
effect. Removing that assumption does not fix the physical fan. Unknown required
extraction must block physical processing, including a focus pulse.

## Interface and integration

`src/accessorycontract.h` is a transport-independent, in-memory safety contract.
It contains no M-code, port, arbitrary command payload, controller settings, or
physical command execution. It separates `EnclosureExtraction` from `AirAssist`.
Each role requires its own explicit output identity and trusted verification
evidence; aliases to the same identity are rejected. The class does not validate
the authenticity of that evidence. Only a trusted integration which has completed
the hardware verification may call `configure`; do not expose this as a JSON
flag, profile claim, or user-entered command bypass.

`preflightError(monotonicNowMs, airAssistRequired)` supplies an actionable reason.
The default requires extraction and fails closed on its unknown mapping. Runtime
consumers must check this before each physical processing start or focus pulse,
and before resume; an active operation must monitor the same condition and
request laser suspension if it fails. The motion safety gate is independent.
An output-capable backend must resolve `Request::outputId` through its own
verified mapping. Its OFF semantics must be independently safe: for example,
a global coolant-OFF command must never silently turn extraction off when only
air assist was requested off. No real M3 mapping is provided by this work.

`request` distinguishes intent, pending state, ACK/rejection, and actual airflow.
`acknowledge` rejects stale, duplicate, or superseded requests. It **never** sets
physical airflow. `observe` requires separate evidence and a monotonic timestamp;
flow observations expire after three seconds and future/regressing times are
rejected. The production observation source must provide continuous verified
flow feedback; a command response, cached ON value, or simulator reading cannot
be supplied as physical hardware evidence. With no suitable observation source,
required extraction remains blocked.

## Lifecycle

Requesting extraction ON latches a requirement for explicit safe clearance.
`begin` requires verified mapping, ACK ON, and fresh observed flow. `Pause`
retains the job lease; consumers must check airflow again before resume.
`Complete` and `Stop` release the job lease but retain extraction's clearance
requirement. `Fault` and `Disconnect` invalidate command state, pending ACKs, and
flow evidence. None of these events returns or sends an extraction OFF command.

After the job lease has ended, `releaseAfterSafeClearance(true)` records a trusted,
explicit confirmation that fumes have cleared and the fault is safely resolved.
It sends no command and provides no emission permission. Only then may a backend
request extraction OFF. There is no guessed cooldown duration. Air-assist
requests remain independent and cannot substitute for extraction evidence.

Software cannot ensure that an accessory remains powered through a controller
reset, emergency stop, cable fault, process crash, or power loss. Leaving OFF
unsent is a conservative host policy, not a fan-running guarantee. Devices need
their own verified extraction/interlock behavior and supervised acceptance.

## Independent mock and simulator checks

`blazer-accessory-tests` links Qt Core and Qt Test only. Its mock output identities
and simulated sensor evidence cannot access any physical transport. It checks
unknown and unverified defaults, separate roles, ACK versus actual flow,
rejected/stale ACKs, freshness and time regression, pause/complete/stop/fault/
disconnect, clearance gating, and simulator-only lifecycle. No physical device
acceptance follows from these tests.

Run:

```sh
cmake -S . -B /tmp/blazer-accessory-build -DBUILD_TESTING=ON
cmake --build /tmp/blazer-accessory-build --target blazer-accessory-tests -j4
ctest --test-dir /tmp/blazer-accessory-build -R '^accessorycontract$' --output-on-failure
```
