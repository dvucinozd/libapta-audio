# Temporal model — ordered development gate

Date: 2026-10-22. Stage C of the
[registered experiment](S6-TEMPORAL-MODEL-EXPERIMENT.md) is **rejected on ASAP**.
The model improves confirmed period and joint period/phase counts but breaks
two previously phase-correct windows. The registered zero-phase-break veto
therefore stops the attempt before Ballroom. No parameter, support rule,
context, score, confidence, reference or gate is changed after this result.

The preceding 44-file native/history/research checkpoint was committed and
pushed as `b32345f77660c3f4864b188e941bc9d31886aa68`; local and remote SHA matched
and the checkout was clean before this continuation. This stage adds only the
ordered host evaluation tool, its tests and test-driver/docs integration on
that base. The archived dirty source inventory identifies those additions.
Native processing, the stage B model and existing scorers remain unchanged.

## Frozen inputs and execution

`tools/apta_1_1_native_temporal_development.py` binds the registered protocol,
exact stage B verification report, model/test/scorer files, all forty prepared
development inputs, reference hashes, stage A reports and every retained
native output before the first candidate result. The source snapshot contains
882 files; the ASAP freeze binds 1092 files. Complete source and evidence
hashes are checked again after evaluation.

Only the existing forty ASAP development performances are used, twenty in
3/4 and twenty in 4/4, with their existing thirty-second rendered-piano PCM
and native dense traces. No source conversion, new selection, native rerun,
spent DJ run, formal holdout access or fresh acceptance input is introduced.
The canonical development loader validates the pinned declaration before
labels/audio access. No private audio, labels, source maps or raw traces enter
Git.

The runner passes only native timing headers, source geometry and dense
observations to the unchanged causal adapter. Old proposals, meter, labels,
private filenames and corpus identity are excluded from that input. One
`SourceBudget` spans all six windows of each source. Candidate selection runs
before that window's reference scoring. No old-grid fallback or old-meter
borrowing is permitted.

Every source contributes all six windows: **240 total, 160 with complete
reference context and 80 with incomplete boundary context**. Both outputs use
the unchanged annotated-development `score_window`, 1% local beat-period and
0.10-beat phase tolerances. Every coarse per-window/aggregate score reproduces
the retained baseline. Every saved candidate/coarse window is independently
rescored after the run. Incomplete-context emissions remain unknown for
whole-window correctness; they are not discarded or counted as correct.

## Result and rejection

| Window metric | Coarse | Temporal | Fixes | Breaks |
|---|---:|---:|---:|---:|
| Emitted grid, all 240 | 238 | 240 | — | — |
| Confirmed period, complete reference | 1 | 6 | 5 | 0 |
| Confirmed phase, complete reference | 5 | 29 | 26 | **2** |
| Confirmed joint period/phase, complete reference | 1 | 4 | 3 | 0 |

Joint correctness is 1/160 versus 4/160 on complete-reference windows,
or 1/240 versus 4/240 unconditionally. These are provisional source-window
development diagnostics, not FINAL track-level acceptance percentages.
All 240 output tuples change; correctness changes in 30 windows. Candidate
abstentions are zero. Coarse abstains twice, once with complete reference.
The candidate emits 156 joint-wrong grids on complete-reference windows and
80 grids whose whole-window correctness is unknown. Coarse has 158 wrong,
79 unknown and one complete-reference abstention.

Per-meter strata each contain 120 windows, 80 reference-complete:

| Meter | Period coarse → temporal | Phase coarse → temporal | Joint coarse → temporal | Phase fixes / breaks |
|---|---:|---:|---:|---:|
| 3/4 | 0 → 0 | 3 → 13 | 0 → 0 | 11 / 1 |
| 4/4 | 1 → 6 | 2 → 16 | 1 → 4 | 15 / 1 |

Per-beat counts also remain explicit: coarse scores 2290/2373 represented
beats, with 149 period-correct and 632 phase-correct; temporal scores
1260/1320, with 141 period-correct and 572 phase-correct. The different
predicted-beat counts prevent treating raw-count changes as like-for-like
coverage. No new accuracy claim is inferred from successful grid emission.

The strict period/joint improvement gates pass. Zero period/joint breaks,
confidence safety, evidence integrity and host resource gates also pass.
**Zero phase breaks fails (2 > 0)**, including one failure in each meter
stratum. Those phase-only correct baseline cases remain protected even when
their periods are wrong. An aggregate gain cannot override that veto.
Ballroom candidate evaluation is consequently **not run**. Its previously
captured stage A traces are retained; they are not transfer evidence for this
rejected model.

Confidence stays fixed at 49, as registered. Both systems have zero
high-confidence grid errors/unknown-context outputs in this ASAP comparison;
the candidate's confidence is not calibrated by this test. It emits no meter
or downbeat. The baseline's 106/183 correct meter records and 21/169 scored
downbeats remain baseline evidence only, with no transfer to the new grid.

## Verification and resource cost

Ten new evaluation tests pass, covering strict gates, phase-only protection,
abstention breaks, confidence safety, hash drift, development-header refusal,
ASAP-before-Ballroom ordering, no overwrite, operational veto, label/old-grid
exclusion, all 240 windows and forty distinct shared source budgets. The test
driver now includes this suite. The thirteen unchanged model tests and 23
existing history/dense/event/scorer tests also pass: **46 tests, no skips,
warnings treated as errors**.

Runner measurements are 489.89 seconds elapsed / 479.15 seconds process CPU,
maximum 19.154 seconds per source, 10121352 bytes peak numeric workspace and
42648 KiB RSS observed by the model. The external `/usr/bin/time` record
separately reports 8:00.23 wall, 478.33 user / 1.60 system seconds and 42828
KiB maximum RSS. Both clock observations are retained rather than normalized
after the run. All source/resource measurements are below the registered
120-second/source, 16 MiB numeric and 256 MiB RSS caps.

This cost is additional host selection over retained captures, not a native
real-time/P4 benchmark. The future native 4 MiB storage / 64 KiB scratch gates
are unmeasured. The unchanged native checkpoint's 289/328 debug/release and
66 focused sanitizer tests remain historical evidence; the full Rust/C/product
or physical P4 matrix is not rerun for this Python-only continuation.

## Evidence and reproduction

Private source snapshot, freeze, per-source results and ordered report:
`/home/daniel/.local/share/libapta-audio/rust-rewrite/temporal-development-20261022/`.
Shareable aggregate copies, verification/run logs, timing and orchestration:
`D:/AI/LIBAPTA/outputs/rust-s6-temporal-development-20261022/`.

| Artifact | SHA-256 |
|---|---|
| Candidate source inventory | `32b962a51888837e59be42d0d7cae03cf3723f616e2655b8be5aa2a395113ee0` |
| Unchanged protocol | `bf8dc41fcce167b3ed9fb23d08d8106121e1c0e74c2d012d253025d893c9bbf2` |
| Unchanged model | `cf7afb76976b8d63da9c538a9aa184bf80c52836485b0a4c99dd990ad61e7227` |
| ASAP freeze | `5ec4def8d4d70cfa998352399c764171213ec05f5f105ee4ad6112f12760d17c` |
| ASAP result | `059fb351b9036722ea3b2e349a47553bbba5eb39a638145c8e69861b4d4055e7` |
| Aggregate checkpoint report | `e7a729685d1e5038d1f69e68e386a03abc2d3c55eabb82acc1e2352feec73f3e` |

Run synthetic verification without accessing a corpus:

```sh
PYTHONWARNINGS=error python3 tests/unit/apta_1_1_native_temporal_development.py -v
```

The evaluation CLI requires the exact retained `--evidence-root`,
`--stage-b-report`, full `--source-revision`, `--source-inventory` and a new
`--output` path. It has no corpus-order or DSP-tuning options and refuses an
existing output directory. The completed command is retained in `run.sh` and
`corpus-time.txt`; it documents this attempt, not authorization to retry it.

## Next bounded step

Close this frozen temporal candidate as rejected; preserve its implementation,
synthetic alias counterexample and two phase regressions. Do not implement
`experimental-s6-temporal-grid`, run Ballroom as a rescue or change this model
after inspecting ASAP.

Before registering another selector, separately pre-register a development-only
audit of **output representation versus timing evidence**: quantify local
reference-period variation over the existing S6 windows, the retained tempo
aliases and the two phase-only regressions. This should distinguish constraints
of one constant grid per window from failures to recover a beat path. It must
not fit a new label-assisted candidate, lower scoring tolerances, relabel
references or become fresh acceptance evidence. A changed representation or
new evidence/model requires a new frozen experiment and independent transfer
gate before native implementation.

The overall PCM → same-grid meter → immutable publication → actual Pajoniiir
validator slice remains open. Same-grid meter/downbeat, native/oracle agreement,
consumer adoption, physical P4 measurements and FINAL qualification are later
gates. Production provider, version, consumer pin and eight-segment wire limit
remain unchanged. This continuation is committed and pushed as a separate
rejected-development checkpoint before the next step; its exact Git revision
is carried by branch history. Historical dirty evidence inventories remain
intact. A checkpoint push is not a release or promotion.
