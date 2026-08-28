# APTA 1.1 WP1 transient-lattice iteration 3

- **Status:** rejected after open-development and spent-corpus evaluation
- **Frozen baseline revision:** `672939ac56a2e01997afd3723373894737881b88`
- **Evidence class:** diagnostic/development only
- **Formal holdouts:** unopened

## Failure-driven hypothesis

Iteration 1's additive fast/slow/refractory signal was expensive and produced
no net spent-set fixes. Iteration 2's amplitude-normalized mixture passed its
resource gate but regressed ASAP downbeat. The raw-evidence oracle then showed
that log compression, broadband-only rise and adaptive whitening reduce the
candidate ceiling. A simpler sixteen-bin local contrast was the only tested
family with positive top-1 and top-three period transfer on both open
development corpora.

Iteration 3 tests exactly that oracle formula in the analyzer. It asks whether
a slow local floor alone can suppress sustained/tail energy without the
previous-bin and refractory terms that made I1/I2 change phase erratically.

## Frozen formula and flags

The isolated baseline enables only
`APTA_ENABLE_EXPERIMENTAL_MULTIBAND_ONSET=ON`. The candidate additionally
enables `APTA_ENABLE_EXPERIMENTAL_TRANSIENT_LATTICE_I3=ON`. I1, I2 and I3 are
mutually exclusive.

For each low/mid/high/broadband channel, with `floor` equal to the mean of up to
the sixteen preceding bins:

```text
contrast(x, floor) = max(x - floor, 0) / (x + floor + 1/255)
novelty = 0.50 * broadband_contrast
        + 0.50 * mean(low_contrast, mid_contrast, high_contrast)
```

The history length and `1/255` denominator floor were fixed by the trace-oracle
protocol and retained magnitude quantization before any I3 analyzer run. There
are no tunable weights, thresholds, previous-rise or refractory terms.

## Verification and resource gates

- default Werror, I3 Werror and I3 ASan/UBSan matrices pass;
- I1/I2 regression fixtures and the production-ring synthetic coverage pass;
- the default analyzer remains byte-identical to WP0;
- `sizeof(apta_internal_onset_bin_t)` remains at or below sixteen bytes;
- no persistent workspace or result-pool allocation is added;
- fixed rolling state exists only in a no-inline S4 flux-fill call;
- median S4 flux overhead is at most 50% and median full-path total overhead is
  at most 5% on three alternating 120-second cost-probe runs per build.

## Retain/reject gate

Retain only with at least five net period/phase fixes and at most one break on
the spent 60-track DJ development set, positive transfer on both open ASAP and
Ballroom development partitions, and no high-confidence safety regression.
Reject immediately for a default change, operational/sanitizer failure,
allocation or layout growth, resource-gate failure, more than one spent-set
break, or negative transfer on either open development partition. A rejection
does not open a holdout or create acceptance evidence.

## Pre-corpus implementation checkpoint

The default Werror matrix passes 120/120, the I3 Werror matrix 122/122 and the
I3 ASan/UBSan matrix 118/118. The production-ring synthetic fixture covers
silence, isolated impulses, sustained decay, syncopation, kick/snare
alternation, off-beat hats and ring rollover. Dependency and mutual-exclusion
configure guards reject invalid flag combinations.

The default analyzer remains byte-identical to WP0, SHA-256
`0e7999efb61734f656b846d5542617454c5a0789224531c071d0f8555512383a`.
On a real 4,096-bin trace, every I3 production flux value matches the frozen
NumPy oracle within `5e-7`; maximum absolute difference is
`4.5446407921038295e-7`, attributable to float32 versus double arithmetic.

Against multiband-only, full P4 30-minute workspace remains 941,248 bytes,
recommended workspace 1,000,092 bytes and result pool 537,104 bytes.
`apta-analyze` shrinks from 221,320 to 217,280 bytes (-4,040), and `libapta.a`
from 483,118 to 482,510 bytes (-608). The no-inline rolling state is bounded at
288 bytes and adds no persistent allocation.

Three alternating 120-second cost-probe runs per build produced identical
evidence-bin and refresh-scan counts. Median results are:

| Path | Total baseline | Total I3 | Total delta | S4 flux baseline | S4 flux I3 | Flux delta |
|---|---:|---:|---:|---:|---:|---:|
| BPM | 1,197.916 ms | 1,185.589 ms | -1.03% | 51,406 us | 30,296 us | -41.07% |
| full | 1,280.987 ms | 1,281.153 ms | +0.013% | 42,447 us | 25,134 us | -40.79% |

Both frozen CPU gates pass with substantial margin. I3 is now frozen for exact
revision-bound spent-set and open-development evaluation.

## Frozen evaluation outcome

The exact candidate revision was
`1814a2a382ae0ddce04468efedf91d22bf9a7b2c`. Each open-development run
completed all 40 tracks with no execution failure. Relative to the
multiband-only baseline:

| Corpus | Metric | Baseline | I3 | Fixes | Breaks | Net |
|---|---|---:|---:|---:|---:|---:|
| ASAP development | meter | 19 | 23 | 7 | 3 | +4 |
| ASAP development | downbeat | 3 | 5 | 4 | 2 | +2 |
| ASAP development | period <=1% | 1 | 3 | 2 | 0 | +2 |
| ASAP development | period <=10% | 3 | 9 | 6 | 0 | +6 |
| Ballroom development | meter | 21 | 23 | 4 | 2 | +2 |
| Ballroom development | downbeat | 6 | 7 | 5 | 4 | +1 |
| Ballroom development | period <=1% | 8 | 18 | 10 | 0 | +10 |
| Ballroom development | period <=10% | 17 | 24 | 7 | 0 | +7 |

This positive transfer permitted evaluation on the already-spent 60-track DJ
development corpus. Both exact runs completed 60/60 with zero execution
failures. Key remained 20/60 and meter improved from 58/60 to 59/60. Beatgrid
remained 5/60 with five fixes and five breaks, while downbeat fell from 6/60
to 5/60 with five fixes and six breaks. The candidate therefore has zero net
beatgrid fixes, negative-one net downbeat fixes and more than the permitted one
break. It fails the frozen retain gate and is rejected.

The comparison confirms zero persistent-workspace, recommended-workspace and
result-pool delta; analyzer and static-library binaries shrink by 4,040 and
608 bytes respectively. These resource results do not override the period and
phase veto.

Privacy-safe evidence SHA-256 values are:

- ASAP baseline report: `f1ff378cb9f03d0bff071d33fa099b34bbdb8e4476e41d683ec9116fcdb64a60`;
- ASAP I3 report: `3cf307ee919ac5704cf8aa56e36eb0cdd4d7703f54aecd8e00946cf7c6eec381`;
- Ballroom baseline report: `abc9f8d79acadaa0221c5529ed65380be34d6781ee98505ae5f5d330b48967d2`;
- Ballroom I3 report: `f5112245a4e7c8b04739ec0444aac0deecf1a6681bc144c89665228f11ae0b13`;
- spent DJ comparison: `0801f51e4ceb8a7bb71fc6011f8ecbbc6c0a45cce27269b8baccc440e74565a9`.

This is development evidence only and makes no acceptance claim. No ASAP,
Ballroom or DJ formal holdout was opened.
