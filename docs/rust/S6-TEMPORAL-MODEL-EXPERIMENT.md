# Dense temporal beat-path experiment

Registered 2026-10-22 before implementing new evidence capture, a selector or
any candidate corpus scoring. The paired machine-readable
[protocol](S6-TEMPORAL-MODEL-PROTOCOL.json) freezes identities and parameters.
Status: stage A complete; [native dense capture](S6-DENSE-TRACE-CHECKPOINT.md)
preserves all retained outputs. [Stage B host implementation](S6-TEMPORAL-HOST-CHECKPOINT.md)
now passes synthetic implementation checks and retains a boundary-tempo alias
counterexample. Stage C recorded candidate accuracy remains unmeasured.
Registered rules below are unchanged.
Baseline is `1876eb34a9169299ece0d908927850e6f6e79670` plus the exact retained
dirty Ballroom tooling inventory. No production/native-selector change is
authorized by registration alone; use the staged gates below.

## Failure and genuinely different information

The unchanged native engine confirms joint period/all-beat phase in 1/160
reference-complete ASAP development windows and 9/165 Ballroom windows. The
sparse and novelty selectors emit no grids on either set. The rejected DJ
event-support oracle recovered only 16/360 joint-correct windows and broke 85
previously correct cases. None qualifies for adoption.

Historical [WP2-I1/I2/I3](../status/APTA-1.1-WP2-CLOSURE.md) already tested
window continuity, majority votes and beat-to-beat offset paths over the
unchanged full-window top-three broadband-flux candidates. I3 improved Ballroom
but failed ASAP transfer. Do not reproduce those models with a different
weight. The Rust event oracle also discarded same-period phase alternatives.

Hypothesis: **a path through all native energy-novelty bins can retain weak
and subthreshold timing observations lost by peak extraction, represent local
inter-beat interval changes, and preserve competing phases while estimating
a new beat lattice**. This is new temporal representation of the already
implemented energy evidence, not a new filter/novelty formula, an old-flux
reranker or a tuned peak selector. It is not claimed to solve musical meter.
Change this one timing axis; retain the existing meter estimator until a
separately gated same-grid integration can test it.

## A — native evidence capture, before a selector

Add only explicit diagnostic feature `experimental-s6-temporal-trace` and
runtime `temporal-trace-diagnostics`. Capture the existing f32 novelty array
inside its current calculation pass, before the 0.20 peak threshold and 60 ms
peak neighborhood. Do not reconstruct it in Python or alter old arithmetic.
Each record contains up to 1024 values with exact source-bin coordinates,
count, completeness and the known startup-context length. Preserve the native
80 ms background definition. Startup bins lacking that background retain
their existing zero values and are explicitly declared; a lost ring bin or
later discontinuity must never masquerade as such a startup zero.

Capture completeness describes the dense array independently of sparse peak
capacity. A peak-list overflow cannot erase a completely captured dense array;
a missing dense bin invalidates this run. Keep source range and final partial
bin size explicit. The extra record budget is <=4128 host bytes, measured
including metadata/alignment. Caller storage only; no core heap allocation.
Existing non-trace layouts and production defaults remain unchanged.

Use the exact existing forty-row development preparations per corpus, first
min(source length, thirty seconds), 48 kHz stereo PCM16 and dense reference
hashes in the protocol. No new source selection or source conversion. Run
coarse/attack/novelty and the new dense diagnostic mode; every ordinary JSON
must equal the retained baseline byte for byte. Removing only the extra dense
field from the new mode must reproduce its ordinary novelty JSON exactly.
Store audio, labels, source maps and raw traces outside Git.

Before any model result, verify synthetic weak events, constant carrier,
silence, source offsets, first/last partial bins, exact capacities, capture
loss, ring wrap, chunk-size independence, source replacement/cancellation,
non-finite/invalid inputs and zero allocations. Run focused Werror/no-default
and sanitizer checks and verify all-feature/non-trace configurations. Evidence
capture itself is not musical improvement or permission to publish a grid.

## B — frozen label-blind host model

Only after A passes, implement a separate diagnostic oracle under `tools/`.
The selector takes dense native values, exact source geometry and no labels,
corpus name, meter, retained coarse proposal or source filename. Existing
scoring/identity helpers remain canonical. The rejected event selector stays
frozen and is not run on either external corpus.

For each target S6 source window, concatenate its preceding two windows, if
present, and itself. Thus context is causal, at most 3072 consecutive 256-frame
bins; do not look into the next window or bridge a missing source range. A
target's final short bin retains its actual source range. Keep each target
outcome independently, including early/short-context abstentions.

For local bin index `i`, observation `x[i]` is that exact native f32 novelty
value in [0,1]. Define 128 tempo states `j=0..127`:

```text
B[j] = 40 * (300 / 40) ** (j / 127)
L[j] = sample_rate * 60 / (256 * B[j])      # nominal interval in bins
```

Cells `(i,j)` retain the two highest-scoring distinct complete paths ending
at a beat at bin `i` with state `j`. A path starts only at `0 <= i < L[j]`,
with score `(i+1)*x[i]`. A transition from `(h,k)` is allowed exactly when
`h < i`, `abs(k-j) <= 1`, and `abs((i-h)-L[j]) <= 1`. Its added score is:

```text
d = i-h
increment = d*x[i] - abs(d-L[j]) - abs(L[j]-L[k])
```

This observes weak/missing beats without an event threshold, compensates
beat-count reward by elapsed interval, and penalizes interval/tempo changes in
the same bin units. Coefficients are exactly one; do not fit or sweep them.
Enumerate both predecessor paths, and include an eligible start as another
candidate. Deduplicate only identical `(bin,tempo-state)` path sequences.
Ties prefer the lexicographically earlier full sequence, first by bin, then
tempo-state index. Retain two per cell; this bounded truncation is explicit,
not a proof of exhaustive posterior ambiguity.

Terminal states require `N-1-i < L[j]`; add trailing score `(N-1-i)*x[i]`
and divide by `N`, the context bin count. Backtrack every retained eligible
terminal path, not only a single phase per tempo. Require at least eight
context beats and four path beats whose source-bin starts lie inside the target
window. Fit source frame against consecutive **path ordinal** with ordinary
least squares using only those target-window beats. Do not refit to annotations.
Reject a nonpositive/nonfinite period, a tempo outside 40..300 BPM or maximum
absolute fit residual exceeding 0.10 fitted beat. Missing observation strength
does not remove its beat or renumber ordinals.

Convert the fitted period to nearest integer milli-BPM, half up. Use the fitted
line's first position inside the target window, rounded to nearest whole source
frame, half up, as anchor. If rounding crosses the window boundary or fails
geometry validation, reject that terminal proposal. Score the resulting exact
Q32 grid, not the pre-conversion fit. The continuous path is internal evidence;
this first oracle still proposes one constant grid per existing S6 window and
can therefore fail expressive/variable-tempo cases. Do not change the scorer
to hide that limitation.

Order valid terminal proposals by descending normalized path score, then
lexicographic full path. The runner is the first proposal differing by >1%
period OR >0.10 winning-period cyclic phase after output conversion. Do not
collapse same-period phases before finding that runner. Emit only if the
winner's score and its distinct-runner margin are strictly positive; absent
runner scores zero. Exact ties abstain. Report scores/margins diagnostically;
there is no calibrated probability or extra margin threshold. Confidence is
fixed at 49, no octave preference, baseline fallback, meter prior or rescue.
Do not output a meter/downbeat from this timing-only oracle.

Before real scoring, synthetic tests must cover tempo aliases and same-period
phase ambiguity, weak/subthreshold evidence, missing beats, slow interval
change, unsupported abrupt change, paths with equal scores, target-window
source offsets, short context, silence, malformed counts/context, lattice
round trips and context/source reset. Test independently derived expected
coordinates; passing these tests is implementation evidence only.

## C — one ordered external-development attempt

Freeze exact source/tooling/config/trace hashes before this stage. Run ASAP
development first, then Ballroom development only if ASAP passes. Reuse the
unchanged annotated-development `score_window` implementation, its local
nearest-five-interval reference period, 1% period / 0.10-beat phase rules and
source-only reference completeness. No source/window removal by output.

For each corpus report all 240 windows, reference-complete and incomplete
counts, emissions/abstentions, per-beat and whole-window period/phase/joint
correctness, per-meter strata, high-confidence errors/unknown context and
paired fixes/breaks/changed verdicts against coarse. No emitted grid means
failure in unconditional and complete-reference denominators. A candidate
emission on an incomplete-reference window has unknown whole-window accuracy.

Reject before native selector implementation unless **each** corpus has:

- strictly more confirmed period-correct AND joint-correct windows;
- zero breaks among coarse period-, phase- or joint-correct windows, including
  candidate abstentions; use all source windows for paired tracking;
- no high-confidence safety regression, missing evidence, identity drift or
  operational failure.

Stop on the first failing corpus. Do not run the second as a rescue or tune the
tempo spacing, path penalties, context, support, residual or gate afterward.
The spent DJ set, formal ASAP/Ballroom/GiantSteps/FMAK holdouts and fresh DJ pool
are outside this experiment. Both external development sets are already open;
they are not fresh acceptance or a generalization guarantee.

## Resource gate and later native adoption

The host probe caps numeric workspace at 16 MiB, process RSS at 256 MiB and
selection at 120 seconds per thirty-second source. Its maximum dense table is
`3072*128*2 = 786432` cells: f64 scores plus packed u32 predecessor references
take 9437184 bytes before other numeric scratch. Record actual RSS, wall/CPU,
cell counts, capture size and compared baseline costs. Exceeding a cap stops
this registered attempt; no silent beam pruning or shorter context.

Only if C passes on both corpora may an exact opt-in Rust selector be attempted
behind `experimental-s6-temporal-grid`. Its caller-owned total storage cap is
4 MiB with <=64 KiB internal scratch, zero heap, explicit placement requirements
and cooperative cancellation/work budgeting. Measure it; host bounds do not
establish that this native budget is achievable. An implementation needing a
different approximation or decision rule needs a new registration first.

Before any product adoption, verify native/oracle agreement, default byte
identity, same-grid meter/downbeat/key nonregression, immutable publication,
source/generation replacement and actual Pajoniiir validator behavior. Do not
borrow old meter records onto a new grid. A native timing-only improvement
cannot close meter or downbeat gates. Physical P4 memory placement, stack,
timing and USB/audio coexistence remain separate; retain the existing wire
eight-segment limit, production provider, version and consumer pin.

## Registration checkpoint

The original document and JSON register the experiment, not a detector result. Exact
retained baselines, source/binary identities and protocol hashes are verified
and archived externally before implementation. No native/Rust code, feature,
selector, scorer, audio, label or acceptance result changes in that checkpoint.
No accuracy gain, fixes/breaks, runtime or P4 claim is inferred from it.

Stage A is now complete in the separate capture checkpoint, with 240 unchanged
ordinary files and 480 complete dense windows. Stage B is now implemented and
synthetically verified in its separate checkpoint; it does not establish
musical accuracy. **Next executable step: C, the frozen ordered external attempt,
ASAP development first.** Stop before Ballroom if any ASAP gate fails.
