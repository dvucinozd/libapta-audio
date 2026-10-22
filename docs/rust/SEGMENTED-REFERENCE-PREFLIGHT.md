# Segmented reference witness implementation and preflight

Date: 2026-10-22. Registration was committed and pushed at
`0815e82fc72925ce11796d3a11fb6e6b189c37d3` before implementation or witness
measurements. The [protocol](SEGMENTED-REFERENCE-FEASIBILITY-PROTOCOL.json)
remains unchanged, SHA-256
`483fca380e70810d793ca02c739a342f3c720ad370823e28a53f557b40e991c1`.

`tools/apta_1_1_segmented_reference.py` intersects exact Fraction/Q32 constraints
for reference-anchored pieces, converts their period bounds to exact feasible
integer millibpm intervals, and finds the minimum piece count within that family
by dynamic programming. It preserves the entire solution before applying the
eight-segment wire cap. Ownership, source/S6 bounds, reference phase, own period,
actual junction interval and natural-point count are checked independently after
expansion. An intersecting declared PCM gap refuses this family's entire graph.
No audio or inference is read, and no reference-selected parameter becomes a
detector decision.

`rust/examples/segmented_reference_probe.rs` independently expands segments with
actual `GridSegment::beat_at`, verifies source coordinates/ordinals and the
registered guarded reference constraints, then exercises strict/canonical GGRD,
MTRD write/parse/grid lookup, complete native validation and the external builder
with meter included. Its fixture constructors use the same public APIs as the
earlier event probe; production validation code is unchanged. Graphs and meter
stay provisional with pending REVN, uncalibrated zero confidence and external
import provenance. No trusted owner/playback adoption is attempted.

Verification passes **15 new Python tests**, including exact tempo-integer bounds
against exhaustive integer search, constant/fractional paths, slow drift and
actual junctions, dynamic-programming partition/tie results against exhaustive
small partitions, single-beat pieces, meter/index binding, offsets, S6 ownership,
gap refusal, unsupported tempo/context, capacity and corrupted point/count data.
The prior 17 audit, 18 multi-beat, ten representation, ten neural-reference and
six annotation tests also pass: **76 Python tests total**.

Seven new Rust tests and 26 existing builder/grid/native-validation tests pass
in debug and release with warnings as errors: **33 per profile**. Positive
constant and changing-period graphs pass full native/builder/wire checks.
Fractional Q32 meter binding passes; corrupted meter coordinate or ordinal fails
complete graph validation. Nine segments pass native-only validation and fail
wire/builder capacity without truncation. A negative fixture initially used a
0.83% period change, which is legitimately inside the 1% gate; the test now
preserves that positive case and rejects a >1% change. No guard was changed.

Rustfmt, focused Clippy with `-D warnings`, no-default-features library check
and the release probe build pass. Three generated end-to-end Python-to-Rust
fixtures (constant, fractional and slowly changing paths with same-grid meter)
also pass exact coordinate/ordinal and all native/wire/meter checks.

External verification script, full log and synthetic bridge script are under
`D:/AI/LIBAPTA/outputs/rust-segmented-reference-20261022/`. The release binary is
`/home/daniel/.local/share/libapta-audio/rust-rewrite/segmented-reference-20261022/target/release/examples/segmented_reference_probe`.
The standard-library Python suite is added to `rust/check.py`. This is focused
host verification; full Rust/C/sanitizer, hosted CI, actual product-owner and
physical P4 qualification are not claimed. Native/core code, prior scorers and
candidate tools, production pins/providers and limits are unchanged.

Commit/push this preflight before the single clean-source 100-source witness
run. Bind prior baseline reports, source and the native binary first; recheck
all inputs and per-source hashes afterward. Cap/unsupported-family outcomes
remain diagnostics, not a reason to adjust thresholds, anchors or limits.
