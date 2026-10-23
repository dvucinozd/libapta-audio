# Dense decoder: frozen-output interval failure diagnosis

Date: 2026-10-23. **Audit complete; DD-05 remains REJECTED.**
[Scope fixed before private inspection](DENSE-DECODER-DIAGNOSIS-SCOPE.md).
[Exact aggregate evidence](DENSE-DECODER-DIAGNOSIS-20261023.json).

The read-only tool reproduces all 600 saved actual-window scores on the original
100-source cohort, including 401 complete and 199 incomplete reference contexts.
It changes no decoder, event, threshold, metric, model or production behavior.
No raw logits/audio, new inference, alternative candidate or formal holdout is
used. All label-dependent findings are post-hoc on already-observed development.

## What caused the registered actual-window veto

**All 95 original actual joint regressions have known-period errors only.**
Their phase remains correct, they have full reference context and known
successors. Missing end support and cross-window successor handling explain
none of these 95 regressions.

The 401 complete-context windows separate exactly into:

| Actual-window outcome | Windows |
|---|---:|
| Pass | 9 |
| Known-period error, phase correct | 293 |
| Phase and known-period error | 95 |
| Empty | 4 |

The remaining 199 windows retain incomplete-reference status and overlapping
diagnostic reasons in the JSON. Unknown successors occur within that incomplete
group; they are preserved rather than replaced by nominal or previous periods.
Across nonempty windows, 495 final window events have a known cross-window
successor and 99 have the final source-unknown successor. Only 55 unknowns enter
the scorer's reference-supported point counts, as in the frozen evaluation.

Of the 190 nominal joint-correct windows, **181 fail actual joint**: 177 solely
on known periods, four on both phase and known periods. This directly explains
why OLS projection improves to 190 while actual joint remains 9. OLS estimates
one period from the window; actual scoring evaluates every next-event delta
against its local reference period with the registered 1% tolerance.

## Row timing and saved refinement both matter

For each of the **5922 known, reference-scored intervals**, the audit decomposes
the original emitted interval exactly:

`actual_delta = 960 * delta_row + delta_saved_refinement`

The original emitted point's nearest reference and local-period target stay
fixed. The following is an arithmetic attribution at those existing matches;
it is not a decoded/scored row-centre alternative or a fallback selection.

| Row term within 1% | Actual interval within 1% | Intervals |
|---|---|---:|
| Yes | Yes | 1558 |
| Yes | No | 490 |
| No | Yes | 787 |
| No | No | 3087 |

Saved refinement therefore crosses the tolerance in both directions: 490
arithmetic losses and 787 gains. Simply removing refinement is not supported
by these counts as a repair. **3087 of 3577 actual period failures already have
a row term outside tolerance.** This points to a timing-path problem extending
beyond the final localization step.

For **2810/5922 (47.4502%)** local targets, no allowed integer interval d10..75
on the 960-frame (20-ms) lattice is within 1%, even as a local arithmetic choice.
That calculation does not establish a realizable path or a candidate-success
upper bound: refinement can move events off the lattice, and transition/path
constraints remain. It establishes that sub-hop timing is essential to the
registered interval contract for a substantial part of these observations.

The registered DP penalizes changes of integer row intervals before refinement.
The subsequent independent peak-localization offsets are not part of that
penalty. Saved adjacent refinement differences exceed 1% of the local target
on 1672 intervals. Their absolute p50/p90/p99 are 122/392/634 frames, or
2.542/8.167/13.208 ms. Integer row intervals change by zero on 3675 transitions,
one row on 2222 and two rows on six. This is consistent with the observed
quantization/refinement timing mechanism; it does not identify a single exclusive
cause for every interval failure.

Absolute actual-period errors have median **1.3275%**, p90 **49.8361%** and p99
**101.3957%**. The fixed bins are 2345 within 1%, 1475 in (1,2]%, 1061 in
(2,5]% and 1041 above 5%. The large tail also needs explanation before a new
candidate: this audit has not classified it as half/double-tempo errors or
proved that a smoothing-only change would resolve it.

## Reference-target consistency constrains the next design

The unchanged local-period helper uses the median of the five nearest annotated
gaps. Among **5958 adjacent in-source reference pairs**, **1923 (32.2759%)**
have their own outgoing gap more than 1% away from that local median.

This is a reference/metric self-consistency diagnostic, not a scored ideal
candidate and not a reason to amend the old verdict. Literal annotation-event
tracking alone cannot be assumed to satisfy this locally regularized-period
criterion. The next output contract must explicitly separate event localization
from the stable DJ-grid period it intends to estimate. Annotation noise, actual
tempo variation and tempo-family mistakes have not been individually attributed
by this aggregate audit.

## Decision for the next checkpoint

Keep `dense-joint-renewal-v1` rejected and retain its baseline and artifacts.
Do not port it, disable refinement as a rescue, relax the 1% veto, revive the
failed piecewise fit, borrow OLS meter/tempo, or select a label-favorable fallback.

The next deliverable should be a **plan and registration decision for one
continuous-time beat trajectory**. This is a proposed hypothesis, not an
implemented or qualified candidate:

1. First characterize the large-error tail and specify final event/grid semantics
   and annotation/local-period consistency checks, while retaining the
   already-failed DD-05 verdict and gates. Do not assume localization noise
   explains the observed near-50% and near-100% tails.
2. Define sub-hop phase/period state and regularity cost over the final emitted
   intervals, including source/window continuity, uncertainty and abstention.
   Fix state bounds, numeric ties and resource limits before implementation.
3. Require generated tests for non-lattice constant periods, changing tempo,
   alternating localization offsets, missed/doubled beats, bar phase and unknown
   successors. Direct actual-interval correctness must be tested alongside OLS.
4. Register a new evaluation boundary with explicit prior exposure to this GTZAN
   evidence and a justified independent development/transfer source. No new
   corpus, model, training, real candidate run or substantial Rust port starts
   in this checkpoint. Product RAM/latency and P4 feasibility remain UNKNOWN.

## Execution and verification

Audit implementation/scope was committed, pushed and remote-SHA verified before
execution at **`dc18bd147885e28c1c8195b86d1381bd578fdea1`**. The original DD-05
measurement remains `8e27cf1495cc5d496a3c251b0e5c825c56dc81d6`; the public
DD-05 aggregate remains SHA-256
`40b3ac60c3e19d7bdb747be78c5f345d7ec96f2c7c8938c2f55dc6925ce37b59`.

The tool binds that exact public result, all three original completion/parent
identities, 100 candidate hashes, 100 private score hashes, ordered reference
manifest and the original canonical scorer source hashes. It reproduces every
saved actual score and reconciles all totals/break/support counters, then
rechecks input/runtime/source identity before writing its report. Original
measurement source files are historical; this audit uses its own clean revision.

All **40 focused Python tests pass**: 12 new diagnosis checks, 10 unchanged
event-representation controls and 18 unchanged piecewise controls, zero skipped.
The new integration fixture audits 100 generated sources, verifies private-detail
separation and rejects overwrite/changed-score input. No Rust/C/P4 matrix is
claimed for this read-only Python addition.

External driver: `D:\AI\LIBAPTA\outputs\dense-decoder-diagnosis-20261023\run.py`.
It invokes `tools/apta_1_1_dense_decoder_diagnosis.py` with the original candidate,
evaluation and prepared directories, writing new `diagnosis-attempt-1` and
`diagnosis-attempt-1-supervision` siblings outside Git. Linux parent supervision
PASS: **47.230 s**, maximum observed process RSS **27948 KiB**, under the scoped
1800-second / 262144-KiB / 64-MiB audit caps. Source IDs, window details, bindings
and stdout/stderr/resource reports remain outside Git.

Audit aggregate SHA-256:
`901faeca3290ae9ef7a688960542119d126180eb89caf6d798a47830382816f8`.
Parent completion SHA-256:
`71907d7632e715a7fb60bbedce583ad72623b9a5469ba32dd0b9f08ae01c1719`.
Driver SHA-256:
`ce37faca05f2562eb05a291d0da9913837ec8e256fcfb22060ae467f0e0d11c4`.
Focused-test log SHA-256:
`9e525481cca2df38a80fb69e12f1355cbe776eb493fbb1db173c341441e4b111`.
