# Label-blind learned-event piecewise timing — measured rejection

Date: 2026-10-23. Exact clean implementation:
`7c6faac44a46a33e0146d0eae57993e6258f802e`, committed and pushed before the run.
The [experiment](LEARNED-PIECEWISE-EXPERIMENT.md),
[fixed protocol](LEARNED-PIECEWISE-PROTOCOL.json) and
[preflight](LEARNED-PIECEWISE-PREFLIGHT.md) remain unchanged. The
[public aggregate](LEARNED-PIECEWISE-RESULT-20261023.json) records metrics,
gates, redacted original regressions, support, resources and hashes.

## Decision

**Reject `learned-window-piecewise-ols-v1`; stop before independent transfer or
native implementation.** Nominal-period joint windows fall from 187/401 OLS to
174/401, with **zero fixes and 13 breaks**. Complete nominal sequences fall
178 -> 168/401. Actual-successor complete sequences also fall, 97 -> 95/401
under the newly matched actual-interval diagnostic. All nine registered gates
fail. Both original coarse-to-OLS regressions still fail joint correctness.

No residual, minimum-piece length, seam, context, capacity, anchor, alternate
family or fallback rescue follows. This exact candidate and its outputs remain
a frozen failed development experiment. Reference-construction research stays
closed; the earlier 60/100 label-derived wire witnesses are not detector gains.

**Next: separately register label-free dense beat/downbeat logit capture from
the same fixed Beat This! final0 model, before another learned-event-only fit.**
The already bound upstream `beat_this/inference.py` exposes `Audio2Frames`
(lines 260–281) and `Audio2Beats` calls it before postprocessing (lines 301–303).
This was checked from the local source, not inferred from model scores. Freeze
the exact model/source/checkpoint, preprocessing, coordinate mapping and capture
caps before any new inference. Retain every frame's beat/downbeat evidence,
including weak and competing peaks, and require unchanged legacy event outputs
through the same upstream postprocessor. Keep exclusive-endpoint events separate
and preserve original S6 geometry and input hashes.

First establish complete trace integrity and event-output parity without label
scoring; only then separately define a bounded decoder using those probabilities
to compare variable timing and competing phase/family paths. The logits are a
potential additional evidence source, not an already tested improvement. Do
not retrain/change models, use labels to select paths, reopen formal holdouts,
or reuse the failed piecewise candidate with tuned parameters. No capture or
new inference is performed by this checkpoint.

## Isolation and fixed evidence

All 100 sources, 600 original windows and 401 complete / 199 incomplete
reference contexts remain. Candidate construction receives only saved learned
events/downbeat indices, plain window geometry, source length and explicit
PCM availability. Its pure module imports only `fractions`, performs no file
access and has no reference/scoring imports. Every candidate is written and
hash-frozen before the evaluator parses labels or old scoring records. Hashing
the original label file for binding is not parsing it into construction.

The separate verification independently reconstructs all candidates while
reference labels are in evaluator memory and requires exact frozen equality.
No reference-derived feasible path or old-correct verdict selects a candidate
piece. All old OLS/coarse tuples, scores and the two original regressions
reproduce. No new audio, labels, model inference, corpus or transfer is opened.
The cohort remains already-open development data with unresolved cross-dataset
identity; no fresh musical acceptance is claimed.

## Grid, actual intervals and meter

| Complete-context diagnostic, denominator 401 | Original OLS | Piecewise candidate |
|---|---:|---:|
| Own nominal period correct | 192 | 179 |
| All represented phases correct | 300 | 274 |
| Own nominal period + phase | 187 | 174 |
| Complete nominal sequence | 178 | 168 |
| Actual outgoing intervals + phase | 101 | 98 |
| Complete actual-successor sequence | 97 | 95 |

The actual-successor comparison includes cross-piece/window jumps only when
consecutive original ordinals and positive positions exist. Missing or final
successors remain unknown, never substituted with a nominal period. The old
187/401 is unchanged and must not be compared to candidate 98 as if they used
the same period definition. The frozen constant-grid scorer is not modified
or claimed to score variable grids: both candidate diagnostics use the existing
common point scorer. Meter/downbeat scoring invokes only the frozen scorer's
meter fields and discards the diagnostic proxy's grid fields.

| Paired old-correct -> wrong or missing transition | OLS breaks |
|---|---:|
| Nominal period | 13 |
| Phase | 27 |
| Nominal joint | 13 |
| Meter | 19 |
| Downbeat | 16 |

There is one phase fix, zero nominal-period fixes and zero joint fixes.
Against coarse S6 there are 131 joint fixes and four breaks, so coarse
no-regression also fails. These are overlapping changes, not independent
error categories. Candidate meter emits 191 records, 177 with correct meter;
downbeat is correct in 151/185 reference-scorable emitted records. Original OLS
emits 215 meter records, 195 correct, with 167/207 correct/scorable downbeats.
Conditional emitted counts do not establish whole-corpus acceptance. Zero
placeholder confidence supplies no calibration or high-confidence safety PASS.

Original regression A still has wrong joint timing, with zero period-correct
points and mismatched phase. Regression B retains one-to-one phase but fails
period precision. Both redacted cases remain in the aggregate. No family
correction or change of the old scorer follows.

## Support and failure attribution within this fixed attempt

| Candidate window outcome | Windows |
|---|---:|
| Accepted | 467 |
| Insufficient learned events | 17 |
| No compatible complete piecewise path | 92 |
| Source capacity refusal | 16 |
| Source seam failure | 8 |
| Total | 600 |

Accepted windows use 524 pieces across the 100 sources. Fifty-five sources
have all windows supported. Three sources exceed eight pieces (nine each),
causing source-wide abstention in their 16 otherwise supported windows. Eight
windows fail simultaneous two-sided source seam checks. Full rejected plans
remain private; no successful partial prefix is substituted. Analysis abstention
is not a declared PCM gap. Actual cohort has no declared PCM gaps.

Of the 13 joint regressions, four are no-path abstentions, four are source-cap
refusals and **five remain accepted but wrong**. Therefore simply removing
guards or raising capacity would not address all observed regressions. This
does not prove every piecewise estimator impossible; it rejects this learned-
coordinate OLS hypothesis without additional upstream evidence. With fixed
event identity it cannot invent missing beats, delete extra events, recover
discarded weak network peaks or resolve musical family ambiguity by itself.

## Verification and reproduction

All **130 Python tests** and **37 unchanged native Rust controls per debug and
release profile** pass, with format, focused Werror Clippy and no-default-feature
checks. No Rust/core/consumer/scorer/format/limit/default or release changes.
No candidate native serialization, FINAL/product, physical P4, full Rust/C/
sanitizer or hosted CI matrix is claimed for this host-only addition.

The measurement binds 1257 files. An independent replay computes centered-
rational OLS instead of the constructor's raw sums, then suffix-optimal paths
instead of prefix dynamic programming. It reconstructs every candidate/window,
source seam/capacity veto and meter coordinate, reproduces all 600 old/new
scores and paired changes, checks both original regressions and all gates,
and rechecks every source/input/candidate/scoring hash. The candidate freeze
remains bound with `reference_labels_loaded: false` for all 100 outputs.

Measured constructor/scoring/replay and final checks take **23.00 s**, peak
runner RSS **26352 KiB**, below the registered 180 s / 256 MiB caps. Initial
source/input binding, tests and separate independent export are excluded.
These are host measurements; no embedded memory/timing claim follows.

From exact clean source in WSL, retaining private inputs and choosing a new
output directory (overwrite is refused):

```sh
python3 tools/apta_1_1_learned_piecewise.py \
  --attempt "$PRIVATE_ROOT/neural-reference-20261022/comparison-source-domain" \
  --prepared "$PRIVATE_ROOT/neural-reference-20261022/prepared-availability" \
  --output "$NEW_PRIVATE_OUTPUT"
```

Private freeze/candidates/scoring/report:
`/home/daniel/.local/share/libapta-audio/rust-rewrite/learned-piecewise-20261023/attempt/`.
Exact source archive, tests and independent fit/partition/score replay/export:
`D:/AI/LIBAPTA/outputs/rust-learned-piecewise-20261023/`.

- Protocol SHA-256:
  `007b1c0cb834c5768947e2df9f1431e6a8483a9ea1a51eace42d0f76c7f4e7fc`.
- Pure constructor SHA-256:
  `dec643e9b948c11c7f9ce23cc4a2df6b3fdfcbfa7142860c1bce1a8473a41acb`.
- Candidate freeze SHA-256:
  `8508d4c38c67be0af4268aea2c00e96032bd9dbb2df246119da1e06d6d06b791`.
- Private report SHA-256:
  `0d2b4bee22549fb3e3f7a3f604196bc0da8ea75a78d0020f5bdf31db64dbe03f`.
- Exact source archive SHA-256:
  `6b4cd2c0bb982f9dede21c20b2b1e6ad2a700a34c0971f8c73b2cad66dc72edf`.
- Independent replay/export SHA-256:
  `468409977e3de43394465bd3c9080315a42bd258985342e0e37fbae1e4b85b10`.

Commit/push this measured rejection before any dense-logit registration.
Independent transfer, confidence, key, fresh acceptance, production adoption
and physical P4 qualification remain open. No new musical accuracy gain.

Measured follow-up: the separately registered
[complete dense capture](NEURAL-DENSE-RESULT-20261023.md) retains 150095 f32 rows
and exactly reproduces all 100 saved event sequences. No labels or musical
scoring are read. Dense decoder registration is next; this rejection stays fixed.
