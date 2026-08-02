# Phase 6 — Multiband onset status

Phase 6 implemented and measured the B3 multiband onset front end on real
audio. The S4 candidate is evidence-positive, bounded, and available behind a
compile-time option. It is not the production default. The phase did not tune
on the 48-track partition, and it did not change the established confidence or
tempo-selection rules.

## 1. Decision

`APTA_ENABLE_EXPERIMENTAL_MULTIBAND_ONSET=ON` enables the frozen S4 candidate.
The normal build remains the existing broadband algorithm. The ESP-IDF port
exposes the same boundary as `CONFIG_APTA_EXPERIMENTAL_MULTIBAND_ONSET`, default
off.

The candidate is retained for further independent validation because:

- development S4 improved from 107/140 to 111/140, with four fixes and no
  breaks;
- development endorsed S4 improved from 124/140 to 126/140, with two fixes and
  no breaks;
- frozen holdout S4 remained 36/48, with no fixes and no breaks;
- frozen holdout endorsed S4 improved from 42/48 to 43/48, with one fix and no
  breaks;
- the full result is therefore 147/188 for S4 and 169/188 endorsed, versus
  143/188 and 166/188 at baseline;
- the 48-track partition was already observed during phase 5, so it is useful
  as a no-regression gate but no longer constitutes pristine independent
  evidence;
- the existing confidence-75 failures remain: three S4 errors and two endorsed
  errors over the full corpus, including the same metrical failures.

The initial attempt also applied B3 to S6. It moved development accuracy from
123/140 to 122/140 and left holdout accuracy at 41/48. That path was rejected.
The final experimental build keeps S6's 32-bit broadband accumulator and
novelty calculation unchanged.

## 2. Contained algorithm change

The portable C1 filterbank is now a shared internal implementation: two
sample-rate-derived one-pole low-pass filters at 200 Hz and 2 kHz produce low,
mid and high bands. Overview and onset own separate filter state because they
advance at different pipeline points. A discontinuity in accepted onset PCM
resets only the onset state, so a seek or gap cannot inherit unrelated filter
history.

For S4 with B3 enabled, each 256-frame onset bin accumulates:

- three unsigned band-magnitude sums;
- one broadband-magnitude sum used as an anchor;
- the sample count and ring identity.

Each magnitude is quantized at 255 units per full-scale sample. Static
assertions prove that a complete 256-frame sum fits in `uint16_t`. The whole bin
still occupies exactly 16 bytes. This avoids the earlier prototype's roughly
164 KiB workspace increase across the local and global ring capacities.

The novelty value consumed by the existing A1 flux array is:

```text
broadband half-wave rise
  + 0.25 * sum(equal-weight per-band half-wave rises)
```

The band contribution is admitted only when the aggregate band envelope also
rises. Without this coherent-envelope gate, filter-tail energy moving from high
to mid to low produced a false half-tempo result in the sub-bin regression
test. Per-band mean normalization is deliberately absent: the old normalized
prototype fell from 8/40 to 2/40 exact results on its synthetic evaluation.

Output shape, autocorrelation, candidate selection, tempo prior, grid fitting,
confidence and serialization are unchanged.

## 3. Memory boundary

The experimental 16-byte bin uses a union:

- S4 views eight accumulator bytes as three `uint16_t` band sums plus one
  `uint16_t` broadband sum;
- S6 views the same storage as its existing `uint32_t sum_absolute` and retains
  the production scale of 32768;
- the experimental sample count is `uint16_t`, statically proven sufficient for
  both the 256-frame S4 bin and 2048-frame S6 bin.

The default-off build retains the original private structure layout. Neither
mode changes a public ABI, file format, result shape, or configured ring
capacity.

## 4. Development protocol and ablation

Phase 6 used the phase-5 partition files without changing them:

| Partition | Tracks | SHA-256 |
|---|---:|---|
| development | 140 | `ae10089b7545cae20a24110b325e59137db3796cc2cdc905ba6931a47eacf33e` |
| holdout | 48 | `1fdd36e12e9d3cd480c2bea0255ad24ade7287f0406782464e3939bc95880ec8` |

Only development results were read while the algorithm was changing. The
candidate was frozen before the one holdout pass. No source audio, title,
artist, Rekordbox path or per-track result is committed.

A required `multiband_mix=0` ablation kept the experimental storage and
quantization but removed the band-flux contribution. Across all 140 development
tracks it was bit-identical to production: zero selection changes, zero
confidence changes, and the same 107 exact results. The measured gain therefore
comes from the new band evidence rather than quantization or layout.

Candidate CSV hashes in the ignored local corpus directory are:

| Run | SHA-256 |
|---|---|
| development S4 | `0e68471fc4eec560b69f1551a7aec935d6afd3cd9a74f53e169069752ce6dbb2` |
| development endorsed | `49a5e55d0014adf651b46f3a430300cf6458844d964a4439d64a31ff74e393f9` |
| development mix-zero ablation | `00ebc8a986b776cf2312afeabd6d859190e5370f48b3c56767e963619c8aac4c` |
| holdout S4 | `b707520100d82ca471afa5ef01da73ff0c1eefe4a0ac4403259a51ede4d7f39d` |
| holdout endorsed | `1cb9566207377a354682bf5ba7f5f8507196a964d53aaa1412686c5a4143e21e` |

Hash casing is immaterial. These files remain untracked because they include
local WAV paths and per-track outcomes.

## 5. Accuracy result

Accuracy means the selected tempo is within one percent of the Rekordbox tempo.

| Partition / mode | Baseline | B3 | Delta | Selection changes | Fixes | Breaks |
|---|---:|---:|---:|---:|---:|---:|
| development S4 | 107/140 | 111/140 | +4 | 12 | 4 | 0 |
| development endorsed | 124/140 | 126/140 | +2 | 14 | 2 | 0 |
| holdout S4 | 36/48 | 36/48 | 0 | 4 | 0 | 0 |
| holdout endorsed | 42/48 | 43/48 | +1 | 4 | 1 | 0 |
| full S4 | 143/188 | 147/188 | +4 | 16 | 4 | 0 |
| full endorsed | 166/188 | 169/188 | +3 | 18 | 3 | 0 |

Precision within 0.1 percent also improved: S4 moved from 112/188 to 116/188,
and endorsed S4 from 123/188 to 128/188.

On development, the S4 error classes moved from 2 octave plus 31 other errors
to 2 octave plus 27 other errors. On holdout, one error moved from the octave
class to the other class without becoming correct. High-confidence error counts
did not improve.

## 6. Tests and performance

Both configurations were built in Release mode with GCC 13.3 and warnings as
errors:

- default broadband: 82/82 CTest tests passed;
- experimental S4 B3: 83/83 passed, including
  `apta.experimental.multiband_onset`;
- core-only default configuration: 70/70 passed;
- default ASan+UBSan: 82/82 passed;
- experimental ASan+UBSan: 83/83 passed;
- `apta.s4.tempo_vectors`, `apta.s4.tempo_subbin`, waveform determinism,
  workspace, bounded and S6 tests pass in both modes.

Five alternating 30-second internal profile runs measured the median `+BPM` row
at 112.995 microseconds per process call for broadband and 136.613 microseconds
for the frozen B3 candidate. The individual medians came from baseline runs
between 111.186 and 117.807 microseconds and B3 runs between 135.516 and 141.089
microseconds. The median increase is 20.9 percent, far below the work order's
roughly 2x ceiling.

## 7. Reproduction

Representative commands are:

```sh
cmake -S . -B build-phase6-multiband -DCMAKE_BUILD_TYPE=Release \
  -DAPTA_WARNINGS_AS_ERRORS=ON \
  -DAPTA_ENABLE_EXPERIMENTAL_MULTIBAND_ONSET=ON
cmake --build build-phase6-multiband -j
ctest --test-dir build-phase6-multiband --output-on-failure

build-phase6-multiband/tools/apta-tempo-corpus \
  --tracks build/phase4-rekordbox/tracks-development.txt \
  --results-csv build/phase4-rekordbox/phase6-development-s4.csv
build-phase6-multiband/tools/apta-tempo-corpus \
  --tracks build/phase4-rekordbox/tracks-development.txt \
  --request-global \
  --results-csv build/phase4-rekordbox/phase6-development-endorsed.csv
```

The holdout commands are intentionally omitted from the tuning workflow. They
were run once only after the candidate and pass/fail boundary were frozen.

## 8. Remaining boundary

Before making B3 a default production behavior, use a genuinely new independent
library and repeat the no-break result. The most important unresolved criterion
is still calibrated confidence: phase 6 improves ordinary accuracy and
precision but does not remove confidently wrong metrical grids. Target-side
ESP32-P4 measurement is also required before changing the ESP-IDF default.
