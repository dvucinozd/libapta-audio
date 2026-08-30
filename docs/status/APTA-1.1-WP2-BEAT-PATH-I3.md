# APTA 1.1 WP2 beat-path tracker iteration 3

- **Status:** rejected by the external-development oracle gate
- **Frozen baseline revision:** `b20df60bde0260a85a99fad7ab5250fb82f509b1`
- **Evidence class:** diagnostic/development only
- **Acceptance claim:** false
- **Formal holdouts:** unopened

## Failure-driven hypothesis

WP2-I1 and I2 summarized seven overlapping windows into either an unweighted
candidate-state path or a strict winner vote. Neither model inspected whether
individual onset peaks form a locally continuous beat-to-beat path. They both
failed external-development transfer and are closed.

Production evidence nevertheless contains the correct period in its top three
on 49/60 tracks of the already-spent DJ diagnostic while rank zero is correct
on 39/60. I3 tests a different temporal axis: a valid full-window candidate
should be supported by a sequence of nearby onset peaks whose local phase
offset changes gradually from beat to beat. This can distinguish a continuous
pulse from an alias without adding another window vote, Viterbi coefficient or
threshold rescue.

## Frozen candidate set and path score

I3 consumes the exact 4,096-bin production novelty trace and reproduces the
existing ordered full-window top three. It cannot add a lag, change a lag,
change a candidate score or synthesize a phase. For each existing candidate
with lag `L`, original phase `P`, positive score `S` and trace mean `M`, freeze:

```text
radius R = max(1, floor(0.10 * L))
expected beat E_n = P + n * L
usable beats = all E_n for which [E_n - R, E_n + R] is inside the trace
offset states D = 0, -1, +1, -2, +2, ... , -R, +R

emission(n, d) = log1p(novelty[E_n + d] / M), when M > 0
transition(d_prev, d) = -abs(d - d_prev) / R

path(n, d) = emission(n, d)
             + max_d_prev(path(n - 1, d_prev) + transition(d_prev, d))

path_mean = max_d(path(last, d)) / usable_beat_count
candidate_total = log(S / S_rank0) + path_mean
```

The `0.10` radius is the already-frozen release beat-phase tolerance, not a
corpus-derived constant. `log1p` makes onset support dimensionless, the mean
normalizes trace scale, and the unit transition coefficient operates on the
already-normalized offset distance. There is no fitted weight, tempo-ratio
exception, label-conditioned route, confidence rescue or threshold.

Equal predecessor totals retain the earlier offset state, which prefers zero,
then the smaller absolute offset, then negative before positive. Equal final
candidate totals retain the lower original candidate rank. At least two usable
beats and a positive finite trace mean are required; otherwise I3 preserves
rank zero. The selected candidate's complete original lag, refined offset,
score, phase and published record remain unchanged.

## Staged evidence and frozen gates

1. Implement only a privacy-safe oracle and deterministic synthetic tests.
   Cover a stable pulse, phase drift within the radius, erratic peaks, silence,
   insufficient evidence, candidate ties and malformed trace topology. Do not
   inspect ASAP, Ballroom or spent-DJ I3 results before this protocol and the
   exact oracle are committed.
2. Run I3 once on the already-open 40-track ASAP and 40-track Ballroom
   development traces. The oracle must reproduce every captured ordered top
   three and select only from it. Reject before native implementation unless
   top-1 period and joint period/phase both transfer positively on both
   corpora, neither metric has more than one break on either corpus, phase
   transfer is non-negative and the top-three period ceiling is unchanged.
3. Only after both external gates pass, run the already-spent 60-track DJ
   trace. Reject before implementation unless period correctness reaches at
   least 47/60, has at least eight net fixes and at most two breaks, joint
   period/phase transfer is non-negative and the 49/60 top-three ceiling is
   unchanged.
4. Only after both oracle stages pass, implement the exact bounded selector
   behind `APTA_ENABLE_EXPERIMENTAL_BEAT_PATH_I3=ON`. Require byte-identical
   default artifacts, invalid-option rejection, oracle/native agreement,
   Werror and sanitizer matrices, no result-pool growth, measured P4 workspace,
   S4 CPU overhead at most 100% and full-path overhead at most 15%.
5. Run exact native ASAP and Ballroom development partitions. Reject for a
   meter, downbeat, period or phase regression on either corpus, any
   high-confidence safety regression or any operational regression.
6. Only then run the spent DJ corpus. Retain I3 only if native period/beatgrid
   meets the same 47/60, at-least-eight-net-fix and at-most-two-break gate,
   with non-negative meter/downbeat/key transfer and no safety or operational
   regression.

Any failed stage rejects I3 without changing the radius, normalization,
emission, transition, candidate combination, tie rule or gate. It does not
authorize an adjacent path formula, a formal holdout, a fresh acceptance
corpus or an acceptance claim.

## Frozen oracle outcome — rejected 2026-08-30

The one permitted external-development run used clean oracle revision
`a5a88105e3b7dcc07b810137e45bf332b2c37bed`. A preliminary invocation had
stopped before evaluation or output because the generic trace loader required
unused band-energy arrays. Revision `a5a8810` narrowed validation to the exact
I3 inputs, added a minimal-schema regression test and was committed before the
first result-producing run.

Both corpora reproduced the captured ordered top three on 40/40 tracks and
selected only from those states. The frozen paired result was:

| Corpus | Metric | Production | I3 | Fixes | Breaks |
|---|---|---:|---:|---:|---:|
| ASAP | period | 2/40 | 2/40 | 0 | 0 |
| ASAP | phase | 1/40 | 0/40 | 0 | 1 |
| ASAP | joint period/phase | 0/40 | 0/40 | 0 | 0 |
| Ballroom | period | 9/40 | 12/40 | 4 | 1 |
| Ballroom | phase | 11/40 | 13/40 | 3 | 1 |
| Ballroom | joint period/phase | 7/40 | 9/40 | 3 | 1 |

I3 retained rank zero on 29 ASAP and 22 Ballroom tracks, promoted rank one on
9 and 13, and promoted rank two on 2 and 5 respectively. Ballroom therefore
shows positive period, phase and joint signal with only one break per metric.
ASAP has no positive period or joint transfer and loses its sole correct phase.
It fails the conjunctive external gate and is rejected before the spent-DJ
trace, native implementation, resource measurement or any formal holdout.

The I3 Ballroom production count is 9/40 rather than the 10/40 recorded by the
historical I1/I2 reports. Those older documents did not retain their trace-set
hash, so the exact private-artifact drift cannot be reconstructed. I3's paired
decision is bound to the exact hashes below and does not reinterpret either
older result.

Privacy-safe retained bindings are:

- ASAP labels / trace set / report SHA-256:
  `7f6d5e40c8771df6052fd31f46a0fb0d0e95ab990c8d89f00c4ef1fe41d882c0` /
  `72089f349db6fef80cd50c30d5a74df17ca75cc9ec4149e576c35f884d048963` /
  `0154ec618cb6af7d2efa75be38677d5904e921139310654bb5a629e54a70b9e7`;
- Ballroom labels / trace set / report SHA-256:
  `5aa77e0b23233a38480d43a77b67f63f2407c91ac444073efce1bf8f0214d323` /
  `2a1fd33d10a2fb93faf3d82c79a56c12c23d88152bb6728b148ef686f43a47b8` /
  `b0f4f7636eeaa00c3eb5343566bbb79011b6053d7cbfa619c0013a39c6f23c92`.

No radius, emission, transition, score combination or tie variant was tested.
The two I3 reports are ignored private artifacts and contain only opaque track
IDs.
