# Stage S4 — Tempo and local grid status

**Stage status:** Complete self-tested implementation candidate  
**Architecture source:** `docs/architecture/APTA-ARCHITECTURE-DRAFT.md`, Stage S4  
**Verified implementation merge:** `8fe19cfda514151880d658520912722db7edb99a`  
**Primary verification:** GitHub Actions PR CI run `#224`  
**Registered runtime tests:** 53  
**Formal Core Profile claim:** not issued

## 1. Stage scope

The architecture roadmap defines Stage S4 as:

- BPM model;
- candidate model;
- confidence;
- local beatgrid;
- locked ranges;
- provisional-to-stable lifecycle.

All six functional items are implemented in the reference `libapta` library and covered by runtime tests.

Stage S4 does not include global beatgrid refinement, multi-segment dynamic tempo or explicit full-track beat arrays. Those remain Stage S6.

## 2. Reference processing model

The reference implementation derives Stage S4 data from the same normalized PCM stream used by waveform processing.

```text
normalized PCM
  ├── waveform overview/detail
  └── 256-frame onset-energy bins
          ↓
      positive energy flux
          ↓
      normalized autocorrelation
          ↓
      tempo candidates
          ↓
      phase search
          ↓
      one local constant-period grid segment
```

The library does not perform codec, filesystem, USB or playback I/O. Push and pull PCM modes converge on the same copied PCM queue and therefore use the same Stage S4 processing path.

## 3. BPM model

The public BPM value uses integer millibeats per minute through `apta_tempo_millibpm_t`.

The reference search range is:

```text
40000 .. 300000 millibpm
```

The implementation searches integer onset-bin lags using normalized autocorrelation. The selected value contains:

- tempo in millibpm;
- evidence range;
- applicability range;
- independent confidence;
- lifecycle state;
- ambiguity flags;
- candidate-set revision identifier.

The algorithm is a deterministic reference backend, not a requirement that third-party implementations use identical DSP.

## 4. Candidate model

Each result may expose up to three ordered candidates.

Each candidate contains:

- tempo in millibpm;
- normalized score;
- confidence;
- relation to the selected value;
- flags.

Candidates are ordered by non-increasing score. Half-time and double-time relationships use the public APTA tempo-relation identifiers. Strong related candidates set the selected value's ambiguity flags.

## 5. Confidence

Tempo and grid confidence are exposed independently from lifecycle state.

The reference confidence calculation combines:

- normalized autocorrelation strength;
- separation from the next candidate;
- amount of contiguous evidence.

Silence and insufficient rhythmic evidence do not produce fabricated low-confidence BPM or grid values. The feature remains unavailable until usable evidence exists.

## 6. Local beatgrid

Stage S4 publishes one local segment using `APTA_GRID_REPRESENTATION_SEGMENTS`.

The segment contains:

- applicability range;
- fractional source-frame anchor;
- beat ordinal;
- fractional frames-per-beat period;
- nominal tempo;
- beat count;
- confidence;
- lifecycle state;
- stable segment identity and revision.

The enclosing grid view separately exposes:

- requested range;
- evidence range;
- applicability range;
- explicit coverage range.

A focus movement may publish a new applicability generation without new PCM input. Previously acquired results remain immutable and retain their original ranges.

## 7. Locked ranges

The public function:

```c
apta_session_lock_grid_range(session, range);
```

accepts a non-empty range contained inside a currently stable or final local grid.

A successful lock preserves:

- anchor;
- period;
- ordinal;
- segment identity;
- locked applicability range.

The result exposes `APTA_GRID_FLAG_LOCKED` in both the grid and segment flags. A conflicting second lock is rejected. Lock publication is transactional and rolls back when immutable result publication cannot complete.

## 8. Progressive lifecycle

The verified lifecycle is:

```text
ABSENT
  ↓ sufficient contiguous evidence
PROVISIONAL
  ↓ larger evidence window and confidence threshold
STABLE
  ↓ completed source/session
FINAL
```

A stable or final result never silently becomes less stable. Locked grid data remains fixed while additional PCM is accepted.

The golden lifecycle test directly observes all three published Stage S4 states:

```text
PROVISIONAL → STABLE → FINAL
```

## 9. Memory and bounded execution

Stage S4 uses a fixed-capacity onset ring:

- 256 source frames per bin;
- 4096 resident bins;
- bounded candidate count;
- one local segment in Stage S4.

Heap sessions use context-owned immutable snapshot arrays.

Known-duration bounded sessions reserve Stage S4 storage inside the existing two-slot immutable result pool:

- three tempo candidates per slot;
- one local-grid coverage range per slot;
- one local-grid segment per slot.

After successful bounded session creation, Stage S4 PCM processing and result publication do not invoke the context allocator. Acquired tempo/grid results may outlive the session and caller workspace.

No resource-class claim is made until total workspace, pool, stack and latency measurements are published for a declared workload.

## 10. `.apta` interchange

Container version 1 now defines two optional singleton sections:

- `TEMP` version 1 — selected tempo and ordered candidates;
- `LGRD` version 1 — one local constant-period segment and coverage range.

Both sections use:

- fixed-width little-endian fields;
- zeroed reserved bytes;
- CRC32C;
- explicit ranges and lifecycle state;
- pointer-independent layout.

`LGRD` requires `TEMP`, and its nominal tempo must match the selected `TEMP` value.

The writer preserves byte-identical historical output when Stage S4 data is absent. A Stage S4 writer→reader→writer round trip is byte-identical.

## 11. Verification evidence

The 53-test suite includes dedicated Stage S4 coverage for:

- 125 BPM golden click track;
- explicit provisional, stable and final lifecycle;
- ten integer-lag tempo vectors from approximately 40 to 250 BPM at 48 kHz;
- an additional 44.1 kHz vector;
- silence and insufficient evidence;
- candidate ordering and confidence;
- local-grid geometry and beat count;
- locked ranges;
- focus-driven applicability revision and old-result immutability;
- push and pull PCM parity;
- static workspace and bounded two-slot result publication;
- zero allocator callbacks after successful bounded create;
- result lifetime after session destruction and workspace overwrite;
- canonical `TEMP`/`LGRD` serialization;
- byte-identical round trip;
- malformed reserved fields, duplicate sections and cross-section conflicts;
- exhaustive prefix truncation;
- parser allocation-failure sweep;
- canonical `valid-s4.apta` fuzz seed.

CI run `#224` completed successfully with:

- GCC and Clang builds;
- C11 and C++11 public-header checks;
- all 53 runtime tests;
- AddressSanitizer;
- UndefinedBehaviorSanitizer;
- five canonical parser seeds;
- bounded libFuzzer smoke.

## 12. Conformance position

The project may accurately state:

> `libapta` 0.1.0 contains a self-tested reference implementation candidate for architecture Stage S4: BPM values, ordered tempo candidates, independent confidence, one local constant-period beatgrid, stable range locking and progressive `PROVISIONAL → STABLE → FINAL` publication.

The project does not yet claim the complete APTA Core Profile because stable API/ABI, independent implementation, broader legally redistributable audio fixtures, measured resource evidence and remaining formal conformance work are incomplete.

## 13. Next architecture stage

The next roadmap stage is:

```text
Stage S5 — Reference desktop tools
```

Its scope is:

- POSIX source adapter;
- decoder integration;
- `apta-analyze`;
- `apta-inspect`;
- `apta-validate`.

## 14. Performance: precomputed onset flux

`apta_internal_s4_refresh()` previously recomputed the onset flux inside the
autocorrelation lag loop. `flux[index]` depends only on the bin index and the
evidence start, both invariant across that loop, so every value was recomputed
once per lag — two bin lookups and two divisions each.

The flux is now computed once per refresh into `apta_session_t.onset_flux`, a
`float` array of `APTA_INTERNAL_ONSET_BIN_CAPACITY` entries allocated through
`apta_internal_session_allocate()` alongside `onset_bins`, so it comes from the
static workspace when one is configured. The lag loop and the phase search both
read that array. `apta_s4_flux_uncached()` remains the single definition of the
flux computation and is called only by the fill loop.

The array is indexed linearly as `flux[bin_index - evidence_first]` rather than
through the `onset_bins` ring mapping. `apta_s4_find_evidence()` returns a
contiguous run bounded by the bin capacity, so linear offsets always fit. This
matters for cost: with a runtime capacity, `bin_index % capacity` compiles to a
hardware integer division, and two of those per inner iteration cost more than
the correlation arithmetic they feed. Measured on an x86-64 host, the ring form
gave 2.1x and the linear form 13.7x over the same baseline.

Cost of `apta_session_process()` for a 5-minute 44.1 kHz track in 1024-frame
blocks, 12,921 calls, microseconds per call:

| Requested features | Before | After |
|---|---:|---:|
| `WAVEFORM_OVERVIEW` | 155.6 | 146.5 |
| `+ CONFIDENCE` | 13,393.9 | 979.2 |
| `+ BPM` | 13,416.9 | 977.9 |
| `+ LOCAL_BEATGRID` | 13,179.6 | 1,016.9 |
| `+ GLOBAL_BEATGRID` | 14,331.1 | 1,205.8 |
| `+ DYNAMIC_TEMPO` | 14,204.0 | 1,158.9 |
| `+ WAVEFORM_DETAIL + GRID_LOCKING` | 14,217.0 | 1,162.6 |

The remaining per-call cost is the lag loop itself, which still performs on the
order of 885,000 iterations per refresh. Reducing it further is a matter of
running the refresh less often rather than making one refresh cheaper.

The static workspace grew by 16 KiB for S4. The three published ESP-IDF memory
profiles still fit and still report two allocator calls each.

Selected tempo values are unchanged: `apta.s4.tempo_vectors` passes without
modification, so no vector's winning lag moved.

## 15. Performance: refresh gated on evidence growth

`apta_internal_s4_refresh()` ran the full autocorrelation on every
`apta_session_process()` call. None of its early exits was a "nothing changed"
check, so a 5-minute track at 1024 frames per call produced 12,921 estimates
where a few hundred carry the same information: one call advances the evidence
range by at most four 256-frame bins.

The estimate now re-runs only when the evidence range has grown by
`APTA_INTERNAL_S4_REFRESH_MIN_NEW_BINS` (default 32, about 186 ms at 44.1 kHz)
since the last one. The constant is `#ifndef`-guarded and can be overridden by
the host build.

Only the two expensive loops are gated. The lag loop and the phase search are
skipped and their result reloaded from `s4_cached_scores`, `s4_cached_lags` and
`s4_cached_phase`; candidate construction, confidence, the requested and
applicability ranges, the state decision and the mutation serial are recomputed
on every call as before. This distinction is load-bearing: the applicability
range is derived from the current focus inside the refresh, so gating the whole
function stops focus movement from republishing. `apta.s4.focus` fails if the
gate is placed before the range computation rather than around the loops.

Three cases bypass the gate:

- `end_of_input_signalled`, and a session state of `APTA_SESSION_COMPLETED`.
  `APTA_FEATURE_FINAL` is reachable only from a pass that sees the full evidence
  range with the session already completed, so gating the last estimate would
  strand the state at `APTA_FEATURE_STABLE`.
- An evidence range that moved backwards, which resets the tracker.

A grid lock needs no exemption: the `local_grid_locked` branch returns before
the gate is reached.

`apta.s4.refresh_gate` covers this. It pushes PCM in 512-frame increments so
most calls take the gated path, and asserts that the final state is
`APTA_FEATURE_FINAL` and that published generations still appear. At the default
gate the draining bypasses are not strictly required for this input; rebuilding
with `-DAPTA_INTERNAL_S4_REFRESH_MIN_NEW_BINS=1000000u` makes them load-bearing,
and in that configuration the `APTA_FEATURE_FINAL` assertion passes with the
bypasses and fails without them.

Cost of `apta_session_process()`, microseconds per call, continuing the table in
section 14:

| Requested features | Before A1 | After A1 | After A2 |
|---|---:|---:|---:|
| `WAVEFORM_OVERVIEW` | 155.6 | 146.5 | 155.4 |
| `+ CONFIDENCE` | 13,393.9 | 979.2 | 376.3 |
| `+ BPM` | 13,416.9 | 977.9 | 368.5 |
| `+ LOCAL_BEATGRID` | 13,179.6 | 1,016.9 | 371.2 |
| `+ GLOBAL_BEATGRID` | 14,331.1 | 1,205.8 | 421.9 |
| `+ DYNAMIC_TEMPO` | 14,204.0 | 1,158.9 | 418.6 |
| `+ WAVEFORM_DETAIL + GRID_LOCKING` | 14,217.0 | 1,162.6 | 465.6 |

The dominant remaining ungated cost is `apta_s4_find_evidence()`, which runs on
every call ahead of the gate -- it has to, because the gate needs the evidence
range to decide -- and scans the whole bin capacity in two passes. Reducing it
would require tracking the completed-bin high-water mark incrementally as bins
fill, rather than rediscovering it by scan.

## 16. Types: double removed from per-sample and inner-loop paths

The target for the first hardware integration is RV32IMAFC: single-precision FPU,
no `D` extension, so every `double` operation is a software-emulated library
call. The tempo path used `double` per source sample and throughout the
autocorrelation.

Changed:

- `apta_internal_onset_bin_t.sum_absolute` is now `uint32_t`, a sum of sample
  magnitudes scaled by `APTA_INTERNAL_SAMPLE_MAGNITUDE_SCALE` (2^15). S6 bins
  hold the most samples, 2048, bounding the sum at 2^26 -- a 64x margin inside
  `uint32_t`. The struct is shared by S4 and S6, so this is one change covering
  both engines.
- `apta_s4_energy()` and `apta_s4_flux_uncached()` return `float` and divide out
  the count and the scale at read time. Since A1 precomputes the flux array,
  this division runs once per bin per refresh rather than once per bin per lag.
- The autocorrelation accumulators, `best_scores[]`, the phase score, the
  candidate score ratio and the confidence separation are `float`; `sqrt`
  became `sqrtf`.
- The degenerate-energy guard moved from `1e-18` to `1e-12f`. The accumulators
  hold sums of squares of normalized flux, so `1e-12f` is still far below any
  real signal while remaining meaningful in single precision.

Selected tempo values are unchanged: `apta.s4.tempo_vectors` passes without
modification, so quantizing the per-sample magnitude to 15 bits did not move any
winning lag.

### 16.1 Cost on the host

The work order motivates this task by target cost, and the host measurement runs
the other way. Microseconds per call, continuing the table in section 15:

| Requested features | After A2 | After A3 |
|---|---:|---:|
| `WAVEFORM_OVERVIEW` | 155.4 | 152.5 |
| `+ CONFIDENCE` | 376.3 | 446.7 |
| `+ BPM` | 368.5 | 448.6 |
| `+ LOCAL_BEATGRID` | 371.2 | 447.5 |
| `+ GLOBAL_BEATGRID` | 421.9 | 583.9 |
| `+ DYNAMIC_TEMPO` | 418.6 | 520.4 |
| `+ WAVEFORM_DETAIL + GRID_LOCKING` | 465.6 | 538.7 |

This is expected. On x86-64 both `float` and `double` are native, so replacing
one with the other buys nothing and the added integer scaling and conversions
cost about 20 percent. The benefit is confined to targets where `double` is
emulated, which this host cannot represent.

Two caveats on these figures. Run-to-run variance on the S6 rows is around 15
percent: an earlier run of the same binary reported 500.9 for
`+ GLOBAL_BEATGRID` against 583.9 here. And the work order's overall target of
every row under 500 microseconds, which A2 met, is no longer cleanly met -- the
S6 rows straddle the line within measurement noise.

The static library grew from 131,695 to 134,005 bytes of text on x86-64. That is
also the wrong measurement for this change: the acceptance criterion concerns the
ESP-IDF component-size report, where removing `double` should drop the
soft-float support routines rather than add code. No RISC-V toolchain was
available here, so that criterion is unverified locally and left to
`.github/workflows/espidf.yml`.
