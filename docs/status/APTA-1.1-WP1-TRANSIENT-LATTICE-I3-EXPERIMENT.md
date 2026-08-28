# APTA 1.1 WP1 transient-lattice iteration 3

- **Status:** implementation verified, pre-corpus
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
