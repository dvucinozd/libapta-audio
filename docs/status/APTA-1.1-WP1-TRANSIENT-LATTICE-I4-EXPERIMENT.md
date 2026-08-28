# APTA 1.1 WP1 transient-lattice iteration 4

- **Status:** pre-registered oracle candidate, not implemented
- **Frozen baseline revision:** `814840ee0157c0c364be2960c7ffa5765720dede`
- **Evidence class:** diagnostic/development only
- **Formal holdouts:** unopened

## Failure-driven hypothesis

I3's sixteen-bin local contrast improved period transfer on both open
development corpora, but replacing the baseline novelty changed nearly every
DJ verdict and produced five beatgrid breaks plus six downbeat breaks on the
spent set. I4 tests whether the local-contrast ceiling can be retained while
restoring baseline stability by combining the two evidence families before
lag and phase selection.

The fusion is causal and scale-neutral. It requires neither a second evidence
buffer nor an additional pass, and it introduces no threshold or rescue rule
derived from track labels.

## Frozen formula and flag

Let `b[i]` be the existing multiband-onset novelty and `c[i]` the exact I3
sixteen-bin local contrast. Including the current bin, define:

```text
b_mean[i] = sum(b[0..i]) / (i + 1)
c_mean[i] = sum(c[0..i]) / (i + 1)
b_norm[i] = b[i] / b_mean[i] when b_mean[i] > float64 epsilon, else 0
c_norm[i] = c[i] / c_mean[i] when c_mean[i] > float64 epsilon, else 0
novelty[i] = 0.50 * b_norm[i] + 0.50 * c_norm[i]
```

Production uses the corresponding float32 arithmetic and a positive-mean
test; the diagnostic epsilon only avoids division by numerical zero. The
weights, causal mean and I3 history are frozen before any I4 analyzer run.

The opt-in flag will be
`APTA_ENABLE_EXPERIMENTAL_TRANSIENT_LATTICE_I4=ON`. It must require
`APTA_ENABLE_EXPERIMENTAL_MULTIBAND_ONSET=ON` and be mutually exclusive with
I1, I2 and I3. The default build remains unchanged.

## Oracle checkpoint

The oracle extension adds only two fixed, theory-driven equal-fusion forms:
global-mean as an architectural reference and causal-mean as the deployable
candidate. No weight, threshold or history grid was searched. Repeated reports
are byte-identical.

Relative to captured multiband evidence, the frozen causal candidate produced:

| Corpus | Metric | Baseline | I4 oracle | Fixes | Breaks | Net |
|---|---|---:|---:|---:|---:|---:|
| ASAP development | top-1 period | 2 | 5 | 4 | 1 | +3 |
| ASAP development | top-1 phase | 1 | 2 | 1 | 0 | +1 |
| ASAP development | top-1 period+phase | 0 | 0 | 0 | 0 | 0 |
| Ballroom development | top-1 period | 10 | 13 | 3 | 0 | +3 |
| Ballroom development | top-1 phase | 11 | 14 | 3 | 0 | +3 |
| Ballroom development | top-1 period+phase | 7 | 10 | 3 | 0 | +3 |

Privacy-safe report SHA-256 values are:

- ASAP development: `177a41f426e6d399986a0f523bf5c942e18a6fb13fe5096a7ffe6bcd36c65305`;
- Ballroom development: `684c8e4ff7a9fea0025ae649ad5ca6494759cd7cc61f5274f2a6ae7ec04cb5fa`.

## Verification and retain/reject gate

Before corpus execution, default/I4 Werror and I4 ASan/UBSan matrices must
pass; default artifacts must remain byte-identical; invalid flag combinations
must be rejected; one production trace must match the frozen oracle within
`5e-7`; persistent workspace and result-pool sizes must not grow; median S4
flux overhead must be at most 50% and median full-path overhead at most 5%.

After those gates pass, evaluate the exact frozen candidate on both open
ASAP/Ballroom development partitions. Only positive transfer on both permits
the already-spent 60-track DJ run. Retain only with at least five net
period/phase fixes, at most one spent-set break and no high-confidence safety
regression. Any failure rejects I4 without opening a formal holdout or making
an acceptance claim.
