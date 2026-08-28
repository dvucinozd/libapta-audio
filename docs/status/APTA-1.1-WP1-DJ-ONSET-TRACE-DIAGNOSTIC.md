# APTA 1.1 WP1 spent-DJ onset-trace diagnostic

- **Status:** diagnostic checkpoint complete
- **Trace source revision:** `fb6c2e13bfe5a2ff9b71fe3c96dd4d30f05a955e`
- **Evidence class:** already-spent development corpus
- **Acceptance claim:** false
- **Formal holdouts:** unopened

## Boundary

This checkpoint diagnoses the six rejected WP1 iterations without consuming
new acceptance evidence. The input is the frozen 60-track DJ corpus already
used during WP0/WP1 development. All source filenames were replaced by opaque
`track-<sha256-prefix>` IDs before trace capture; temporary opaque audio copies
were deleted immediately after each run. The trace and report directories are
ignored build outputs.

`tools/apta_1_1_dj_onset_trace_oracle.py` is intentionally separate from the
ASAP/Ballroom oracle. It accepts only the frozen DJ manifest format and CSV
schema, verifies the manifest-bound labels hash and exact ID coverage, rejects
non-opaque IDs, and emits fixed metadata:

- `acceptance_claim: false`;
- `evidence_level: development`;
- `corpus_status: spent`.

The truth period is the frozen `beat_period_frames / 256`. Beat-phase error is
measured against the constant beat sequence implied by the musician-verified
`downbeat_frame` and beat period. Period tolerance remains 1% and phase
tolerance remains 0.10 beat. This diagnostic neither changes nor relaxes the
WP1 analyzer promotion gate.

## Reproducibility and validation

Trace capture used the multiband production baseline plus the opt-in meter
trace, with no transient iteration enabled. The trace executable SHA-256 was
`313416ab6cd1fb71c4a04e6a0abc1673bf7c7feaba77a2c5ada206c610cfc5b7`.
The frozen input bindings were:

- manifest SHA-256:
  `27e017ca5e609e70a29bacf0df1a613a8fc87be33d1701924c0fc5d388034521`;
- labels SHA-256:
  `e7eac4ab8a80019b3da558c347d36242b827e485c7c66dff27c72fa8c25abbb8`;
- 60-file trace-set SHA-256:
  `41d490fd0573a785a1c39256eeb0ee936c91efaf2850781f8ad0130ffdf8f6a7`.

All 60 JSON documents parsed, had stride four, exact energy/flux geometry,
finite values, exact manifest coverage and privacy-safe source IDs. All 60
captured top-three candidate sets matched the offline scorer. Twenty-five
tracks have at least one printed/quantized band-energy value outside the tight
vectorized reconstruction tolerance, but reconstructed and captured production
evidence have identical aggregate candidate results and transitions.

The oracle ran under Python 3.12.3 and NumPy 2.5.2. Two independent executions
produced the byte-identical report SHA-256
`946a5ee4962e6052613e8b22f708dfd41208e72dc25beaf7b9fea1250a759f85`.
A negative test using a CSV not bound by the manifest was rejected before any
report was written.

## Fixed-formula results

| Evidence | Top-1 period | Period fix/break | Top-1 phase | Phase fix/break | Joint top-1 | Joint fix/break | Top-3 period | Top-3 joint |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Captured multiband | 39/60 | 0/0 | 23/60 | 0/0 | 23/60 | 0/0 | 49/60 | 25/60 |
| Reconstructed multiband | 39/60 | 0/0 | 23/60 | 0/0 | 23/60 | 0/0 | 49/60 | 25/60 |
| Broadband rise | 36/60 | 0/3 | 21/60 | 0/2 | 21/60 | 0/2 | 49/60 | 24/60 |
| Log band flux | 41/60 | 5/3 | 23/60 | 6/6 | 23/60 | 6/6 | 48/60 | 27/60 |
| Adaptive whitened | 37/60 | 5/7 | 16/60 | 6/13 | 16/60 | 6/13 | 45/60 | 24/60 |
| Local contrast 16 | 49/60 | 12/2 | 27/60 | 10/6 | 27/60 | 10/6 | 50/60 | 32/60 |
| Equal-mean baseline/contrast | 43/60 | 5/1 | 26/60 | 5/2 | 26/60 | 5/2 | 54/60 | 33/60 |
| Causal-mean baseline/contrast | 43/60 | 6/2 | 27/60 | 6/2 | 27/60 | 6/2 | 54/60 | 34/60 |
| Causal-harmonic baseline/contrast | 44/60 | 7/2 | 30/60 | 8/1 | 30/60 | 8/1 | 52/60 | 35/60 |
| Centered contrast curvature | 39/60 | 7/7 | 18/60 | 4/9 | 18/60 | 4/9 | 48/60 | 26/60 |

## Decision

The trace confirms that local contrast contains useful DJ transient timing,
but replacing production evidence for both lag ranking and phase selection is
not stable: the same families introduced open-development breaks in I3 through
I6. Causal harmonic fusion is the strongest fixed DJ compromise, improving
top-1 period from 39 to 44 and joint period/phase from 23 to 30, with one phase
break. Its earlier ASAP period result still had two breaks, so I5 remains
rejected and is not revived.

The next permissible hypothesis must isolate the useful phase evidence from
the unsafe period reranking. A separately pre-registered I7 may keep captured
production lag candidates and ordering unchanged while using causal harmonic
evidence only for the phase argmax at those fixed lags. No formal holdout or
fresh acceptance corpus may be opened before that candidate is frozen and has
passed all open-development, spent-set, resource and default-invariance gates.
