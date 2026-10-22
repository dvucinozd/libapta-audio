# Bounded segmented reference-path feasibility

Registered 2026-10-22 at clean `rust-rewrite` baseline
`35f3822e252376ce139c9a0ad508614e0ce82ffc`, before implementation or witness
measurements. The [fixed protocol](SEGMENTED-REFERENCE-FEASIBILITY-PROTOCOL.json)
owns all arithmetic, family constraints, native checks and capacities. This
follows the [timing attribution result](TIMING-FAILURE-AUDIT-RESULT-20261022.md).

## Question and bounded witness

Determine whether the existing native/wire segment representation can express
the existing reference beat path with period/phase tolerance and exact ordinal/
meter binding under the eight-segment wire limit. This uses labels and is not
a new detector or reference-to-production path. No audio, inference, new labels,
holdouts or reserved transfer are opened.

Each piece starts exactly at its first original reference beat/ordinal and
has one native millibpm-derived Q32 period. Intersect exact phase, period,
ownership, original-window, line-count and junction constraints, with a fixed
one-frame conservative guard inside the existing 0.10-beat / 1% tolerances.
Integer midpoint ownership cells partition the reference envelope; exact nearest
reference membership is checked separately. The first/last envelope bounds
remain reference-supported, never asserted full-source. No known PCM gap is
bridged: an intersecting declared gap rejects this particular witness family.

Every beat retains its original index, but intermediate coordinates can move
within the fixed bounds. This is a fitted reference-path witness, not lossless
reference-coordinate compression. A segment cannot hide extra natural line
points behind a shortened count. At a segment junction, also check the actual
successor interval to the next segment's exact reference anchor. The final
source successor is unknown, never invented.

Dynamic programming finds the minimum segment count **within this fixed anchored
family**, with deterministic longest-first ties. It is not a global feasibility
proof for every possible native grid. Preserve the full solution before applying
the eight-segment wire cap; report over-cap native-only graphs and unsupported
family outcomes without truncation or a change to tolerances/limits.

## Native and scoring evidence

Use a separate host Rust example to expand the Python witness through actual
`GridSegment::beat_at`, then exercise strict grid roundtrip and canonical bytes.
Include reference-supported same-grid meter in MTRD and complete native/external
builder validation. Select the first segment's actual TEMP and preserve existing
dynamic/provisional/pending-revision semantics. Complete native validation and
wire-cap acceptance are separate outcomes. Production validators and consumer
code/pins are unchanged; no trusted final playback result is produced.

Retain all 100 GTZAN sources / 600 windows and the original 401 complete contexts.
Reproduce every old coarse/OLS tuple and constant reference witness. Use the
unchanged common point/period/one-to-one diagnostics, separately showing nominal
segment periods and actual outgoing intervals. Report the 106 previously
constant-period-incompatible windows without filtering or rescoring old gates.
These are representation witnesses, not an increase in detector accuracy.

## Preflight and integrity

Commit/push registration, then test constant/changing periods, exact rational
boundaries, fractional native coordinates, minimum-count partition versus small
exhaustive cases, singletons, source/window/ownership boundaries, absent PCM,
unsupported tempos, over-eight capacity, strict roundtrip and meter ordinal/
coordinate corruption. Test actual complete native and builder positive controls.
Commit/push preflight before one clean-source fixed-cohort run.

Bind all source, prior reports/events/labels and native binary first; hash all
details and recheck after execution. Stop immediately on baseline or identity
mismatch. Private reference coordinates, IDs and per-source records stay outside
Git; publish aggregates and hashes only. Host budget is 180 s / 256 MiB, not a
physical P4 measurement. Core DSP, default features, release version, native
limits, confidence and consumer providers remain unchanged.
