# APTA 1.1 WP1 transient-lattice experiment

- **Status:** rejected after development evaluation
- **Frozen baseline revision:** `4f06c21da573b9cb1451b6f8004db12f4364580e`
- **Implementation revision:** `2dc7b2c5272f394df0d37a1206538fe1003fc0e7`
- **Evidence class:** diagnostic/development only
- **Formal holdouts:** unopened

## Hypothesis

The existing one-bin positive rise loses transient shape and local contrast.
A bounded novelty signal combining immediate rise, four-bin and sixteen-bin
local floors, band balance and a one-step refractory peak should suppress
filter tails and sustained energy while retaining periodic low-, mid- and
high-band attacks. Better transient localization should expose correct S4
period and phase candidates more consistently without changing the public
result model or growing the dominant onset-bin allocation.

## Frozen comparison

The isolated baseline enables
`APTA_ENABLE_EXPERIMENTAL_MULTIBAND_ONSET=ON`. The candidate uses the same
source, toolchain and options plus
`APTA_ENABLE_EXPERIMENTAL_TRANSIENT_LATTICE=ON`. All other experimental DSP
options are disabled for the isolated comparison. A later integration check
may combine only a retained WP1 slice with other retained candidates.

The first implementation uses fixed four-bin and sixteen-bin trailing floors.
Its refractory term compares the current positive band rise with the previous
positive band rise. These constants and weights are frozen before corpus
observation; changing them starts a new named iteration and report.

## Evidence sources

1. Deterministic synthetic fixtures: impulses, sustained tones, syncopation,
   kick/snare alternation, off-beat hats, silence and onset-ring rollover.
2. The already-spent 60-track DJ development corpus for fixes/breaks and
   period/phase diagnostics.
3. Only the open 40-track development partitions of ASAP and Ballroom for
   transfer. Their formal holdouts remain unopened.

## Resource expectation

- `sizeof(apta_internal_onset_bin_t)` remains at or below 16 bytes;
- no new heap/workspace or result-pool allocation;
- one bounded 17-frame transient-history scratch object is used only during an
  S4 refresh;
- binary size and S4 refresh CPU may increase and must be measured;
- default builds and stable 1.0 behavior remain byte-for-byte unaffected.

## Retain gate

Retain the iteration only if it produces at least five net period/phase fixes
with at most one break on the spent DJ set and positive transfer on both open
ASAP and Ballroom development partitions. Confidence must not become more
optimistic on incorrect results.

## No-regression veto and stop condition

Reject immediately for any default-behavior change, operational failure,
onset-bin growth, new allocation, sanitizer failure, high-confidence safety
regression, more than one spent-set break, or negative transfer on either open
development partition. If the first frozen formula misses the retain gate, its
results remain diagnostic; do not open a holdout or tune a threshold in place.
Record the failure taxonomy before deciding whether a separately named WP1
iteration is justified.

## Pre-corpus implementation checkpoint

The opt-in slice and its production-ring synthetic fixture compile with GCC
13.3.0 and warnings-as-errors. The default release matrix passes 120/120 tests,
the isolated candidate release matrix passes 122/122, and the candidate
ASan/UBSan matrix passes 118/118. The clean archive gate remains the WP0 result;
the two local release matrices exclude only `apta.package.archives` to avoid
traversing ignored NTFS/WSL build trees.

The default `apta-analyze` SHA-256 remains exactly
`0e7999efb61734f656b846d5542617454c5a0789224531c071d0f8555512383a`,
matching WP0 byte-for-byte. The transient flag is rejected at configure time
unless the multiband-onset dependency is explicitly enabled.

Against the isolated multiband-only baseline, the transient candidate keeps
the full P4 30-minute workspace at 941,248 bytes, recommended workspace at
1,000,092 bytes and result pool at 537,104 bytes. Both `apta-analyze` binaries
are 221,320 bytes; `libapta.a` grows from 483,118 to 485,518 bytes (+2,400).
The bounded 17-frame history occupies 272 bytes of refresh-call stack and adds
no persistent allocation.

## Development results

All runs used implementation revision
`2dc7b2c5272f394df0d37a1206538fe1003fc0e7` and
`SOURCE_DATE_EPOCH=1767225600`. The spent 60-track comparison completed with
zero execution failures and produced the same report twice, SHA-256
`0ce53e5b075fc9a764531a24a21a2705b1cd9564aa93cfd12a97fb2c284c8bb8`.
It is explicitly marked `development`, `spent` and `acceptance_claim=false`.

| Spent-set family | Multiband baseline | Transient I1 | Fixes | Breaks | Net |
|---|---:|---:|---:|---:|---:|
| period only | 40/60 | 39/60 | 0 | 1 | -1 |
| downbeat | 6/60 | 6/60 | 1 | 1 | 0 |
| full beatgrid | 5/60 | 5/60 | 1 | 1 | 0 |
| meter | 58/60 | 57/60 | 0 | 1 | -1 |

Key remains unchanged at 20/60. High-confidence full-beatgrid errors improve
from 24 to 23, but this does not offset the failed correctness gate. Five
period signatures and eleven downbeat frames change, showing that the path is
active rather than accidentally equivalent to the baseline.

The open development partitions show some transferable transient evidence but
also two meter regressions:

| Corpus | Metric | Baseline | Transient I1 | Fixes | Breaks |
|---|---|---:|---:|---:|---:|
| ASAP | downbeat | 3/40 | 5/40 | 2 | 0 |
| ASAP | period <=1% | 1/40 | 1/40 | 0 | 0 |
| ASAP | meter | 19/40 | 18/40 | 0 | 1 |
| Ballroom | downbeat | 6/40 | 7/40 | 2 | 1 |
| Ballroom | period <=1% | 8/40 | 11/40 | 3 | 0 |
| Ballroom | meter | 21/40 | 20/40 | 0 | 1 |

The ASAP baseline/candidate report SHA-256 values are respectively
`f1ff378cb9f03d0bff071d33fa099b34bbdb8e4476e41d683ec9116fcdb64a60`
and `039fb4b5b203174912740ef4731e988d42ccbd1f78d9623d4cff617a64f44d34`.
The Ballroom values are
`abc9f8d79acadaa0221c5529ed65380be34d6781ee98505ae5f5d330b48967d2`
and `3c272640fa6be102103e2f55a8f6f82d865980165799fbb22f4991cb2f756b19`.

## CPU profile and decision

Three alternating 120-second synthetic cost-probe runs were measured for each
isolated build. Medians use identical evidence-bin and refresh-scan counts.

| Path | Total baseline | Total I1 | Total delta | S4 flux baseline | S4 flux I1 | Flux delta |
|---|---:|---:|---:|---:|---:|---:|
| BPM | 1,229.325 ms | 1,388.465 ms | +12.95% | 52,111 us | 193,818 us | +271.93% |
| full | 1,346.984 ms | 1,438.479 ms | +6.79% | 43,229 us | 157,762 us | +264.94% |

Iteration 1 is rejected: it misses the required five net spent-set fixes,
introduces period and meter breaks, and materially increases refresh cost.
It is not promoted, no holdout is opened and no acceptance claim is made. The
opt-in code remains reproducible diagnostic evidence. Any follow-up must be a
separately named WP1 iteration with a new hypothesis; threshold tuning of I1 is
not permitted.
