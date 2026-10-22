# Free-phase reachability implementation and preflight

Date: 2026-10-22. Registration was committed/pushed at
`0a180b4cf46f6dd3f39ab8f9bda0097443c4179d` before implementation or cohort
measurements. The [fixed protocol](FREE-PHASE-REACHABILITY-PROTOCOL.json) has
SHA-256 `54a5cac6ee6ce86c82589fd34728fc54d9a9a5bf0b66ba5c67d5f473a9b71f41`.

## Implementation

`tools/apta_1_1_free_phase_reachability.py` is an explicitly invoked host-only
stdlib diagnostic. It reuses the unchanged segmented-reference ownership and
local-period preparation, with exact rational arithmetic thereafter. No native
code, prior candidate, scorer, format, limit, dependency or production option
changes. `rust/check.py` includes the new unit suite.

Per-cell natural geometry eliminates its independent continuous nominal period
at the feasible closed maximum. Interval intersection preserves the strict
previous-natural-point bound and the inclusive next-natural-point bound.
The forward interval recurrence and backward reconstruction produce a complete
rational timing path or an exact first-empty-prefix record. Nominal periods
are auxiliary geometry witnesses, not a quantized grid or actual intervals.

The independent checker operates on the uncombined ownership, geometry and
successor difference inequalities. Bellman-Ford uses integer-scaled rational
bounds plus symbolic strictness. It detects both negative cycles and strictly
negative infinitesimal zero cycles, with a separately checked cycle sum. No
floating-point epsilon is used in either feasibility decision. The retained
common diagnostic scorer is unchanged and is separate from the exact solver.

The runner refuses dirty source and existing output, binds all tracked source
and fixed private inputs before measuring, reproduces all prior anchored plans
and frozen coarse/learned/reference scores, checks old native witness inclusion,
validates every rational witness and rechecks every input/detail hash at end.
It preserves all denominators, unknown successors and no-path records. It
does not output successful partial prefixes, meter, FINAL/native results or
detector confidence.

## Verification

All **100 Python tests pass**: 24 new reachability tests and the 76 previous
segmented/timing-audit/multibeat/representation/neural-reference/annotation tests.
The new suite covers:

- exact open/closed intersections and sums, singleton contradictions and
  deterministic reconstruction;
- 729 exhaustive two-variable integer chains, 120 seeded strict rational chains
  against the independent graph checker, and tiny rational cycle weights down
  to `1/10^100` without a numeric epsilon;
- a generated reference sequence that has no anchored-family path but does
  have a continuous free-phase path, and an actual reference-gap contradiction
  with both empty-prefix and negative-cycle certificates;
- cumulative drift, actual interval corruption, source/window/ownership ends,
  fractional/changing native witness inclusion, nominal tempo limits, negative
  guards, declared gaps, missing context, invalid partitions/event caps, hash
  rejection and forced solver/checker disagreement.

The unchanged native control passes **33 focused Rust tests in each debug and
release profile**: 10 builder, seven grid, nine native validation and seven
segmented probe tests. Rustfmt, focused warnings-as-errors Clippy and
no-default-features library checks pass. These controls do not serialize or
qualify the new continuous paths. Full Rust/C/sanitizer/CI and physical P4
matrices are not rerun or claimed by this host-only change.

Verification script and log are retained outside Git at
`D:/AI/LIBAPTA/outputs/rust-free-phase-reachability-20261022/`.

Commit/push this implementation and preflight before one clean-source
100-source measurement. No labels/audio/IDs or witness coordinates are committed.
The 47/100 previous native result and all earlier rejected candidates stay fixed.
