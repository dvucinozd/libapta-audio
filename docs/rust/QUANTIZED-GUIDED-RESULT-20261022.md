# Terminal guided quantized construction — measured result

Date: 2026-10-22. Exact clean implementation:
`77c7be4ed8847859d746a4d6d02e5cb0c54d6556`, committed and pushed before the run.
The [experiment](QUANTIZED-GUIDED-EXPERIMENT.md),
[fixed protocol](QUANTIZED-GUIDED-PROTOCOL.json) and
[preflight](QUANTIZED-GUIDED-PREFLIGHT.md) bind this one attempt.
The [public aggregate](QUANTIZED-GUIDED-RESULT-20261022.json) records all counts,
diagnostics, resources and hashes without private IDs or coordinates.

## Decision and next work

**60/100 complete reference paths have a wire-qualified native witness, up from
47/100: 13 additional paths.** All 47 previous native responses reproduce
identically. The guided family also constructs complete native graphs for
three additional sources which need 11, 13 and 13 segments and explicitly fail
the existing eight-segment wire/builder cap. Total native support is 63/100.

| Full-source outcome | Sources |
|---|---:|
| Previous controls retained and wire-qualified | 47 |
| Additional wire-qualified native paths | 13 |
| Additional native paths exceeding wire cap | 3 |
| Continuous-feasible, no path in the fixed guided family | 12 |
| Infeasible under the earlier necessary relaxation | 25 |
| Total | 100 |

The 28 newly continuous-feasible sources therefore split into 13 wire-qualified,
three over-cap and twelve unsupported by this deterministic guided family.
The guided family itself recovers all 47 prior sources; the separately reported
union still deliberately retains their exact original paths as controls.

**Reference-construction research is closed for this sequence. Next: separately
register a bounded, label-blind variable-timing reconstruction of saved Beat
This! events against the original OLS control.** The construction interface must
receive learned events, fixed source windows and availability only; reference
labels belong exclusively to scoring. Preserve both original regressions and
all rejected candidates. Do not add another reference-anchor optimizer, change
the eight-segment limit or train/port another model from this result alone.
Independent development transfer follows only if that learned-event experiment
passes its separately fixed gate; holdouts and fresh acceptance stay closed.

This is reference-derived representation feasibility. The measured model remains
187/401 joint-correct windows under its original projection. No detector accuracy,
confidence, FINAL export, product-owner or physical P4 qualification is gained.

## Family limitations and actual native checks

Each saved continuous position supplies one rounded/clamped integer anchor.
Dynamic programming finds the minimum only within this frozen anchor family.
The 12 family failures do not prove quantization or every free-anchor fit
impossible. The three over-cap counts likewise do not prove other anchors
could not fit within eight. Full paths are retained before capacity checking;
there is no successful truncation or retry with different rounding/parameters.

All 63 guided graphs pass actual native validation and exact Python/Rust
`beat_at` expansion. Sixty pass strict/canonical GGRD and complete external
builder validation with same-grid meter; the other three explicitly fail
wire/builder capacity while retaining their complete native graphs. All 179
native MTRD records preserve ordinal and whole-frame binding; 166 belong to
wire-qualified graphs, including the original 135 and 31 additional records.
The builder's single synthetic overview column is a structural fixture, not
waveform analysis. All states remain provisional, pending REVN, uncalibrated
confidence zero. Core/native validators, limits and production pins/defaults
are unchanged; only the host probe has a new versioned explicit-anchor input.

The guided-family segment histogram across the 63 native paths is:
`1:25, 2:14, 3:3, 4:5, 5:4, 6:2, 7:5, 8:2, 11:1, 13:2`.
These minima refer to guided paths, not a global optimum or the selected old
control paths retained in the union.

## Existing contradiction audit

The 25 earlier continuous contradictions reproduce with original bounds:

- one source has an empty nominal-tempo interval;
- one has an empty geometry domain despite a nonempty nominal interval;
- 23 have individually nonempty domains with incompatible accumulated intervals.

The first source-order representative of each category is retained privately
through the report and bound source records. This is a mechanical audit of
existing labels and certificates. No audio/listening or changed-bound experiment
is performed, so expressive timing, annotation jitter and musical label mistakes
remain unclassified. It is not evidence to rewrite labels or frozen thresholds.

## Window diagnostics and approximation

All 100 sources, 600 original S6 windows and 401 complete / 199 incomplete
reference contexts are retained. All old tuples and scores reproduce: coarse
47/401, model OLS 187/401 and constant-reference OLS 227/401 joint windows.
Those per-window controls retain their original support policies.

| Complete-context actual-successor diagnostic | Joint / strict sequence |
|---|---:|
| Guided native graphs | 252/401 |
| Guided wire-qualified graphs | 240/401 |
| Union retaining old native paths | 252/401 |
| Wire-qualified union retaining old paths | 240/401 |

These label-derived paths require full-source support and are not detector
comparisons. Of the 106 previously constant-period-incompatible windows,
21 obtain a full wire-qualified source witness, versus four in the original
anchored-family study. Unsupported sources contribute no successful prefixes;
this does not prove their individual windows impossible in isolation.

The union native paths retain 3799 original ordinals, with 3674 moved coordinates.
Absolute displacement has median 399.58 frames (8.32 ms), maximum 3949.21 frames
(82.28 ms). The wire-qualified union retains 3538 points. Final source successors
remain unknown: its common scorer reports 50 missing successors within known
reference coordinate context and ten fractional endpoints beyond the last
annotation timestamp. The larger native union has 51 and twelve respectively.
These endpoint/context distinctions remain explicit; neither incomplete context
nor a separate 70-ms event-F1 criterion is inferred correct.

## Verification and reproduction

All 112 Python tests, 37 focused Rust tests per debug/release profile and three
generated legacy/guided Python-to-Rust fixtures pass. Rustfmt, focused Werror
Clippy, no-default-features check and release probe build pass. Full Rust/C/
sanitizer/CI, product and P4 matrices are not rerun or claimed for this host tool.

The run binds 1552 source/input/binary files, reproduces all 100 old anchored
and continuous plans, and checks all 47 legacy native responses. A separate
replay independently intersects generic affine period inequalities, finds tempo
bounds by binary search rather than the constructor's integer inversion, and
computes forward shortest-path minima/ties rather than reverse dynamic
programming for all 75 feasible sources. It re-executes every guided native
graph and legacy control, checks meter/ordinals, all 100 statuses, all 600 old/new
scores, contradictions, approximation and every input/detail hash before export.

Measured processing/scoring, prior-plan replay and final checks take 35.27 s,
peak runner RSS 28436 KiB, within 180 s / 256 MiB. Initial input/source binding,
compilation and separate verification/export are excluded. No target-device
capacity or real-time claim follows.

From clean exact source in WSL, with retained private inputs and a new output:

```sh
python3 tools/apta_1_1_quantized_guided_reference.py \
  --attempt "$PRIVATE_ROOT/neural-reference-20261022/comparison-source-domain" \
  --prepared "$PRIVATE_ROOT/neural-reference-20261022/prepared-availability" \
  --prior-audit "$PRIVATE_ROOT/timing-failure-audit-20261022/attempt" \
  --prior-segmented "$PRIVATE_ROOT/segmented-reference-20261022/attempt" \
  --prior-reachability "$PRIVATE_ROOT/free-phase-reachability-20261022/attempt" \
  --analyzer "$GUIDED_NATIVE_PROBE" --output "$NEW_PRIVATE_OUTPUT"
```

Private records/report:
`/home/daniel/.local/share/libapta-audio/rust-rewrite/quantized-guided-20261022/attempt/`.
Exact archive, log, synthetic fixtures and independent replay/export:
`D:/AI/LIBAPTA/outputs/rust-quantized-guided-20261022/`.

- Protocol SHA-256:
  `2f5287b6e36742348ba9a012694c26fe6c495ce17278d146cc525d651b29be5f`.
- Private report SHA-256:
  `192b1ea278f1579913f0d6a1a5d83a0db42d50b17f5e88fe16b25314eb6d0bd9`.
- Native probe SHA-256:
  `f1bd0cfc3c326715682f7931ad8acd9bc7214c6f89335497de95b7001c034a3b`.
- Exact source archive SHA-256:
  `290d269950155cf347980ae1676ae2c6f9edc1d310392562de888c9bce52e250`.
- Independent replay/export SHA-256:
  `a50d497ef10b87c8e71a2532f4d006a3f4c0452e496cc95872f5e806b4f0c776`.

No audio/inference, new labels/corpus, holdout or reserved transfer is opened.
The already-open cohort and unresolved cross-dataset identity remain development
limitations. Commit/push this terminal result before learned-event registration.
