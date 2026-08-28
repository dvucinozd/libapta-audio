# APTA 1.1 WP1 transient-lattice iteration 7

- **Status:** pre-registered; no I7 oracle or analyzer result collected
- **Frozen baseline revision:** `56824862e784890a3f5619b78f5526e95796ae60`
- **Evidence class:** diagnostic/development only
- **Formal holdouts:** unopened

## Failure-driven hypothesis

I5's causal harmonic consensus is useful but unsafe when it replaces production
novelty for both lag ranking and phase selection. On open ASAP development it
introduced two top-1 period breaks. On the already-spent 60-track DJ trace it
nevertheless improved top-1 phase from 23 to 30 with eight fixes and one break,
and joint top-1 period/phase from 23 to 30 with the same transition count.

I7 isolates that timing evidence. The production multiband novelty remains the
sole source of lag candidates, lag scores and candidate ordering. The exact I5
harmonic novelty is used only to choose a phase for each already-selected lag.
This architecture cannot rerank tempo candidates and has no rescue threshold,
label-conditioned branch or fitted coefficient.

## Frozen computation and flag

Let `b[i]` be captured production multiband novelty and let `h[i]` be the exact
sequential-float32 I5 causal harmonic fusion of `b[i]` and the I3 sixteen-bin
local contrast. Freeze the computation as follows:

```text
candidates = production_top_three_lags_and_scores(b)

for each candidate lag L, preserving its score and rank:
    phase_score[p] = sum(h[i]) for every i where i mod L == p
    phase(L) = the lowest p with maximal phase_score[p]
```

No period score is recomputed from `h`, and no baseline/harmonic result is
selected after comparison. Empty/zero harmonic evidence follows the existing
lowest-index argmax behavior. All I5 history length, normalization, arithmetic
order and zero handling remain unchanged.

The opt-in flag will be
`APTA_ENABLE_EXPERIMENTAL_TRANSIENT_LATTICE_I7=ON`. It must require
`APTA_ENABLE_EXPERIMENTAL_MULTIBAND_ONSET=ON`, remain incompatible with the S6
dynamic-tempo path unless that path can preserve the same frozen separation,
and be mutually exclusive with I1 through I6. The default build remains
unchanged.

## Staged verification and frozen gates

1. Add only the frozen hybrid to both privacy-safe development oracles. Do not
   test alternate fusion formulas, phase weights, thresholds, history lengths
   or candidate-routing rules.
2. The oracle period candidate sets, scores and ordering must be exactly equal
   to captured baseline on every traced track. Reject before implementation if
   joint top-1 period/phase does not improve on both open ASAP and Ballroom
   development, or if either corpus has more than one joint break.
3. Evaluate the already-spent DJ trace only after the external oracle gate.
   Reject before implementation unless joint top-1 period/phase has at least
   five net fixes, no more than one break, and an unchanged top-three period
   ceiling.
4. Before any native corpus execution, require default/I7 Werror and I7
   ASan/UBSan matrices, byte-identical default artifacts, invalid-flag
   rejection, exact production/oracle agreement within `5e-7`, no persistent
   allocation growth, S4 flux overhead at most 50% and full-path overhead at
   most 5%.
5. Run exact native ASAP and Ballroom development partitions. Reject for a
   meter regression on either corpus, negative downbeat/phase transfer on
   either corpus, any period-candidate change, or any high-confidence safety
   regression.
6. Only then run the already-spent 60-track DJ development corpus. Retain I7
   only with at least five net beatgrid fixes, at most one beatgrid break,
   non-negative downbeat transfer and no meter, key, operational or
   high-confidence safety regression.

Any rejected stage closes I7 without opening an ASAP/Ballroom formal holdout,
using a fresh acceptance corpus or making an acceptance claim. Passing I7
retains an opt-in WP1 candidate; it does not by itself complete WP1 or authorize
release promotion.
