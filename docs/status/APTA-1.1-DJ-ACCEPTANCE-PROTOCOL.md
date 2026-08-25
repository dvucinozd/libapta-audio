# APTA 1.1 DJ-analysis acceptance protocol

This document freezes the final corpus-level acceptance contract for the native
APTA 1.1 DJ-analysis features before fresh manually verified validation results
are inspected.

It is a protocol, not an acceptance result. No production accuracy claim exists
until a fresh corpus satisfies every gate below.

## Scope

The evaluator covers the user-visible outputs that need independent labelled
reference data:

- musical key: tonic and major/minor mode;
- meter: 3/4 or 4/4;
- downbeat phase;
- beat period / grid phase;
- high-confidence error safety for each output family.

Tempo is represented by beat-period accuracy. The separate relation-aware tempo
ensemble protocol remains authoritative for the specific metrical-error
candidate introduced during Task 5.

## Corpus isolation

Acceptance evidence must use a corpus that was not used to choose DSP
thresholds, confidence mappings, relation gates or these acceptance thresholds.
The historical 188-track tempo corpus and its former 48-track holdout are not
fresh evidence for this gate.

The tracked manifest contains only opaque track IDs and evidence metadata. Track
titles, artists, source paths and audio are not committed by this protocol.
Ground-truth labels are SHA-256 bound by the frozen manifest before result
scoring.

A corpus with fewer than 48 labelled tracks is diagnostic-only and cannot
produce an accepted report.

## Frozen thresholds

The implementation in `tools/apta_1_1_dj_acceptance_eval.py` must match these
values:

| Metric | Acceptance gate |
|---|---:|
| Minimum fresh tracks | 48 |
| Exact key accuracy | >= 75% |
| Exact meter accuracy | >= 95% |
| Downbeat phase accuracy | >= 90% |
| Beatgrid accuracy | >= 90% |
| High-confidence threshold | >= 75/100 |
| Max high-confidence error rate, each family | <= 5% of all tracks |
| Beat-period tolerance | <= 1% |
| Downbeat phase tolerance | <= 0.10 beat, cyclic within the true bar |

Every condition is conjunctive: one failed condition rejects the candidate.
No weighted aggregate may compensate for a failed gate.

## Input contract

The frozen JSON manifest uses format `apta-1.1-dj-validation-1` and records:

- sorted unique opaque `track_ids`;
- exact `track_count`;
- SHA-256 of the labels CSV;
- UTC freeze timestamp;
- reference-source description;
- manual verification procedure.

The labels CSV provides exactly one reference row per manifest ID with key,
meter, downbeat frame and beat period. The results CSV provides exactly one row
per manifest ID with the corresponding APTA outputs and confidence values.

The evaluator rejects missing/duplicate IDs, malformed fields, out-of-range
confidence, unsupported meter values, a labels hash mismatch, or any result set
that does not exactly match the frozen manifest IDs.

## Correctness definitions

- **Key:** tonic and mode must both match exactly.
- **Meter:** numerator and denominator must both match exactly; native APTA 1.1
  acceptance is restricted to 3/4 and 4/4.
- **Beat period:** reported period must be within 1% of the manually verified
  period.
- **Downbeat:** cyclic phase error within the true bar must be no more than
  0.10 beat.
- **Beatgrid:** both beat-period and downbeat-phase checks must pass.
- **High-confidence error:** an incorrect family result reported with confidence
  >=75 counts as a safety error for that family.

## Evidence levels

`diagnostic-only` means the format and metrics are usable but the corpus has
fewer than 48 tracks. `acceptance` means the minimum corpus size is present; the
report is accepted only when every frozen gate also passes.

A passing self-test validates evaluator mechanics only. It is never corpus
acceptance evidence.

## Release boundary

Before `v1.1.0`, a release candidate must retain the frozen manifest, labels
hash, evaluator version, complete machine-readable report and source revision.
Any threshold or correctness-definition change after seeing fresh validation
results creates a new protocol revision and requires a new untouched validation
set.

## Official acceptance run — pre-run record — 2026-08-25

Frozen corpus (owner listening verification, workbench of 2026-08-25):

- labels SHA-256: `e7eac4ab8a80019b3da558c347d36242b827e485c7c66dff27c72fa8c25abbb8`;
- frozen UTC: `2026-08-25T18:48:18Z`;
- track count: 60 (all verified, none excluded);
- reference source: owner listening verification; prefilled aids were
  Rekordbox plus the independent estimator set only — APTA output was never
  shown in the workbench;
- verification procedure: owner confirmed or corrected key, BPM, meter and
  downbeat by ear for every retained track.

Disclosure recorded before the run: this corpus was previously analyzed with
automated labels as a diagnostic (2026-08-23). The tempo-family selection
changes retained on this branch (`c12b303` dominant S6 family choice,
`1dadfb2` common-time prior) were informed by that diagnostic, so their
meter/period results here carry reduced evidential weight. Key, downbeat and
beatgrid accuracy were never improved using this corpus. Thresholds above
were frozen before any of those runs and are unchanged.

Analyzer for this run: HEAD binary at source revision `ace15b5` tree,
one pass over the exact frozen corpus, then FINAL-only export and this
evaluator with unchanged thresholds.

## Official acceptance run — result — 2026-08-25

Executed as recorded above. Verdict: **rejected** (report JSON
`14006b0f55fe31aec04a8a0407a85c7094f137390d48cee354a50aec338cb22c`).
Key 25.0% (fail), meter 96.7% (pass), downbeat phase 8.3% (fail), beatgrid
6.7% (fail), key high-confidence errors 16.7% (fail), grid high-confidence
errors 40.0% (fail), meter/downbeat high-confidence errors 0% (pass).