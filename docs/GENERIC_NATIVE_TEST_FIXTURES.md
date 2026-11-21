# Private synthetic native input in existing runtime tests

Product basis: immutable8740228 plus Machine callout lifetime fixbc64658 and
fresh reported Hold followup000970c. The existing226-result GRBL migration and
18-result runtime suite were seeded byte-for-byte from Root's already integrated
files. Those seed commits are not new integration changes.

`tests/support/syntheticnativeproof.h` is test-only. Its named private friend
constructs an explicitly synthetic post-acceptance activation. It does not claim
OS delivery or a physical human, install controller grants, change clocks in
production, or add a production synchronous mode. An offscreen carrier stays
owned by the adapter until its Machine is destroyed. The fixture calls actual
Machine.registerNativeActionControl first, preserving destruction/window/context
connections, then privately supplies the accepted stage with a real current
Machine binding provider, fresh monotonic deadline, and shared revocation token.
The ordinary QQuickItem intentionally models only that accepted stage; actual
Button metadata/OS capture eligibility needs the separate native adapter tests.

Machine cases still execute the actual Machine start/jog/home/frame/work-zero/
pause/override entrypoints, including their guards, current payload binding,
consume/seal and actual worker dispatch. Denied Machine contexts cannot acquire
an accepted synthetic binding. Raw GrblSession protocol cases remain raw. Existing
private queued Core transaction/abort setup is still explicitly fixture setup,
not evidence that an unproven public action was admitted.

Factory/runtime cases without a Machine use only the private Runtime friend
sealNativeAction/submitNativeAction seam. The request digest binds exact typed
action, arguments and prepared output; original deadlines, connection/coordinate
epochs and revocation stay checked in the worker. Actor stale-generation and
stale-connection injections carry otherwise-current native evidence so missing
proof does not mask their original denial purpose. The mailbox admission test
still distinguishes enqueue from execution, with no physical override/flow grant.

No original assertion or slot/data function was removed: baseline939 GRBL and157
Actor assertion macros remain, with one additional actual Hold-report wait before
positive Factory Resume. The new holdConfirmed field is a fresh controller report,
not a physical standstill or laser-off claim.

Initial focused verification:18/18 Actor results pass normally in28106ms; seven
GRBL smoke results pass in2968ms (dispatch reservation, two work-zero layouts,
actual Factory Hold/Resume and focus extraction lifecycle). Full226 normal and
complete core ASan/UBSan builds are running; this initial commit does not claim
those later results. No physical ports, input injection, hardware or installation.
