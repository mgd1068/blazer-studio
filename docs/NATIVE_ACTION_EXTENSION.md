# Native action implementation and acceptance boundary

Machine now registers native controls for `start`, `jog`, `home`, `frame`, `zero`,
`resume` and `override` through
`registerNativeActionControl(control, action, fixedArguments={})`. Registration
alone grants nothing. Multiple controls per action have separate control tokens.
The control must expose readable boolean `pressed` and `nativeActionEnabled`
properties with notify signals. Semantic eligibility is checked at press,
release and consumption. Dynamic `nativeActionArguments` supply `{dx,dy}`,
`{scope}`, or `{feed,power}`; fixed arguments take precedence. Missing eligibility
or unobservable pressed/eligibility state rejects click registration.

The native collector records a fresh original OS press. The actual control's
pressed notification must accept it within the original250 ms budget before it
can arm. An already accepted hold may last at most5 s. A fresh original OS
release on the same control/window/device/button completes that interaction;
the release has its own250 ms deadline. An old or never accepted press cannot
be revived by release. Changed arguments, source, reference, eligibility,
replacement, context loss and cancellation reject. Busy-state button disabling
discards unused interactions but preserves an already consumed frozen request.
Focus release stays focus-only; unrelated queued jobs are not globally revoked.

Machine consumes internally, compares a cheap current binding (document and
selection generation, profile, prepared source and connection/reference epochs)
and seals a private immutable action proof. No QML authorization bool is passed
back for later use. Worker checks action, request-body digest, original deadline,
connection/revocation/reference epochs, control context and one-shot interaction
before dispatch, in addition to all existing motion/flow/homing/override gates.
Reference epoch changes on WCO/known-offset/reference/homing/reset transitions.
The sealer checks the expected epochs under the mailbox lock; it cannot silently
adopt a newer frame between GUI validation and sealing.

Raw unsealed runtime motion/override calls may report data admission but cannot
write those commands. Safe FeedHold remains unconditional and is a separate
request kind: it never toggles a delayed hold into resume. Resume requires a
sealed Resume proof plus actual paused state and current output/motion evidence.
The first normal write rechecks original input expiry/control revocation before
host acceptance commits the finite transaction. After that, a frozen active job
continues under worker status/flow/clock safety rather than an input TTL.
Host write acceptance is not physical movement or optical emission evidence.

Focus remains on-press. Wayland keyboard is still denied because the reviewed
KWin input-method path can reuse seat timestamps; pointer trust is limited to
the OS-compositor event time described in [the clock contract](NATIVE_INPUT_INTENT.md).
No motion, homing, airflow or override commissioning grant is introduced.

Own adapter tests cover accepted long holds, rejected old/unaccepted presses,
argument/eligibility changes, original deadlines and context revocation. Actual
runtime continuity tests use an explicit private synthetic native proof to keep
serial/watchdog acceptance independent of physical GUI input. Broad226/18 test
fixture migration and independent generic action/actual Main acceptance are
still required before integration is called complete. Private synthetic input
must not be described as real OS input or hardware validation.
