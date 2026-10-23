# Free-phase reference timing reachability — measured result

Date: 2026-10-22. Exact clean implementation:
`5ca4420ae2f46194904395b9e33164c80d63921b`, committed and pushed before measuring.
The [experiment](FREE-PHASE-REACHABILITY.md),
[fixed protocol](FREE-PHASE-REACHABILITY-PROTOCOL.json) and
[preflight](FREE-PHASE-PREFLIGHT.md) remain the authority for this run.
The [public aggregate](FREE-PHASE-RESULT-20261022.json) retains all outcomes,
diagnostics, resources and evidence hashes without source IDs or coordinates.

## Decision

**75/100 complete reference paths are feasible in the registered continuous
relaxation.** This includes all 47 prior native witnesses and **28 additional
sources** which had no complete path in the fixed reference-anchored family.
The remaining **25/100 are infeasible even in this necessary relaxation**, under
the same guarded timing, reference-envelope, ownership, original-window,
nominal-tempo and natural-count geometry bounds.

| Previous anchored-family result | Continuous feasible | Continuous infeasible |
|---|---:|---:|
| Native witness | 47 | 0 |
| No anchored-family path | 28 | 25 |
| Total | 75 | 25 |

All 100 interval-solver verdicts agree with the independent exact
difference-constraint checker. Every old anchored plan reproduces exactly and
all 47 old native paths satisfy the relaxation's original inequalities.

**Next: separately register bounded free-anchor, quantized native segmented
construction under the existing eight-segment wire cap.** Keep all 100 sources
and this necessary reachability result visible. Determine whether any of the
28 additional continuous paths can actually be represented; do not treat
continuous feasibility as an already solved native grid. Retain the previous
47 witnesses as no-regression controls and require actual native/wire/meter
validation in that future construction study.

Neither a new detector nor a native free-anchor constructor is implemented by
this checkpoint. Increasing the segment cap cannot repair the 25 contradictions
within this registered relaxation. Their status is not a proof against every
possible wire representation, applicability/ownership policy, tolerance contract
or musical annotation. It also does not imply every individual window on those
sources is infeasible; the test requires a complete source-envelope path.

## Exact scope and infeasibility evidence

The diagnostic retains one continuous rational position per original reference
ordinal, with the unchanged Q32-rounded ownership/phase bounds from the previous
study. Its independent nominal period per event retains the native tempo range
and necessary natural-count geometry. Actual outgoing intervals, including
junctions, obey the same local-period +/-1% minus one source frame. Final source
successors remain unknown. No event is inserted, removed or moved to a different
original S6 window; the reference envelope remains first beat through last beat
plus one frame, not assumed whole-audio coverage.

Fixed reference anchors, equality of nominal and actual periods inside a piece,
Q32/integer-tempo quantization and the eight-piece construction limit are relaxed.
Therefore a continuous witness need not be serializable. No MTRD is newly
constructed and no native, builder, FINAL-export or product-owner validation is
claimed for these new continuous paths.

Among the 25 infeasible sources:

- two contain at least one empty individual event domain after the fixed
  nominal/ownership/natural-count intersection;
- 23 have individually nonempty domains but incompatible accumulated actual
  interval constraints across the complete path.

All 25 retain a private first-empty forward-prefix record and an independently
certified negative cycle of the raw difference inequalities. Strict zero-length
cycles remain contradictions; no floating epsilon is used. A third reverse-chain
projection independently reproduces all 100 feasible/infeasible verdicts.
No failed source is rescued by changing bounds or successfully scoring a prefix.

## Window diagnostics and displacement

The original 100 development sources, 600 windows and **401 complete / 199
incomplete** reference contexts are retained. Saved coarse/learned OLS tuples,
all old per-window scores and constant-reference records reproduce exactly.
The earlier model remains 187/401 joint windows and constant reference OLS
227/401, under their original independent per-window support policy.

The continuous label-derived paths pass joint actual-period/phase and strict
one-to-one sequence diagnostics in **300/401** complete contexts. The source-wide
support rule is unchanged from this study: all other sources emit no full path.
This is timing feasibility, not a comparison of detector accuracy or an accepted
algorithm. Continuous witnesses are derived from reference labels and have a
different representation/support policy from the old constant OLS controls.

Of the 106 previously constant-period-incompatible windows, 59 now have a full
source-envelope continuous witness, versus four with the prior anchored native
witness. The other 47 remain unsupported by this source-wide relaxation. This
does not establish that those individual windows would each be infeasible alone.

The 75 paths retain **4545 original ordinals**. Exactly **1568 positions move**;
2977 remain at their original reference timestamps. Absolute displacement has
median **0 frames** and maximum **4645.40 frames (96.78 ms)**. Maximum normalized
phase error is **0.0999785 beat**, within the fixed 0.10-beat tolerance and its
one-frame guard. This does not establish a separate 70-ms event-F1 criterion.
All 4545 points lie within known reference coordinate context; 4470 have a
known actual successor and 75 final-source successors remain unknown. Unknown
successors and incomplete contexts are retained, never inferred correct.

No audio, inference, new labels or corpus, formal holdout or reserved transfer
is opened. The development cohort remains already open with unresolved
cross-dataset identity. Confidence calibration, learned timing/family selection,
meter accuracy, independent transfer, fresh acceptance and physical P4 gates
remain open.

## Verification and reproducibility

All **100 Python tests** and **33 focused Rust control tests in each debug and
release profile** pass, with Rustfmt, focused warnings-as-errors Clippy and
no-default-features checks. The new suite includes exhaustive small integer
chains, strict rational chains, moving-phase positive and incompatible-reference
negative controls, cycle certificates and old native witness inclusion. The Rust
tests exercise unchanged native controls, not serialization of continuous paths.
No core DSP, scorer, native validator/limit, consumer pin/provider or release
default changes. Full Rust/C/sanitizer, hosted CI and physical P4 matrices are
not rerun or claimed for this host-only diagnostic.

The run binds **1443 files**, checks all 100 old anchored plans, all old scores,
the 47 old native path embeddings and every new witness/negative certificate.
The separate replay checks raw inequalities and all saved negative cycles,
reconstructs forward empty prefixes, solves the reverse interval chain, and
reproduces all 600 old/new scores, cross-tabs and displacement. Every bound
input and private detail hash is rechecked before export.

Measured processing, prior-plan replay, scoring and final recheck take
**28.87 s**, peak RSS **27636 KiB**, within the 180 s / 256 MiB host caps. Initial
source/input binding, compilation and the separate verification/export replay
are excluded from that timing. This is not a P4 memory or real-time measurement.

From a clean checkout at the exact implementation SHA in WSL, retaining the
private inputs and choosing a new output directory (overwrite is refused):

```sh
python3 tools/apta_1_1_free_phase_reachability.py \
  --attempt "$PRIVATE_ROOT/neural-reference-20261022/comparison-source-domain" \
  --prepared "$PRIVATE_ROOT/neural-reference-20261022/prepared-availability" \
  --prior-audit "$PRIVATE_ROOT/timing-failure-audit-20261022/attempt" \
  --prior-segmented "$PRIVATE_ROOT/segmented-reference-20261022/attempt" \
  --output "$NEW_PRIVATE_OUTPUT"
```

Private report, rational paths, prefix and negative-cycle certificates:
`/home/daniel/.local/share/libapta-audio/rust-rewrite/free-phase-reachability-20261022/attempt/`.
Exact source archive, verification log and independent reverse replay/export:
`D:/AI/LIBAPTA/outputs/rust-free-phase-reachability-20261022/`.

- Protocol SHA-256:
  `54a5cac6ee6ce86c82589fd34728fc54d9a9a5bf0b66ba5c67d5f473a9b71f41`.
- Private report SHA-256:
  `af62b36ba4bcd3617dfb2ee61482d4968ab549f22d886c80277c0a26fb8a276a`.
- Exact source archive SHA-256:
  `9c11f61dfae803a3e89d0cc88b1b53c80125b76b32c1765cefb2d51f59cc2093`.
- Independent replay/export script SHA-256:
  `a3496cbfd5033b3ebc1b557f44f8fea6c10fdc5ec4aa6f787169f85e6697741e`.

Commit/push this result before the separately registered quantized construction
study. The prior anchored-family result and all earlier failed candidates stay
fixed; this checkpoint makes no production, release or physical qualification.

Measured follow-up: the [terminal guided quantized
construction](QUANTIZED-GUIDED-RESULT-20261022.md) realizes 16 of the 28 additional
continuous paths natively; 13 fit the wire cap. Totals are 63 native / 60 wire
sources, with all 47 previous responses preserved. The twelve guided-family
failures are not global impossibility proofs. Reference-construction research
is now closed. The subsequent
[label-blind learned-piecewise experiment](LEARNED-PIECEWISE-RESULT-20261023.md)
is rejected. Next work is upstream dense logit capture and event-output parity.
