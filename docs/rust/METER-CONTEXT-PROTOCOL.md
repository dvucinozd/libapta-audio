# MC-00: bounded neighboring-bar meter evidence

Registered 2026-10-25 on `rust-rewrite` baseline
`1d16bffc159a6d6c406780e890d95eacd7df6ec4`, before new code or measurement.
The [machine contract](METER-CONTEXT-PROTOCOL.json) binds this document and all
baseline components. [P7](METER-CONTEXT-P7-DIAGNOSIS-20261025.md) establishes prior
exposure:25 saved boundary sources lose local two-bar evidence when one downbeat
moves across a window boundary. P6 remains rejected. This is a new representation
hypothesis on public arithmetic; no old candidate, score or contract is amended.

## Pure API and unchanged grid

Implement only `tools/apta_meter_context_projection.py`,
`tests/unit/apta_meter_context_oracle.py` and
`tests/unit/apta_1_1_meter_context.py` at MC-01.
API: `project_window(beat_frames, downbeat_ordinals, total_frames, first_frame,
end_frame, *, known_gaps)`. Inputs have the frozen P1 exact-integer/list rules,
maximum3072 events,1440000 frames,48000Hz. `known_gaps` must explicitly be an empty
list; missing, unknown or nonempty gaps cannot use this contract. There are no
IDs, labels, references, scores, confidence overrides, callbacks, I/O, environment
selection or solver calls. The source is complete and gap-free; future context is
available offline, not a causal streaming claim.

Call the unchanged P1 projector once. Preserve all its fit/model/points/grid status
fields exactly. A non-ACCEPTED grid cannot gain meter. An existing P1 owned meter
and its status are returned exactly, with `meter_origin=P1` and `context=null`.
This preservation applies even if hypothetical wider context could disagree.
Only an ACCEPTED grid with no P1 meter enters the new fallback.

## Bounded evidence and exact mapping

Take every observed downbeat inside `[F,E)`, the last at most two before F and
the first at most two at/after E, retaining source ordinal order. Examine every
consecutive triple of that contiguous slice. A candidate triple must satisfy:

1. Its two raw beat-ordinal differences are equal and3 or4.
2. Every witness maps to the unchanged grid using
   `j=floor(((raw_frame-A)*2**32/P)+1/2)`, `q=A*2**32+j*P`.
   Context j may be negative; publication still uses P1's owned points only.
3. Each q is source-valid, its `g+j` is nonnegative and below2**63, and exact
   distance `abs(q/2**32-raw_frame)` is at most the frozen rational OLS period/10.
   Both successive mapped-index differences must equal the raw meter numerator.
4. At least one witness's raw frame is in `[F,E)` and its exact `(q,g+j)` is one
   of the P1-owned points. Only those witnesses can be published.

There is no fit, threshold or event change. The1/10 tolerance is unchanged.
Collect all eligible triples before choosing. Each implies
`(numerator, (g+j_left) modulo numerator)`. If more than one distinct pair occurs,
abstain with `AMBIGUOUS_CONTEXT`; never choose a convenient meter or phase.
With one pair, choose the first eligible triple in source order, then its first
publishable witness in source order. With no eligible triple, abstain with
`NO_ELIGIBLE_CONTEXT`. Do not consult reference context or correctness.

Publish the same estimated-meter fields as P1: numerator, denominator4, exact
Q32 downbeat whole/fraction, model ordinal and the three original witness ordinals.
Successful fallback status is `CONTEXT_OWNED`, origin `CONTEXT`. New witnesses may
be outside the window; published downbeat must be owned by the same grid.

## Diagnostic record and cost

Return all P1 fields, except format becomes `libapta-meter-context-projection-1`
and registration_sha256 is this machine contract's digest. Add baseline_meter,
baseline_meter_status, meter_origin and context. The latter is null on the two
early-return paths; otherwise it has exactly downbeat_ordinals, examined_triples,
eligible_triples, hypotheses, lookbehind_frames and lookahead_frames. Hypotheses
are sorted unique numerator/phase_modulo records. Meter is null and origin null
on fallback abstention. All count/coordinate fields are exact integers.
Lookbehind is `max(0,F-min_context_raw_frame)`; lookahead is
`max(0,max_context_raw_frame-(E-1))`, both zero for empty context. These are actual
observed context spans, not measured processing latency or bounded milliseconds.

Additional work/storage is linear in source downbeats plus P1-owned points, with
at most four external witnesses. Do not copy a3072-point grid for every triple.
Limits remain60s/public source,1800s/suite,262144KiB/process,67108864 external bytes,
8 windows/source and3072 owned points across a source wrapper. Host prototype
costs establish no native/P4 allocation or inference feasibility.

## MC-01 public verification and stop

Use a separately written oracle importing only the frozen independent P1 oracle
and stdlib; it must not import new production code or its rounding/selection
helpers. Check exact full records. Independent nearest-point mapping may compare
the two surrounding exact candidates with upward half ties instead of sharing
the production rounding formula. Reuse P1's independent rational fit/grid oracle.

Required public cases:3/4 and4/4 boundary crossings in both directions; all source
and window endpoints; positive context; Q32 fractions/half ties; insufficient
events; malformed exact types/order/geometry/gaps; absent or insufficient bars;
conflicting numerators and equal-numerator phases; out-of-tolerance context;
bounded context selection; existing owned-meter preservation; raw input mutation/
label/import/I/O isolation; and a maximum3072-event bounded-cost case. Validate
owned output tuple/ordinal, deterministic context and source-window point budget.
Use parameterized arithmetic beyond the one minimal boundary example; no saved
C3 event/label/score cases enter MC-01. Verify raw-event invariance and equal
application to both public arms. Run unchanged P1 controls where dependencies
permit, reporting any unrun control explicitly.

An implementation discrepancy is repaired only in public preparation, preserving
failure logs. A contract counterexample stops for a separately registered review;
do not amend this frozen contract after observing it or silently weaken a veto.
There is no guarantee that this context rule preserves cross-arm support for all
possible events: that remains a measured gate, not a theorem inferred from one fix.

MC-01 public correctness is not a C3 matched campaign or component-bound runtime
qualification. A later MC-02 must minimally adapt the existing qualified phase
machinery to this separate module/protocol, freeze new loaded source identities,
qualify operational fixtures and separately register one new generated comparison.
Reuse canonical evaluation/oracles/resources; do not build a second scoring stack.
No old P5/P6 dispatch token selects MC-01. No real scoring is permitted here.

Before any real proposal, resolve the historical real-runtime H0 audit and scope
decision. Independent transfer, whole-pipeline product budgets, continuous full-
track representation, native adoption, physical P4, confidence and fresh final
acceptance remain separate. No model training, provider/pin, default, release or
VERSION1.0.1 change follows from this generated-only contract.
