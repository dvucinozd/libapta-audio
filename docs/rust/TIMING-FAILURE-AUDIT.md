# Read-only timing failure attribution

Registered 2026-10-22 on clean `rust-rewrite` baseline
`1bc100402af93941167f5fefbe7598e2d800b44b`, before implementation or new
diagnostic results. The [protocol](TIMING-FAILURE-AUDIT-PROTOCOL.json) freezes
all categories, arithmetic, evidence and limits. This follows the
[rejected PCM refiner](PCM-ANCHOR-TIMING-RESULT-20261022.md).

## Question

The saved learned events pass broad event/continuity screening but their OLS
projection has only 187/401 joint-correct complete-context windows. Raw explicit
intervals, a longer-context fit and local PCM energy refinement do not provide
a safe replacement. Determine which failure dimensions deserve a new estimator
before starting another model/backend, context or threshold experiment.

Use only the existing 100-source GTZAN events, labels and coarse/OLS records.
No audio, network inference, new labels, reserved ASAP transfer or formal holdout
is opened. Reproduce every old tuple and score. Retain all 600 original windows,
with 199 incomplete reference contexts explicitly unknown.

## Fixed diagnostics

Apply the unchanged OLS projection to the reference events themselves as a
separate **reference-only constant-fit witness**. Score with the same unchanged
scorer and common point/sequence diagnostics. This is a label-based witness of
one representation, not a detector, oracle selection or accuracy ceiling.

Measure the existing nearest-five-interval local reference periods. Intersect
their +/-1% allowed constant periods across every annotated beat in the window.
An empty intersection diagnoses incompatibility for a complete one-to-one
sequence with a single period. It does not disprove an old precision-only grid
that omits beats, and does not distinguish musical tempo change from annotation
jitter. Preserve raw interval variation separately and do not change tolerances.

Classify the saved selected period relative to the median local reference
period using the seven ratios fixed in JSON (including half/double and
two-thirds/three-halves). Map each raw event to its nearest annotated beat with
the existing 0.10-beat phase tolerance; count mismatches, duplicate matches,
outside-window matches and uncovered reference beats. Report these as diagnostic
indications, never as independently verified annotation or event mistakes.

Use the registered priority order for one triage display group per window.
Also retain all overlapping flags, family counts, model/reference-witness
cross-tabs and genre aggregates. The exclusive group is not causal proof:
reference variation, model event mismatches and family ambiguity may coexist.
Preserve both original coarse-to-OLS regressions with full private detail and
redacted aggregate publication. No new candidate, confidence score or musical
promotion gate is introduced.

## Execution

Commit/push registration, implement and test positive constant timing, phase
offsets, wrong metrical levels, missed/extra/duplicate event correspondence,
reference context boundaries, variable periods, witness limitations, priority
versus overlapping flags and unchanged input/scorer behavior. Commit/push
preflight before the single clean-source audit. Save details outside Git.
Bind all code/inputs first, hash every record and recheck after the audit.
Identity/control mismatches stop immediately. Host caps are 120 s / 256 MiB.

The result may choose an evidence axis for a future separately registered
algorithm; it cannot select per-window parameters from labels or rescue a failed
candidate. Default DSP, native contracts/limits, consumer pins/providers,
releases and confidence remain unchanged. Independent transfer, fresh acceptance
and physical P4 qualification remain open.
