# Dense decoder DD-02: pure implementation and generated checks

Date: 2026-10-23. Registration was committed/pushed at
`ade78898891767064671f7cf0cf4fe2eb5c8d4be` before implementation.
The [experiment](DENSE-DECODER-EXPERIMENT.md) and
[protocol](DENSE-DECODER-PROTOCOL.json) remain unchanged.
Protocol SHA-256:
`5dc1241087a08663b06766f91faade5a5758f9791fd85e0077c62e78d5a186fd`.
[Machine-readable checks](DENSE-DECODER-DD02-20261023.json).

## Decision and implementation

**DD-02 implementation and generated checks PASS. Proceed to DD-03.**
This is not the full DD-04 preflight and no real dense trace is decoded/scored.
The registered development gates are not evaluated. The best recorded musical
result remains unchanged; no new accuracy claim follows.

The pure [constructor](../../tools/apta_dense_decoder.py) receives dense beat/
downbeat arrays and source/window/gap geometry only. It performs no file,
environment, network, subprocess, neural-runtime, random or evaluator access.
Finite exact f32 validation precedes registered clipping and rational tie-away
integer quantization. Source/resampled row-count bounds and complete plain S6
partitions are checked. Known PCM gaps abstain for the whole source.

The fixed recurrence uses separate 3/4 and 4/4 source-level paths, intervals
10..75 rows, at most two-row change between intervals, registered transition
cost, exact integer scores and deterministic predecessor/terminal/meter ties.
Typed signed-64-bit score and byte parent arrays bound storage; one meter's
tables are released before the next meter's solve. Only the small per-meter
paths/diagnostics are retained. Every traceback is arithmetically replayed with
seed/transition/phase/terminal checks.

The winner alone passes the score>0 and minimum-nine-events rules. There is no
restart, beam pruning, alternate-path rescue, parameter sweep or margin gate.
Concave beat-channel peaks receive exact rational sub-hop localization after
path selection, rounded once to source frames. Endpoint/beyond-source centers
remain excluded from emissions and refinement neighbors. Final frames own
original S6 membership once; moving across a window boundary retains the source
ordinal and same-event downbeat identity.

Outputs include explicit abstention/window statuses, two per-meter diagnostics,
clipping/boundary counters, state/transition counts and window-ownership changes.
A per-meter score difference is not a confidence estimate or global top-two
search. Confidence remains zero/uncalibrated, with no native graph or promotion.

## Generated verification

The [new test suite](../../tests/unit/apta_1_1_dense_decoder.py) passes **33 tests**:
- exact quantization half-ties, signed zero, f32 subnormals/extremes, clipping,
  rejection of NaN/Inf, non-f32 numbers and invalid geometry/types;
- a separate exhaustive path enumerator for lengths 1, 10, 11, 12, 20, 21 and
  24 as applicable, both meters, signed/random and all-equal scores. It shares
  no recurrence, tie or traceback helper with production;
- a hand-calculated three-beat interval-change cost and a stronger local decoy
  that loses to the higher-score complete path;
- exact 3/4 and 4/4 pulse rows/frames/phases, variable-tempo source path,
  meter-score equality, null/insufficient support and the exact nine-event boundary;
- known-gap whole-source abstention, no runner-up rescue, terminal/beyond-source
  exclusion, final in-domain peak without a boundary neighbor;
- asymmetric localization, window crossing with source ordinal preserved,
  path/corrupted-coordinate rejection, no constructor I/O/input mutation;
- full-size typed-state/transition bounds and registered protocol/constant identity.

The previous **148 Python controls** also pass: dense capture, learned piecewise,
quantized guided, free phase, segmented reference, timing audit, multibeat,
event representation, neural reference and annotated-development suites.
The new suite is wired into [the combined runner](../../rust/check.py).
That integration does not mean the full combined runner was executed here.

Unchanged Rust controls pass **37 tests per debug/release profile**
(10 builder, 7 grid, 9 native validation, 11 segmented probe). Rustfmt,
focused Clippy with warnings-as-errors and no-default library check pass.
No Rust algorithm/core code, C code, evaluator or legacy output is changed.
Full C/sanitizer/platform/hosted-CI/consumer/P4 matrices are not rerun or claimed.

## Full-size host operation, not whole-system qualification

Generated inputs have 1501 captured rows and a 1440000-frame source. The final
center is the exclusive endpoint; DP uses 1500 in-domain rows, hence 693000
states across the two meters and 3305610 predecessor examinations per source.

| Generated fixture | Time | Cumulative process peak RSS | Status |
|---|---:|---:|---|
| 4/4 pulses | 1.120 s | 22740 KiB | 60 events, meter 4 |
| Flat positive/ambiguous logits | 1.118 s | 25000 KiB | 150 events, deterministic meter-3 tie |
| Flat zero logits | 1.023 s | 25000 KiB | nonpositive_path, no events |

Maximum simultaneous score+parent array payload: **3564000 bytes**, below
the registered 3566376-byte bound. Each generated call passes the 60-s and
262144-KiB host limits. RSS is Linux ru_maxrss for this Python process, cumulative
across these three calls; it includes interpreter/output overhead, not a model
or a P4 placement measurement. Run-time/resource parent-watchdog failure tests
belong to DD-03/04 and have not yet been implemented.

Flat positive evidence produces a deterministic candidate under the registered
objective. This fixture checks tie/numerical behavior, not musical support or
confidence calibration. No ambiguity threshold is introduced after this result.

## Reproduction, identities and next step

External verification script/log, generated resource script/report:
`D:/AI/LIBAPTA/outputs/dense-decoder-dd02-20261023/`.

```powershell
wsl -d Ubuntu -u daniel --exec bash /mnt/d/AI/LIBAPTA/outputs/dense-decoder-dd02-20261023/verify.sh
```

The script runs all 181 Python checks, the three generated resource cases and
the focused Rust controls. Cargo target output remains external and reuses the
existing pinned toolchain/build root. Source/test/log/report hashes are recorded
in the public JSON. These generated implementation checks were performed before
the checkpoint commit; the exact tested module/test bytes, rather than an
uncommitted full-repository revision claim, bind their result. Real measurement
still requires a clean committed/pushed implementation and full DD-04 preflight.

**Next DD-03:** separate construct/evaluate processes, complete input/source
binding and candidate freeze before label loading, unchanged three scoring
paths, explicit resource/watchdog/partial-run handling.
**Then DD-04:** independent scalar full verifier, runner/isolation/corruption/
watchdog preflight. Do not open the real-logit development run before both pass.
Independent transfer, product budgets, whole-track/gap support, supported native
representation, consumer integration, confidence and physical P4 remain open.
