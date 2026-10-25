# MC-05: public fixed-grid phase evidence

Registered 2026-10-25 before implementation on `rust-rewrite` baseline
`1dd78470d8d741e31904765bf7b5f55d8740ac6c`.
Authority: [MC-04](METER-CONTEXT-MC04-CONTRACT-REVIEW-20261025.md).
This is an opt-in host measurement prototype, not a detector or a new campaign.
MC-03 remains rejected; P3, MC-00, their outputs, thresholds and veto are immutable.

## Hypothesis and isolation

On an identical fixed grid and meter, all owned points with ordinal congruent to
the published witness describe the same meter phase. Their reference coverage
can be measured separately from the single published point. This diagnostic
does not establish improved musical accuracy or equivalence across grids.

`tools/apta_meter_phase_points.py` has no imports, references, IDs, I/O or CLI.
`extract_source(windows, *, total_frames, sample_rate, known_gaps)` returns one
record per input window, in order. No construction, fitting, stitching or gap
filling is permitted. `tools/apta_meter_phase_evidence.py` is evaluation only:
`evaluate_source(windows, truth, *, total_frames, sample_rate, known_gaps)` calls
the extractor, then unchanged `apta_context_matched.meter_evidence` for the original
publication and for every phase point. No existing dispatcher imports either new
module. In-memory separation is not a process sandbox.

## Closed inputs and exact geometry

All records reject extra/missing keys, bool-as-int, non-integer coordinates and
invalid types. Source: 1..1440000 frames, exactly 48000 Hz, explicit `known_gaps=[]`.
There are 1..8 ordered, nonoverlapping half-open windows, at most 3072 total owned
points per source. Each window has exactly `first_frame`, `end_frame`, `model`,
`points`, `meter`. References are not constructor arguments.

Model is null only for an empty, meterless window; otherwise exactly the existing
P1 fields: `anchor_frame`, `anchor_ordinal`, `tempo_millibpm`, `period_q32`,
`confidence`. Anchor is source-owned, ordinal nonnegative signed-64, tempo
40000..300000, confidence zero; period equals integer division
`48000*60000*2^32 // tempo_millibpm`. Positions remain full exact Q32 integers.
An owned model point has index j>=0, position `anchor_frame*2^32+j*period_q32`
and ordinal `anchor_ordinal+j`. Input must contain **every** such point in its
window, in order, within the source and ordinal bounds. A missing/extra/altered
point is invalid, not an opportunity to reconstruct an input. This binds exact
geometry locally; it does not prove independent upstream replay or source identity.

Points have exactly `position_q32`, `ordinal`. Meter is null or exactly normalized
`numerator`, `denominator`, `downbeat_frame`, `downbeat_fraction_q32`,
`downbeat_ordinal`; only 3/4 or 4/4, bound to one exact owned point. Normalize a
projection's richer meter/model record explicitly at the caller boundary.
Unsupported projections can use a null model/empty points/null meter; this is
absence, not grid or meter correctness. Raw projection failure reasons stay with
the caller and are not reinterpreted here.

## Closed outputs and scoring

Extractor record keys: `first_frame`, `end_frame`, `model`, `grid_points`,
`phase_points`. Returned models/point lists are detached copies. Grid points are
preserved in full; phase points K are exactly those with
`(ordinal-downbeat_ordinal) % numerator == 0`, or empty when meter absent.
Changing an owned witness by whole bars preserves K only on identical geometry.

Evaluator returns one record per window with exactly `phase`, `published`,
`phase_point_count`, `supported_point_count`, `correct_point_count`,
`E_supported`, `all_correct`, `conditional_accuracy`, `point_scores`.
`phase` is the extractor record; `published` is the unmodified canonical original
meter evidence (M/D and complete/incomplete mask). A point score contains exactly
`point` and `evidence`; evidence comes from canonical `meter_evidence` with the
original numerator/denominator and that point as temporary evaluation witness.
The temporary witness never replaces any publication or enters a projector.

K is reference-free. E includes **every** K point inside the inclusive reference
beat span. Canonical scoring retains source-order ties, local median and 0.10
beat tolerance. Each point is evaluated, including unsupported ones. Correct
point count measures downbeat correctness; meter correctness remains separately
visible in canonical evidence. Wrong meter can still have correct phase points.
No best-point choice, rate averaging or new tolerance. `E_supported` is 0/1;
`all_correct` is null when E empty, otherwise 0/1 requiring all E points correct.
Conditional accuracy is null when E empty, otherwise exact numerator/denominator.
Absent meter means K/E empty even if grid exists.

Truth uses the unchanged canonical closed record and validation. Missing reference
coverage means a disjoint valid span; absent/malformed labels are invalid, not
an invented score. Window counts and point counts are separate. This prototype
does not aggregate sources/groups/cohorts or pair grids; any later audit must bind
source/grid identity, window ordering, original M/D, complete/incomplete strata,
and point/window denominators at source/group/cohort levels before scoring.

## Public verification, resources and stop conditions

Only newly authored public arithmetic fixtures: both meters, equivalent witnesses,
Q32 carries and endpoints, wrong meter/phase, distinct grids, partial/full/disjoint
reference spans, null inputs, malformed types/shapes/budgets, input/output mutation,
maximum capacity and 8-window ordering. A test-only independent Fraction/median
oracle imports no production module. Compare complete extraction/evaluation records.
Import/I/O checks prove only the stated Python boundary. Existing P3/core files
must match registration hashes. No private artifacts, labels or saved cohorts open.

Caps inherited from MC-00: 60 seconds/source, 1800 seconds/suite, process RSS
262144 KiB, external output 67108864 bytes. Public checks run under Linux timeout
and address-space limit; report measured wall/RSS separately from those ceilings.
No candidate metrics or acceptance verdict is produced. Any oracle mismatch,
mutation, changed frozen source, invalid-input acceptance or cap breach blocks
completion. Fix public implementation errors with their regression tests; do not
amend frozen scoring. Commit/push only after the public checks and documentation.

After this checkpoint: review the prototype, then separately register any saved-
projection interpretation audit with source/runtime/input/output binding. No
automatic private replay, real inference, native port, consumer activation, P4,
confidence qualification or release. VERSION stays 1.0.1.
