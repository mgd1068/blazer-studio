# Prepared controller output data

`PreparedControllerJob::prepare(plan, cancellation)` performs the potentially
large plan scan, encoding and line conversion outside the controller runtime.
Its result is `shared_ptr<const PreparedControllerJob>` with a private
constructor. It contains output data only: profile/motion fingerprints, driver,
transport/media type, immutable command lines, move count, first laser-off
approach target and finite raw G54 endpoint bounds. It retains no Document,
Artwork, image, renderer, live session or permission.

Preparation currently accepts only the existing GRBL serial XY output contract.
It reuses the existing encoder and its profile, continuity, power, feed, maximum
move count and source-binding checks. Empty or over-79-byte wire lines (including
newline) reject the whole candidate before dispatch. Cancellation produces an
invalid result with no command lines. Encoding/conversion and destruction of
large results must remain outside the runtime event loop.

The work bounds cover every actual rapid target and every emitting segment's
from/to endpoint, including the encoder's initial output-off approach. A rapid's
preview origin is not emitted and is excluded. Bounds are **raw work coordinates**,
not physical machine coordinates. Horizontal/vertical bounds can have zero size
in one dimension; QRectF::isValid/isEmpty is not the validity predicate for this
artifact. Call `valid()` first.

For the current straight-line XY encoder and rectangular signed machine envelope,
the runtime can check the bounds' corners using its current command-rounding,
WCO and signed-envelope predicate instead of traversing hundreds of thousands
of moves. It must independently check current **raw reported machine position**,
reference, modal state, connection/intent epochs, status freshness and airflow.
Preparation, matching hashes, cached data or an accepted queue request grant none
of those conditions. Curved/nonlinear mappings or other controller families
would need a different bound proof and are not silently supported by this type.

Runtime integration must keep the command collection immutable, use cursors
rather than `removeFirst()` (which could detach/copy a shared large list), and
retain a preparation/supervisor owner until worker references have been retired.
Final large-buffer destruction must occur outside the runtime. Preparation
finishing must never automatically start a job: it only makes data available for
a subsequent fresh explicit start intent.

`preparedcontrollerjob` tests use independently parsed GCode XY targets, initial
approach and rapid-preview-origin cases, a degenerate horizontal bound, cached
plan mutation/destruction, invalid/cancelled inputs, distinct device/motion
bindings, and 250 alternating fitting/non-fitting signed-offset envelopes. A
separate-thread preparation test uses a synchronous barrier only in the test;
it is not a production API or an independent-controller liveness proof.
