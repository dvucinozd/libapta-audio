# APTA 1.1 WP0 Development Harness

**Status:** in progress  
**Qualification date:** 2026-08-28  
**Analyzer source revision:** `551c1ea85cc782f5258d05882ec16f0c6d052495`

## Scope and evidence boundary

WP0 freezes a reproducible development baseline before WP1 changes onset,
beat-lattice, meter/downbeat or key behavior. It does not create or replace
release acceptance evidence. Formal holdout splits remain unopened.

The recovered 60-track DJ set is the already-spent development corpus. Its
locally regenerated labels SHA-256 is
`e7eac4ab8a80019b3da558c347d36242b827e485c7c66dff27c72fa8c25abbb8`,
which exactly matches the retained acceptance provenance. The local musician
verification CSV SHA-256 is
`0b51016bdffbc4da2e97babf7e1f87b22542be5384381d4066a396d68c1bf70a`,
also an exact provenance match. No private filenames or host paths are tracked.

## Frozen configurations

The default baseline has all experimental DSP options disabled. The current
candidate enables:

- `APTA_ENABLE_EXPERIMENTAL_MULTIBAND_ONSET=ON`;
- `APTA_ENABLE_EXPERIMENTAL_3BAND_DOWNBEAT=ON`;
- `APTA_ENABLE_EXPERIMENTAL_KEY_TUNING=ON`;
- `APTA_ENABLE_EXPERIMENTAL_HARMONIC_HPCP=ON`;
- `APTA_ENABLE_EXPERIMENTAL_KEY_TRACE=ON`;
- `APTA_ENABLE_EXPERIMENTAL_METER_TRACE=ON`.

Both release builds use GCC 13.3.0, CMake 3.28.3, `Release`, desktop adapters,
tools, tests and `APTA_WARNINGS_AS_ERRORS=ON`. The sanitizer build uses the
candidate flags, `Debug`, ASan/UBSan and warnings-as-errors.

## Build and test matrix

| Configuration | Result | Notes |
|---|---:|---|
| default Werror | 120/120 pass | source-archive test is run separately in a clean local clone |
| all-current-experiments Werror | 121/121 pass | source-archive test excluded; covered by the clean default archive run |
| all-current-experiments ASan/UBSan | 117/117 pass | only sanitizer-applicable tests are registered |
| clean source archive | 1/1 pass | clean local clone avoids ignored build-tree traversal on WSL/NTFS |

The DJ comparator is additionally covered by five deterministic Python unit
tests and is registered in CTest. It treats missing results as execution
failures, rejects malformed coverage, normalizes flags, reports exact resource
deltas and never makes an acceptance claim.

## Resource delta

The current all-experiments candidate changes the declared full 30-minute P4
profile and Linux/GCC build artifacts as follows:

| Metric | Default | Candidate | Delta |
|---|---:|---:|---:|
| minimum workspace | 941,216 B | 943,680 B | +2,464 B |
| recommended workspace | 1,000,058 B | 1,002,676 B | +2,618 B |
| result pool | 537,104 B | 537,104 B | 0 B |
| `apta-analyze` binary | 217,088 B | 221,592 B | +4,504 B |
| `libapta.a` archive | 480,126 B | 488,630 B | +8,504 B |

Binary and archive sizes are toolchain/platform observations, not portable ABI
or ESP32-P4 resource claims. Physical target evidence remains a later gate.

## Corpus runs

The default and current-candidate analyzers completed with zero execution
failures against the exact same 60-track spent DJ bytes and labels. The unified
comparison is deterministic: a second invocation with differently ordered flag
arguments produced the same report SHA-256,
`838cbb1022ba8cdf2c3fd141c3f6df0c7450924a74e6ccdfafe1b42d54fcbaab`.

| DJ family | Default | Candidate | Fixes | Breaks | Net | High-confidence errors |
|---|---:|---:|---:|---:|---:|---:|
| key | 20/60 | 22/60 | 2 | 0 | +2 | 0 -> 0 |
| meter | 58/60 | 58/60 | 0 | 0 | 0 | 0 -> 0 |
| downbeat | 5/60 | 8/60 | 4 | 1 | +3 | 0 -> 0 |
| beatgrid | 4/60 | 6/60 | 2 | 0 | +2 | 24 -> 23 |

The same two analyzers also completed the 40-track open development splits for
ASAP and Ballroom. The holdout splits were not run.

| Development corpus | Configuration | Meter | Downbeat | Period <=1% | Period <=10% |
|---|---|---:|---:|---:|---:|
| ASAP | default | 19/40 | 4/40 | 2/40 | 2/40 |
| ASAP | candidate | 19/40 | 3/40 | 1/40 | 3/40 |
| Ballroom | default | 21/40 | 6/40 | 10/40 | 17/40 |
| Ballroom | candidate | 21/40 | 7/40 | 8/40 | 17/40 |

Machine-readable run metadata, exported results and reports remain local under
ignored build directories. The all-current-experiments configuration is useful
diagnostically but fails the no-regression veto: ASAP downbeat and strict period
accuracy regress, and Ballroom strict period accuracy regresses. It is not a
promotion candidate for default behavior.

## WP0 exit

The paired DJ comparison and both open development-corpus runs are complete.
Independent repeat runs are the remaining WP0 check. Once their per-track
output hashes match, WP0 closes and WP1 starts from this frozen baseline with
the opt-in transient-lattice slice.
