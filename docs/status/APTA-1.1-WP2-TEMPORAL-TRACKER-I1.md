# APTA 1.1 WP2 temporal tracker iteration 1

- **Status:** pre-registered; no WP2-I1 oracle or implementation result collected
- **Frozen baseline revision:** `37afcdd2c4c201f1091a624479ddb421651b18ba`
- **Evidence class:** diagnostic/development only
- **Formal holdouts:** unopened

## Failure-driven hypothesis

WP1 closed without promoting a new onset representation. On the already-spent
DJ trace, however, production evidence contains the correct period in its
top-three candidates on 49/60 tracks while selecting it first on only 39/60.
The missing information is therefore often temporal consistency among existing
candidates rather than another onset transform.

WP2-I1 tracks the existing bounded lag/phase states across consecutive windows.
A true beat lattice should keep a similar period and predict a continuous beat
phase from one window to the next; an isolated alias may win one autocorrelation
but should not form the best continuous path.

## Frozen windows, states and score

The constants are derived from existing S4 geometry rather than corpus results:

- window length: `APTA_INTERNAL_STABLE_TEMPO_BINS = 1024` bins;
- hop: `APTA_INTERNAL_MIN_TEMPO_BINS = 512` bins;
- states per window: `APTA_INTERNAL_MAX_TEMPO_CANDIDATES = 3`;
- for a 4,096-bin frozen trace, starts are 0, 512, 1024, 1536, 2048,
  2560 and 3072;
- each state is the existing candidate lag, existing score and phase argmax for
  that window's captured production novelty.

For candidate `j` in window `t`, normalize its positive existing score by that
window's best score and freeze the emission as:

```text
emission(t, j) = log(score(t, j) / best_score(t))
```

For a transition from state `i` to `j`, define:

```text
tempo_distance = abs(log2(lag_j / lag_i))

absolute_phase = window_start + local_phase
phase_distance = distance from state j's first predicted beat in the new
                 window to the nearest beat of state i's continued grid,
                 divided by (lag_i + lag_j) / 2

transition(i, j) = -tempo_distance - phase_distance
```

The Viterbi recurrence is `emission + best(previous + transition)`. Initial
states use emission only. Equal scores retain the lower predecessor rank and
then the lower current rank. There is no transition coefficient, threshold,
beam width, tempo-ratio exception, fitted prior or label-conditioned branch.

After the seventh local window, score each of the original full-4,096-bin
production candidates as a terminal state using its normalized full-window
emission plus the same transition from the best path terminal. The selected
candidate must be one of those original three; its lag, refined offset and
published candidate record are not synthesized. Promotion moves that complete
record to rank zero while preserving the remaining relative order and existing
serialization invariants.

For that terminal comparison, translate each full-window phase by the smallest
non-negative integer multiple of its lag that places a predicted beat on or
after the final local-window start (bin 3072). Compare that absolute beat with
the nearest continued beat of the seventh-window state. This is only a change
of phase origin; it does not alter either candidate lag or phase.

## Staged oracle and retain/reject gates

1. Implement only a privacy-safe offline oracle over captured production
   novelty. Reuse the exact S4 lag scorer, tempo prior and phase argmax. Do not
   test alternate windows, hops, transition functions, weights, state counts,
   terminal rules or tie breakers.
2. Require each temporal selection to be one of the unchanged full-window
   top-three candidates. Reject before native implementation if top-1 period
   transfer is not positive on both open ASAP and Ballroom development, if
   either has more than one period break, or if phase/joint transfer is
   negative on either corpus.
3. Only after the external oracle gate, run a separate strict oracle on the
   already-spent 60-track DJ trace. Reject before implementation unless period
   correctness reaches at least 47/60, has at least eight net fixes and at most
   two breaks, phase/joint transfer is non-negative, and the 49/60 top-three
   period ceiling is unchanged.
4. Implement the exact bounded tracker behind
   `APTA_ENABLE_EXPERIMENTAL_TEMPORAL_LATTICE_I1=ON`. It must require
   multiband onset, remain incompatible with rejected transient flags and leave
   the default build byte-identical.
5. Before native corpus execution require default/WP2-I1 Werror and WP2-I1
   ASan/UBSan matrices, invalid-flag rejection, oracle/production selection
   agreement on synthetic and real traces, cancellation/refresh/rollover tests,
   no result-pool growth, measured bounded workspace, S4 CPU overhead at most
   100% and full-path overhead at most 15%.
6. Run exact native ASAP and Ballroom development partitions. Reject for
   negative meter, downbeat, period or phase transfer on either corpus, any
   high-confidence safety regression, or any operational regression.
7. Only then run the already-spent DJ corpus. Retain only if native period/
   beatgrid results meet the same 47/60, at-least-eight-net-fix and at-most-two-
   break gate, with non-negative meter/downbeat/key transfer and no safety or
   operational regression.

Any failed stage rejects WP2-I1 without changing the frozen formula, opening an
ASAP/Ballroom formal holdout, consuming a fresh acceptance corpus or making an
acceptance claim. Passing all stages retains an opt-in candidate; it does not
by itself authorize a formal holdout or release promotion.
