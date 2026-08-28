# APTA 1.1 WP1 transient-lattice experiment

- **Status:** implementation verified, pre-corpus
- **Frozen baseline revision:** `4f06c21da573b9cb1451b6f8004db12f4364580e`
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
