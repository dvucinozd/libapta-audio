# APTA 1.1 WP1 onset-trace oracle

- **Status:** diagnostic checkpoint complete
- **Trace source revision:** `da9e3fe2e92c8e0d3e3b05a66cd82275df3507ea`
- **Evidence class:** open development only
- **Acceptance claim:** false
- **Formal holdouts:** unopened

## Purpose and boundary

Transient iterations 1 and 2 changed the lattice but failed their frozen
retain gates. This checkpoint exposes and evaluates the exact S4 onset-bin
evidence rather than proposing another weighted formula from aggregate scores.
The local trace inputs contain opaque track IDs and exact normalized
low/mid/high/broadband arrays aligned with captured production flux. They remain
under ignored build directories and are not release or acceptance evidence.

`tools/apta_1_1_onset_trace_oracle.py` evaluates six fixed evidence families:
captured and reconstructed production multiband flux, broadband rise,
quantization-derived log band flux, sixteen-bin local contrast and sixteen-bin
adaptive-whitened flux. It reproduces S4's 40–300 BPM lag scan, log-normal tempo
prior, top-three ordering and phase argmax. Period is scored within 1%; beat
phase uses the frozen 0.10-beat tolerance. It requires NumPy and rejects trace
coverage that does not exactly equal the development IDs in the balanced
development/holdout label manifest.

## Reproducibility

The trace tool SHA-256 was
`313416ab6cd1fb71c4a04e6a0abc1673bf7c7feaba77a2c5ada206c610cfc5b7`.
All 40 ASAP and all 40 Ballroom captured top-three candidate sets matched the
offline scorer exactly. Reconstructed multiband values can differ at printed
float precision when `current_total > previous_total` is tied, but their
top-three summaries and transitions match captured production evidence.

Each oracle was executed twice. Byte-identical report SHA-256 values were:

- ASAP development:
  `5d3596168befe07e58020c2a78b0abb7be5500182f51b972137290094928770a`;
- Ballroom development:
  `a554ac512caa500d520c0468338bed1544c12c0a872e2d05a016e6f5de7b9730`.

## Results

| Corpus | Evidence | Top-1 period | Fix/break | Top-3 period oracle | Joint top-1 period+phase | Joint fix/break |
|---|---|---:|---:|---:|---:|---:|
| ASAP | captured multiband | 2/40 | 0/0 | 3/40 | 0/40 | 0/0 |
| ASAP | broadband rise | 2/40 | 0/0 | 2/40 | 0/40 | 0/0 |
| ASAP | log band flux | 1/40 | 0/1 | 2/40 | 0/40 | 0/0 |
| ASAP | adaptive whitened | 1/40 | 1/2 | 2/40 | 0/40 | 0/0 |
| ASAP | local contrast 16 | 3/40 | 3/2 | 4/40 | 0/40 | 0/0 |
| Ballroom | captured multiband | 10/40 | 0/0 | 17/40 | 7/40 | 0/0 |
| Ballroom | broadband rise | 9/40 | 0/1 | 17/40 | 7/40 | 1/1 |
| Ballroom | log band flux | 6/40 | 1/5 | 10/40 | 3/40 | 0/4 |
| Ballroom | adaptive whitened | 7/40 | 2/5 | 14/40 | 4/40 | 1/4 |
| Ballroom | local contrast 16 | 14/40 | 4/0 | 20/40 | 10/40 | 3/0 |

## Decision

Log compression and adaptive whitening are closed: both reduce the period
oracle and introduce independent-development breaks. Broadband-only evidence
does not improve the ceiling. The simple sixteen-bin local contrast is the only
tested family with positive top-1 and top-three period transfer on both open
development corpora, and it adds three joint period/phase fixes with zero
breaks on Ballroom. Its ASAP joint ceiling remains unchanged and its top-1
period gain includes two breaks, so this is not sufficient for promotion.

Iteration 3 may test only the structurally simpler, one-pass sixteen-bin local
contrast as an opt-in analyzer path. It must retain the original WP1 spent-set,
external-transfer, confidence and resource gates. No coefficient search,
holdout access or acceptance claim is authorized by this diagnostic result.
