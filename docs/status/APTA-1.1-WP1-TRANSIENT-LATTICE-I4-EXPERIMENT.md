# APTA 1.1 WP1 transient-lattice iteration 4

- **Status:** rejected after open-development evaluation
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
b_norm[i] = b[i] / b_mean[i] when b_mean[i] > 0, else 0
c_norm[i] = c[i] / c_mean[i] when c_mean[i] > 0, else 0
novelty[i] = 0.50 * b_norm[i] + 0.50 * c_norm[i]
```

Production and the deployable oracle use the corresponding sequential float32
arithmetic and a positive-mean test. The weights, causal mean and I3 history
are frozen before any I4 analyzer run.

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

## Pre-corpus implementation checkpoint

The default Werror matrix excluding the separately exercised source-archive
test passes 120/120. The I4 Werror matrix excluding that same test passes
122/122, and the I4 ASan/UBSan matrix passes 118/118. Dependency and mutual
exclusion configurations reject invalid I4 flag combinations. The default
analyzer is byte-identical to WP0, SHA-256
`0e7999efb61734f656b846d5542617454c5a0789224531c071d0f8555512383a`.

The one-pass implementation keeps a bounded 320-byte-or-smaller stack state
inside the no-inline S4 flux-fill helper and adds no persistent allocation. On
a real 4,096-bin development trace, all production values are finite and the
maximum absolute difference from the exact sequential-float32 oracle is
`1.79077144224e-8`; no bin exceeds the frozen `5e-7` tolerance.

Against multiband-only, full P4 30-minute workspace remains 941,248 bytes,
recommended workspace 1,000,092 bytes and result pool 537,104 bytes.
`apta-analyze` grows from 221,320 to 221,376 bytes (+56), while `libapta.a`
shrinks from 483,118 to 483,102 bytes (-16).

Three alternating 120-second cost-probe runs per build have identical
evidence-bin and refresh-scan counts. Median results are:

| Path | Total baseline | Total I4 | Total delta | S4 flux baseline | S4 flux I4 | Flux delta |
|---|---:|---:|---:|---:|---:|---:|
| BPM | 1,239.667 ms | 1,277.899 ms | +3.08% | 51,984 us | 46,162 us | -11.20% |
| full | 1,383.865 ms | 1,412.552 ms | +2.07% | 43,932 us | 38,214 us | -13.02% |

Both CPU gates pass. Exact corpus execution remains forbidden until this
implementation is committed as one revision-bound candidate and the
source-archive gate passes on that stable source tree.

## Frozen evaluation outcome

The exact candidate revision was
`a80f15f5c9b1c0e6748c194a595763fbf3173dea`. A clean detached worktree at
that revision passed the isolated deterministic package-archive gate 1/1.
Both generated binary packages were byte-identical at SHA-256
`67c14a041c2316782e49f10e068af9026f3be788c3401609a983e490d2d7cdea`;
both source packages were byte-identical at SHA-256
`461eb22016ed9aa6efdde484b5172316fdc43489a7a435ef2b9bdd2e61edfcdf`.

Each open-development run completed all 40 tracks with no execution failure.
Relative to the exact multiband-only baseline:

| Corpus | Metric | Baseline | I4 | Fixes | Breaks | Net |
|---|---|---:|---:|---:|---:|---:|
| ASAP development | meter | 19 | 16 | 1 | 4 | -3 |
| ASAP development | downbeat | 3 | 3 | 2 | 2 | 0 |
| ASAP development | period <=1% | 1 | 1 | 0 | 0 | 0 |
| ASAP development | period <=10% | 3 | 6 | 3 | 0 | +3 |
| Ballroom development | meter | 21 | 20 | 1 | 2 | -1 |
| Ballroom development | downbeat | 6 | 6 | 3 | 3 | 0 |
| Ballroom development | period <=1% | 8 | 12 | 5 | 1 | +4 |
| Ballroom development | period <=10% | 17 | 21 | 4 | 0 | +4 |

The causal fusion improves the period candidate but regresses meter on both
independent open-development partitions. That is negative external transfer
under the frozen gate, so I4 is rejected before the spent DJ corpus. The spent
DJ corpus and all ASAP, Ballroom and DJ formal holdouts remain unopened for
this iteration.

Privacy-safe report SHA-256 values are:

- ASAP baseline report: `f1ff378cb9f03d0bff071d33fa099b34bbdb8e4476e41d683ec9116fcdb64a60`;
- ASAP I4 report: `f4fcf6b83188dc56e99d5270e31b72f399d53442307a244213c98ba23eddc86f`;
- Ballroom baseline report: `abc9f8d79acadaa0221c5529ed65380be34d6781ee98505ae5f5d330b48967d2`;
- Ballroom I4 report: `32e19c4b036fa5637c7c51bb7373c6aafbf40c9b900db6716bef2f70ab72bf54`.

This is development evidence only and makes no acceptance claim.
