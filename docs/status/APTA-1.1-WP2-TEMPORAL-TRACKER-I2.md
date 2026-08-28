# APTA 1.1 WP2 temporal tracker iteration 2

- **Status:** pre-registered; no WP2-I2 oracle or implementation result collected
- **Frozen baseline revision:** `ec8f2c6947319257aead025b1a5bb7e120883229`
- **Evidence class:** diagnostic/development only
- **Formal holdouts:** unopened

## Failure-driven hypothesis

WP2-I1 allowed every local emission and transition score to influence the
terminal rank. It promoted a non-baseline candidate on 21/40 tracks in both
open corpora, producing three Ballroom period breaks and negative phase/joint
transfer. A bounded tracker must be substantially more conservative: a local
alias should not replace a full-window winner unless the same candidate family
dominates an absolute majority of independent temporal views.

WP2-I2 removes Viterbi scoring rather than changing a transition weight. It
uses a strict majority vote from the same seven pre-registered windows and
otherwise preserves rank zero.

## Frozen consensus rule

Retain the WP2-I1 geometry and exact production scorer:

- seven 1,024-bin windows at starts 0, 512, 1024, 1536, 2048, 2560 and 3072;
- each window computes the existing ordered top three, but only local rank zero
  casts a vote;
- the terminal choices are exactly the original ordered full-4,096-bin top
  three candidates.

Map each local winning lag `L` to the full candidate `F[k]` minimizing:

```text
distance(k) = abs(log2(L / F[k].lag))
```

Equal distances map to the lower full-window rank. Count seven votes. Promote
a non-baseline full candidate only when it is the unique absolute-majority
winner with at least four votes. If no candidate has four votes, preserve full
rank zero. Since seven is odd, two candidates cannot both have an absolute
majority; the explicit uniqueness rule guards implementation mistakes.

The selected candidate's full-window lag, refined offset, score, phase and
complete published record are unchanged. Promotion only moves that record to
rank zero and preserves the remaining relative order. There is no lag-distance
threshold, score weight, phase weight, transition penalty, ratio exception,
fallback based on labels or fitted constant.

## Staged oracle and retain/reject gates

1. Add only this strict-majority selector to the privacy-safe WP2 oracle. Do
   not test alternate vote thresholds, weighted votes, runner-up votes, window
   geometry, distance metrics or tie rules.
2. Require every selection to remain in the unchanged full-window top three.
   Reject before native implementation if top-1 period transfer is not
   positive on both open ASAP and Ballroom development, if either has more
   than one period break, or if phase/joint transfer is negative on either.
3. Only after the external oracle gate, run a separate strict oracle on the
   already-spent DJ trace. Reject before implementation unless period reaches
   at least 47/60, has at least eight net fixes and at most two breaks,
   phase/joint transfer is non-negative, and the 49/60 top-three ceiling is
   unchanged.
4. Implement the exact selector behind
   `APTA_ENABLE_EXPERIMENTAL_TEMPORAL_LATTICE_I2=ON`, requiring multiband onset
   and leaving the default build byte-identical.
5. Before native corpus execution require default/I2 Werror and I2 ASan/UBSan,
   invalid-flag rejection, exact oracle/production agreement, deterministic
   majority/tie/rollover/cancellation/refresh tests, no result-pool growth,
   measured bounded workspace, S4 CPU overhead at most 100% and full-path
   overhead at most 15%.
6. Run exact native ASAP and Ballroom development partitions, then the spent DJ
   corpus only if every prior gate passes. The native retain gates are the same
   as WP2-I1: no negative external metric or safety/operational regression, and
   DJ period/beatgrid at least 47/60 with at least eight net fixes and at most
   two breaks plus non-negative meter/downbeat/key transfer.

Any failure rejects WP2-I2 without changing the majority rule, opening an
ASAP/Ballroom formal holdout, consuming a fresh acceptance corpus or making an
acceptance claim.
