# Exact wire coordinates and relative jog bounds

Independent review after the frozen beta.6 package found two numerical gate
defects. They were corrected in `d0df81c`; that fix is **not** present in the
archived `4b73fb4` package. Real movement contracts remain uncommissioned and
blocked by default. No physical device was used for this correction.

## Absolute G54 output

The encoder used `QString::number(value, 'f', 3)`, whereas the signed-envelope
check used `std::round(value * 1000) / 1000`. These are not equivalent at
binary/decimal ties. On the supported Qt build, input `0.0045` is encoded as
`0.004`; the arithmetic expression produces `0.005`.

An envelope with lower X = 0.005 therefore incorrectly admitted a command whose
actual X word was 0.004. An upper X = 0.004 could conversely reject valid output.
The same issue applies to Y and shifted signed envelopes.

`GrblWireNumbers` now supplies the unchanged encoder formatting and the numeric
value parsed from exactly that formatting. Absolute-output preflight uses this
value before adding the current WCO. Compact prepared-job bounds use the same
conversion on their raw corner coordinates. Raw reported machine coordinates
are never passed through this decimal conversion.

## Relative G91 jogs

A G91 jog adds the **encoded delta** to the actual starting machine point.
Rounding its resulting absolute target again can hide an overrun: raw MPos
0.0054 plus delta 1.000 reaches 1.0054, beyond an upper bound of 1.005. The old
check rounded that target to 1.005 and accepted it.

Jog preflight now requires a known in-bounds raw machine point and checks
`reportedMachinePosition + encodedDelta` against the raw signed envelope,
without a second rounding step. `insideRawMachineBed` is shared with reported
position validation. No new public commissioning or permission API was added.

## Regression evidence

An independent test-only change demonstrated all eleven new cases failing on
the previous actual core: eight X/Y, lower/upper and zero/negative-WCO cases
compare the real compact gate with independently parsed GCode targets; three
relative-jog cases observe an invalid ±1.000 command on a synthetic PTY. With
the fix, those cases pass, including positive acceptance of valid output and
the smaller ±0.999 jog. No always-deny implementation is accepted by the tests.

Normal targeted acceptance: GRBL 225 QtTest results, independent virtual plant
30, job output 13 and prepared output 17; four CTest groups passed in 56.93 s.
AddressSanitizer/UndefinedBehaviorSanitizer acceptance: GRBL, prepared output
and the independent C++ peer wrapper all passed (three groups, 63.17 s).
Leak detection was disabled; this is not a leak audit. The prepared-output
property corpus now includes six-decimal inputs and independently parsed wire
targets rather than only already-quantized three-decimal coordinates.

Local logs: `/tmp/blazer-exact-wire-coordinates-tests.log`,
`/tmp/blazer-exact-wire-coordinates-sanitize-tests.log` and the independent
baseline failure log `/tmp/prepared-wire-red.txt`.

These are software/virtual-coordinate checks, not physical origin, WCO,
measurement-resolution, airflow, shutdown or hardware acceptance. The separate
controller-thread migration and its independent GUI-stall acceptance remain
unfinished at this stage.
