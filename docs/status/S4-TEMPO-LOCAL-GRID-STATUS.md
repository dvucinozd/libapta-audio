# Stage S4 — Tempo and local grid status

**Stage status:** Complete self-tested implementation candidate  
**Architecture source:** `docs/architecture/APTA-ARCHITECTURE-DRAFT.md`, Stage S4  
**Verified implementation merge:** `8fe19cfda514151880d658520912722db7edb99a`  
**Primary verification:** GitHub Actions PR CI run `#224`  
**Registered runtime tests:** 53 at the verified merge above; 75 on the current branch (see sections 14 to 21)  
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

### 16.2 Component size on the real target

The acceptance criterion is that the ESP-IDF component-size report does not
grow. It does. Measured by CI against the same report on `main`, for the whole
branch A1 through C3:

| Target | `main` | branch | delta |
|---|---:|---:|---:|
| ESP-IDF 5.5.4 / ESP32 / scalar | 36,786 | 37,091 | +305 |
| ESP-IDF 6.0.2 / ESP32 / scalar | 36,311 | 36,670 | +359 |
| ESP-IDF 6.0.2 / ESP32-S3 / ESP-DSP | 36,358 | 36,700 | +342 |

About one percent. Two things are worth separating.

The branch is not A3 alone. A5 adds a public entry point and the workspace
requirement computation, which plausibly accounts for most of the increase.
Attributing the delta per task would need a CI run per commit, which was not
done, so no such attribution is claimed here.

More interesting is that the report gained a `libgcc.a` row, 187 bytes, where
`main` has none: the branch introduced 64-bit integer helpers on a 32-bit
target. One cause was A3's own: declaring the scaled magnitude `uint64_t` made
the squaring a full 64x64 multiply, a `__muldi3` call once per source sample --
exactly the class of cost this task exists to remove, swapped from soft-float
to soft-int. That is fixed; the operand is now `uint32_t` and the product a
widening 32x32 multiply, worth about 30 bytes and removing a per-sample helper
call.

`libgcc.a` remains at 187 bytes after that fix, so other 64-bit helpers are
still linked. The likely sources are the `uint64_t` to `double` conversion in
the rms quantizers, which runs once per column, and the `uint64_t` division by
a runtime divisor in A5's workspace query, which runs once per session. Both
are cold relative to the per-sample path, so the remaining cost is size rather
than throughput. This is inference from what the branch adds, not a symbol
listing: no cross toolchain was available to confirm it.

On x86-64 the static library text grew from 131,695 to 134,005 bytes, but that
is not the measurement the criterion is about, and the per-sample `__muldi3`
regression above was invisible there -- both operand widths compile to one
instruction on a 64-bit host. It was only found by running CI.

## 17. Activation: CONFIDENCE decoupled from the tempo engine

`APTA_INTERNAL_S4_FEATURES` included `APTA_FEATURE_CONFIDENCE`, and
`apta_s4_enabled()` tests against that mask. A host requesting
`WAVEFORM_OVERVIEW | CONFIDENCE` -- a reasonable request meaning "tell me how
much of the waveform you actually measured" -- therefore activated the full
autocorrelation tempo estimator. Nothing in the public headers signalled this:
`apta_types.h` documents `APTA_FEATURE_CONFIDENCE` as an independent bit, and
the result model already carries `apta_waveform_overview_view_t.confidence` and
`apta_tempo_value_t.confidence` as separate fields. Only the activation mask
conflated them.

`APTA_FEATURE_CONFIDENCE` is no longer in `APTA_INTERNAL_S4_FEATURES`. It is
treated as a modifier that qualifies whichever features a host actually
requested.

Consequences worked through:

- **Dependency validation.** `CONFIDENCE` was reachable by the
  `waveform_dependency` group only through `APTA_INTERNAL_S4_FEATURES`, so
  removing it there would have made `CONFIDENCE` alone a valid session. It is
  now named explicitly in that group in `apta_config.c` and
  `apta_session_detail_contract.c`: `CONFIDENCE` alone is still rejected,
  `WAVEFORM_OVERVIEW | CONFIDENCE` is accepted. `apta_session_mask_is_coherent()`
  additionally accepts `WAVEFORM_OVERVIEW` as something `CONFIDENCE` can
  qualify, alongside the tempo and grid features it already accepted.

- **Overview confidence.** This did not previously exist. Both snapshot paths
  hard-coded `result->overview.confidence = APTA_CONFIDENCE_UNKNOWN`, so the
  field was never meaningful, with or without S4. It now reports coverage
  completeness: complete columns over expected columns, scaled to
  `APTA_CONFIDENCE_MAX`, or `APTA_CONFIDENCE_UNKNOWN` while the track length is
  unknown or when the host did not request confidence. Note that there are two
  snapshot paths, `apta_waveform_snapshot.c` and `apta_result_pool_snapshot.c`,
  each carrying its own copy of the overview logic; bounded sessions go through
  the second.

- **Accessors.** `apta_result_get_feature_state()` for `APTA_FEATURE_CONFIDENCE`
  is answered from the tempo estimate only when `APTA_FEATURE_BPM` is available;
  otherwise it falls through the chain so the waveform layer answers. Without
  this the S4 accessor would have reported a zeroed tempo state for sessions
  that never ran S4.

- **Capabilities.** `apta_context_get_capabilities()` still advertises
  `APTA_FEATURE_CONFIDENCE`; that path was not touched.

### 17.1 Effect

| Requested features | Before A1 | After A3 | After A4 |
|---|---:|---:|---:|
| `WAVEFORM_OVERVIEW` | 155.6 | 152.5 | 157.9 |
| `+ CONFIDENCE` | 13,393.9 | 446.7 | 156.7 |

`overview+confidence` is now within noise of `overview`, which is the check this
task exists to satisfy. Measured against the original baseline that row is 85x
cheaper.

The other rows are unaffected, as expected: a host that asks for `BPM` still
gets the estimator.

### 17.2 Behaviour change for existing hosts

This is observable. Any host that relied on `APTA_FEATURE_CONFIDENCE` to switch
on tempo analysis will no longer get a tempo. Such a host was depending on
undocumented activation coupling, and the fix is to request `APTA_FEATURE_BPM`,
but the change is not source-compatible in behaviour and is recorded here for
that reason.

`apta.waveform.confidence_without_tempo` covers the new contract: a session
requesting `WAVEFORM_OVERVIEW | CONFIDENCE` reports a meaningful overview
confidence that reaches both `apta_waveform_overview_view_t` and
`apta_result_get_feature_state()`, still advertises the capability, and reports
no tempo. Restoring `CONFIDENCE` to `APTA_INTERNAL_S4_FEATURES` makes it fail on
the tempo assertion.

## 18. Tempo accuracy: baseline measurement

B1 to B3 cannot be evaluated on impulse trains, so `tools/apta_tempo_corpus.c`
synthesizes a repeatable corpus and measures the estimator against it. Four
multi-layer drum patterns -- kick, snare, hat and a bassline -- at ten tempi
from 90 to 174 BPM, 30 s each, 40 tracks. The noise source is a seeded
xorshift, so the audio and the results are reproducible from the binary alone.
`--write-wav DIR` dumps the corpus for inspection.

```bash
cc -O2 -std=c11 -Iinclude tools/apta_tempo_corpus.c build/libapta.a -lm \
   -o build/apta-tempo-corpus
./build/apta-tempo-corpus --seconds 30
```

The corpus is synthetic: exact timing, no expressive dynamics, no production
processing. It is a floor, not a prediction of real-world accuracy. Every rate
below must be read with that attached.

### 18.1 Result at the current implementation

| Measure | Value |
|---|---:|
| Tracks | 40 |
| Exact | 8 (20.0%) |
| Octave-family errors | 25 (62.5%) |
| Other errors | 7 |
| No tempo reported | 0 |

Error breakdown: two-thirds 11, third 8, half 4, quarter 2, other 7.

| Confidence | n | mean | min | max |
|---|---:|---:|---:|---:|
| Correct | 8 | 81.1 | 77 | 84 |
| Incorrect | 32 | 70.3 | 58 | 88 |

With an actionable threshold of 70 -- the point at which a host would let Sync
or Quantize use the grid -- there are **14 high-confidence octave errors**. B1
requires that number to be zero.

### 18.2 What the numbers say

The estimator is not shippable for tempo or beatgrid in its current form, and
the confidence value is the reason rather than the accuracy rate.

Confidence does not separate right from wrong. The two distributions overlap
almost completely, and the single most confident answer in the corpus, 88, is
**wrong** -- higher than the best correct answer, 84. A host gating Sync on
confidence would be misled preferentially toward the errors.

The most common failure, two-thirds at 11 of 40, is a relation the public enum
defines but the implementation never produces, and the second most common,
third at 8 of 40, has no relation constant at all. So the dominant failure mode
is not merely unresolved, it is not reportable. That is B2.

The work order's own observation reproduces exactly: `offbeat_bass` at 128 BPM
is reported as 42.710 BPM, the same 42,710 millibpm the work order cites for
its impulse train, at confidence 80.

### 18.3 Status

This is the measurement B1 to B3 are to be judged against. It is recorded here
before any algorithmic change so the comparison is honest, and it is deliberately
not tuned: no threshold in the corpus was chosen to make the current
implementation look better or worse.

## 19. Tempo selection: preferred-tempo prior

Selection was the raw autocorrelation argmax. A log-normal prior centred at
125 BPM, width 0.55 in natural-log units, now weights each lag's score before
selection. Both, plus the ambiguity knee below, are `#ifndef`-guarded.

Every lag in range is scanned and weighted, so taking the maximum already
prefers the best member of an octave family. The explicit family scan --
correlations at 1/2, 2, 1/3, 3, 2/3, 3/2, 1/4 and 4 times the winning lag,
eight against roughly 224 -- exists to measure how close the runner-up sibling
came, which is what confidence needs.

### 19.1 Result on the accuracy corpus

Measured against the section 18 baseline, same 40 synthetic tracks:

| Measure | Baseline | With prior |
|---|---:|---:|
| Exact | 8 (20.0%) | **23 (57.5%)** |
| Octave-family errors | 25 (62.5%) | 17 (42.5%) |
| Other errors | 7 | **0** |
| Confidence, correct | 81.1 mean | 71.0 mean |
| Confidence, incorrect | 70.3 mean | 57.8 mean |

The corpus tool now prints a threshold sweep rather than assuming a gate. At
75, twelve correct answers survive and zero incorrect ones do; every lower gate
admits octave errors. **The actionable threshold is therefore 75**, and B1's
requirement of no high-confidence octave errors holds against it. At the
baseline no threshold separated the two populations at all.

### 19.2 Confidence calibration, and a wrong first attempt

Scaling confidence linearly by `1 - sibling/winner` produced zero
high-confidence errors and was still wrong: a clean four-to-the-floor track
always has a substantial half-tempo sibling, so confidence collapsed on
ordinary material, nothing in the corpus exceeded 63, and threshold gating
became useless. The scaling now has a knee at 0.85. A sibling below that
fraction of the winner does not reduce confidence at all; above it confidence
falls to zero. Ambiguous has to mean "nearly as good", not "present".

### 19.3 What the prior does not fix

174 BPM is still halved in two of the four patterns. The prior is not the
cause: 174 is marginally closer to the centre than 87, so the prior mildly
opposes the halving and the raw correlation overrides it. Those answers score
70 and 72, below the actionable threshold, so a host gating at 75 rejects them
rather than acting on them.

The dominant residual is `two-thirds`, 15 of 40 -- a relation the public enum
defines but the implementation never produces. That is B2.

## 20. Multi-band onset front end: measured, rejected

B3 proposes replacing the broadband onset envelope with a multi-band one, on
the stated theory that a broadband envelope "cannot distinguish a kick from a
snare from a harmonic onset" and that this is "the root cause of the octave
errors in B1".

It was implemented and measured rather than assumed. Per-band accumulators in
the shared onset bin struct, per-band half-wave rectified flux, weighted into
the single novelty value the autocorrelation consumes, reusing C1's filterbank
with its own state instance.

| Variant | Exact | Octave errors | Lowest usable threshold |
|---|---:|---:|---|
| Broadband baseline | 8 (20.0%) | 25 (62.5%) | none |
| Multi-band, per-band normalized | 2 (5.0%) | 35 (87.5%) | none |
| Multi-band, unnormalized | 8 (20.0%) | 27 (67.5%) | none |
| Prior only (B1) | **23 (57.5%)** | **17 (42.5%)** | **75** |
| Prior + multi-band | 22 (55.0%) | 18 (45.0%) | 80 |

The change was not adopted. On this corpus it does not improve accuracy alone,
and on top of the prior it is marginally worse while costing 164 KiB of
workspace, from the onset bin struct growing, and three times the per-sample
onset work.

Per-band normalization is actively harmful here, and mechanically so:
equalising band means amplifies the snare band, whose natural period in these
patterns is two beats, so the dominant error shifts to half-tempo -- 21 of 40.

The wider result is that the work order's premise appears inverted. Octave
errors are a selection problem, not a novelty-function problem: autocorrelation
of a periodic signal peaks at multiples and divisors however clean the novelty
curve is, and what chooses among those peaks is the prior. The prior alone
moved exact matches from 20% to 57.5%; the multi-band front end alone moved
them not at all.

This is recorded rather than implemented so the finding is not lost. Revisiting
it would be reasonable if the corpus were replaced with real audio, where
timbral separation may matter more than it does on programmed patterns.

## 21. Tempo relations completed and reported

`relation_to_selected` detected two cases, half and double. Three consequences
followed from that, all of which the accuracy corpus made concrete:

`APTA_TEMPO_RELATION_THREE_HALF` and `APTA_TEMPO_RELATION_TWO_THIRDS` were
defined in the public headers and never produced. There was no value at all for
the third or triple relation, so the failure the work order itself measured --
a 128 BPM signal reported as 42.7 -- was not merely unresolved, it was not
expressible.

Four values are added: `THIRD` 5, `TRIPLE` 6, `QUARTER` 7, `QUADRUPLE` 8. The
relation is a single byte in the `TEMP` section, so the set is append-only and
the existing values 0 to 4 are unchanged. The reader's validation bound moved
with it; a reader predating these values rejects a section carrying them, which
is the conservative behaviour.

The classifier now walks one table of exact ratios with a two percent
tolerance, ordered by distance from unity so the nearest match wins. That same
table drives B1's octave-family scan, which previously carried its own hard
coded list. The set of relations the estimator can detect and the set it
searches can no longer drift apart.

Ambiguity flagging was one flag per ratio, which does not scale to eight
relations and left the two dominant errors -- `two-thirds` at 15 of 40 and
`third` -- with no flag at all. `APTA_TEMPO_FLAG_OCTAVE_AMBIGUITY`, bit 7, is
set for any detected relation and for a strong family sibling that did not
survive into the three-entry candidate list. `HALF_TIME_AMBIGUITY` and
`DOUBLE_TIME_AMBIGUITY` still fire for their own relations, so existing hosts
are unaffected.

Accuracy is unchanged, which is correct: this task changes what is reported,
not what is selected. 23 of 40 exact, threshold 75, no high-confidence octave
errors.

### 21.1 Coverage gap

`apta.s4.relations` pins every relation's numeric value and the flag's bit
position, so a future renumbering fails a test rather than silently
reinterpreting existing files. That is the risk worth guarding.

What is not covered is a test driving the estimator to emit each of the eight
relations. `apta_s4_relation()` is static, and reaching each relation through
the public API needs a signal whose autocorrelation carries two strong peaks at
that exact ratio -- eight purpose-built signals. Those were not written. The
relations that occur in practice, `two-thirds`, `third` and `half`, are
exercised by the accuracy corpus, which reports the relation for every track.

## 22. Final cost measurement

Measured on the merged branch, x86-64 host, 5-minute 44.1 kHz track in
1024-frame blocks, 12,921 calls. Three runs of the same binary, so the spread
is host noise rather than a build difference:

| Requested features | Baseline | Final (3 runs) |
|---|---:|---|
| `WAVEFORM_OVERVIEW` | 155.6 | 157.3 |
| `+ CONFIDENCE` | 13,393.9 | 159.7 |
| `+ BPM` | 13,416.9 | 467.9 – 486.8 |
| `+ LOCAL_BEATGRID` | 13,179.6 | 473.6 – 481.8 |
| `+ GLOBAL_BEATGRID` | 14,331.1 | 536.3 – 548.3 |
| `+ DYNAMIC_TEMPO` | 14,204.0 | 538.0 – 555.0 |
| `+ WAVEFORM_DETAIL + GRID_LOCKING` | 14,217.0 | 577.5 – 592.2 |

Run-to-run spread is 2 to 4 percent. An earlier note in section 16.1 described
S6 variance as around 15 percent; that was wrong. The two figures it compared,
500.9 and 583.9, came from different builds rather than repeated runs of one
binary, and repeated runs are much tighter than that.

`overview + CONFIDENCE` is within noise of `overview`, which was the check A4
existed to satisfy: that row is 84x cheaper than at baseline.

### 22.1 The under-500-microsecond target is not met, and should not be chased

> **Superseded.** All seven rows now measure under 500 microseconds. Section
> 16.6 of the S6 status document has the current figures and the cause: sharing
> the correlation loop between the two estimators replaced a 64-bit bin index
> with a 32-bit slice offset in the innermost loop, and the `+ BPM` row fell
> from about 500 to about 395. The reasoning below is kept because its argument
> -- that the host figure is a proxy and the target has never been measured --
> still holds, and is the reason the target was not chased at the time.

Three rows exceed 500 microseconds per call. The work order's section 17.4 lists
that as a success condition, and it is the one condition of seven that this work
does not satisfy.

Where it went, measured against the A4 snapshot:

| Row | After A4 | Final | Delta |
|---|---:|---:|---:|
| `+ BPM` | 457.6 | ~479 | +4.7% |
| `+ GLOBAL_BEATGRID` | 499.5 | ~543 | +8.7% |
| `+ WAVEFORM_DETAIL + GRID_LOCKING` | 533.8 | ~587 | +10% |

The S6 rows were already at or above 500 after A3. The further 5 to 10 percent
is B1's octave-family scan, eight extra correlations against roughly 224 per
estimate, plus B2's ratio-table relation classifier.

Removing the family scan entirely would recover about 3.4 percent, against a
gap of roughly 8 percent, so it does not reach the target on its own. The only
change that would is undoing A3 -- and A3 exists precisely because the target is
RV32IMAFC, where every `double` operation is a software-emulated call. Trading
target cost for host cost to satisfy a host-measured threshold would be
optimising the wrong machine.

The threshold's purpose was to prove headroom against a budget of about 2 ms
per 20 ms tick on an ESP32-P4. That budget has never been measured on the
actual target, and the host figure is a proxy that A3 deliberately made
pessimistic. Closing this properly means measuring on hardware, not tuning
against x86.

## 23. The actionable threshold does not survive realistic timing

Section 19 derived an actionable confidence threshold of 75 from the synthetic
corpus and reported that B1's requirement -- no high-confidence octave errors
-- holds against it. That conclusion depended on the corpus having perfectly
quantized timing, which was not stated at the time and is not true of real
material.

The corpus tool now models three imperfections, individually selectable so
their effects can be separated: per-hit timing jitter, velocity variation with
a downbeat accent, and a slow sinusoidal tempo drift whose mean stays at
nominal. Defaults are modest rather than adversarial -- 6 ms of jitter is a
tight human performer, or any recording that was not grid-quantized.

Forty tracks, 30 s each:

| Variant | Exact | Octave | Other | Max confidence, correct | Lowest usable gate |
|---|---:|---:|---:|---:|---:|
| Exact timing | 23 | 17 | 0 | 90 | 75 |
| Jitter only | 21 | 15 | 4 | **64** | 60 |
| Dynamics only | 21 | 19 | 0 | 89 | 75 |
| Drift only | 25 | 15 | 0 | 82 | 70 |
| Swing only | 23 | 17 | 0 | 90 | 75 |
| All together | 18 | 15 | 7 | **63** | 60 |

"Lowest usable gate" is the smallest threshold at which no incorrect answer
survives while at least one correct one does.

### 23.1 What this changes

Accuracy barely moves. Exact matches go from 23 to 21 under jitter, which is
within a couple of tracks of noise at this sample size.

Confidence collapses. The highest confidence attached to a *correct* answer
falls from 90 to 64, so a host gating at 75 admits nothing at all -- not one
track of forty. The threshold recorded in section 19 is an artefact of the
corpus, not a property of the estimator.

Timing jitter is the sole cause. Dynamics and swing leave the confidence
distribution untouched; drift lowers it modestly. Jitter alone also produces
every one of the errors that are not simple ratios, which the exact-timing
corpus never showed.

The mechanism is plausibly direct rather than subtle. An onset bin is 256
frames, 5.8 ms at 44.1 kHz, and the jitter standard deviation is 6 ms -- about
one bin. Onsets smear across adjacent bins, the novelty peaks flatten, and the
normalized autocorrelation score drops. Confidence is `35 + score * 50`, so a
lower correlation lowers confidence directly. This is a hypothesis consistent
with the measurement, not something the measurement proves.

### 23.2 Where that leaves the shippability question

Section 17.4 item 6 of the work order asks whether tempo and beatgrid are
shippable, and says a negative answer is a valid outcome. The honest answer is
now: not on this evidence. A host cannot gate Sync or Quantize on confidence
and obtain useful coverage on material that was not grid-quantized, because
confidence never reaches a level at which the errors have been excluded.

Two directions follow, and they are different problems:

- Recalibrate confidence against realistic material rather than against
  correlation strength alone. The estimator is not much less accurate under
  jitter; it only reports itself as less certain.
- Make the novelty function tolerant of sub-bin timing error, for example by
  distributing an onset across adjacent bins rather than assigning it to one.
  That attacks the cause rather than the symptom.

Neither is attempted here. This section records the measurement that motivates
them.

### 23.3 What this measurement does not cover

The swing knob shows no effect, but that is a defect in the experiment rather
than a result: the four patterns place almost nothing on odd sixteenths, so the
knob barely engages. Swing is untested, not shown harmless.

There is still no production processing -- no compression, no limiting, no
reverb, no bass-heavy masters -- and no real recordings. These rates remain a
floor.

## 24. Confidence cannot be fixed by reweighting

Section 23 showed that confidence collapses under realistic timing while
accuracy barely moves, and pointed at two directions: recalibrate confidence,
or make the novelty tolerant of sub-bin jitter. The first was investigated and
abandoned on measurement.

The formula is:

```text
confidence = 35 + score * 50 + separation * 15 + (stable ? 5 : 0)
confidence *= (1 - family_ambiguity)
```

The argument for recalibrating was that `score`, the absolute normalized
autocorrelation, measures how metronomic a recording is rather than how likely
the answer is correct, while `separation` and `family_ambiguity` are
scale-invariant and should survive a uniformly weaker correlation. Shifting
weight from the first to the second looked principled.

It does not work, because `separation` carries no signal. Measured over 60
tracks with jitter, taking the candidate scores from the public result:

| Population | n | separation min / median / max | mean confidence |
|---|---:|---|---:|
| Correct | 21 | 0.00 / 0.08 / 0.19 | 54.2 |
| Incorrect | 39 | 0.00 / 0.04 / 0.23 | 48.4 |

The two distributions overlap almost entirely, and no threshold on separation
splits them: at 0.1 it admits 10 correct and 11 incorrect, at 0.2 it admits
none correct and two incorrect. The term's whole range is 0.23 wide, because
with three candidates and 500-millibpm duplicate suppression the runner-up is
always close in score. There is no room in it to carry information.

### 24.1 What that implies

The aggregate is not much better than the part. Mean confidence is 54.2 for
correct answers and 48.4 for incorrect, with heavily overlapping ranges. Six
points of separation between the populations is not something a host can gate
on.

That reframes the problem. It is not that the three terms are weighted wrongly;
it is that none of them knows whether the answer is right. No reweighting of
`score`, `separation` and `family_ambiguity` will produce a usable gate,
because the information is not in them.

A usable confidence needs evidence the estimator does not currently gather.
Two candidates, neither attempted here:

- Measure how well the derived grid actually fits the onsets it claims to
  explain -- residual between predicted beat positions and observed onset
  peaks. That is a direct measure of "is this grid right", which none of the
  present terms is.
- Compare independent estimates. S6 already analyses the track in windows and
  S4 estimates locally; agreement between them is evidence, disagreement is
  doubt, and neither engine currently looks at the other.

### 24.2 Corpus changes behind these numbers

Two patterns were added, `sixteenth_hats` and `ghost_funk`, carrying
sixteenth-note hats and ghost snares on odd sixteenths. They exist because the
original four placed almost nothing on odd sixteenths, which made the swing
knob a no-op and left it untested.

With them the corpus is 60 tracks and swing is genuinely exercised: it changes
exact matches from 23 to 24 and leaves the confidence distribution untouched.
Swing is now tested and harmless, which section 23.3 could not claim.

The new patterns are hard. They contribute close to zero exact matches and
fifteen octave errors, almost all doubling: dense uniform subdivision gives the
autocorrelation a strong peak at the eighth-note level, and B1's prior barely
discriminates there -- at 90 BPM the prior weights the true tempo 0.84 and the
doubled answer 0.80, so the raw correlation decides. Narrowing the prior would
help this material and hurt drum and bass at 174. That tension is recorded
rather than tuned away.

Corpus totals in sections 18 to 23 were measured on the original four patterns
and are not comparable with the six-pattern figures here.

## 25. Grid fit decided on real recordings

Section 24 named two candidates for a confidence that knows whether the answer
is right. The first, grid fit, was implemented and measured on the synthetic
corpus, where the result was mixed: the term carried real signal, unlike
`separation`, but on humanized material no threshold was clean, and the change
was left on a branch rather than merged.

That verdict was wrong, and the corpus is why.

### 25.1 The corpus and the material disagreed

The humanized corpus applies 6 ms of per-onset jitter, which models a human
drummer. Most material a beatgridding library is pointed at is sequenced: house,
techno, trance, anything produced in a DAW. For that material the *exact*
corpus was the closer model all along, and the humanized one was measuring a
population the library rarely sees.

This was not visible from inside the corpus. It took real recordings.

### 25.2 Ground truth from a DJ library

68 tracks were read off a Rekordbox USB export. Rekordbox stores its analysis
per track in `PIONEER/USBANLZ/**/ANLZ0000.DAT`; the `PQTZ` section is the beat
grid and carries tempo in hundredths of a BPM, and `PPTH` carries the track
path. The modal grid tempo is the value the software displays and the value the
user mixes to, so it is ground truth in the only sense that matters here.

Each track was decoded to a 90-second window starting at 0:30, 44.1 kHz stereo,
into a temporary directory. No audio enters the repository (section 10.4).

Both builds were run over the same decoded files: `main` with the correlation
score, the branch with grid fit. The library differs; the corpus tool does not.

### 25.3 Result

Selection is identical, as it must be -- confidence scores an answer, it does
not choose one. Both builds report 42 of 68 exact (61.8%), one octave-family
error, 25 other errors.

The confidence distributions are not identical:

| Build | correct mean | incorrect mean | gap | incorrect max | correct min |
|---|---:|---:|---:|---:|---:|
| `main` correlation score | 70.6 | 61.5 | 9.1 | 79 | 37 |
| branch grid fit | 75.1 | 44.7 | 30.4 | 66 | 48 |

The last two columns decide it. On `main` the worst wrong answer scores 79 and
the weakest right answer scores 37, so the populations interleave across the
whole usable range and no threshold separates them. With grid fit the worst
wrong answer scores 66 and the weakest right answer 48; the overlap is 18 points
wide instead of 42, and above 66 the wrong population is empty.

Precision and recall of the admitted set, correct meaning within 1% of the
Rekordbox grid:

| Gate | `main` precision | recall | grid fit precision | recall |
|---:|---:|---:|---:|---:|
| 60 | 69.1% | 90.5% | 97.4% | 88.1% |
| 65 | 73.3% | 78.6% | 97.2% | 83.3% |
| 70 | 85.2% | 54.8% | 100% | 69.0% |
| 75 | 82.4% | 33.3% | 100% | 45.2% |
| 80 | 100% | 16.7% | 100% | 33.3% |

Grid fit is better on both axes at every gate, which is the rare shape of
result that needs no trade-off argument. At the documented actionable threshold
of 75 it admits 19 tracks with no errors, where the correlation score admits 14
with three errors. `main` reaches perfect precision only at 80, and only by
admitting 7 of 42.

B1's requirement -- zero high-confidence octave errors -- holds for grid fit at
every gate from 50 to 95. For the correlation score it fails at 70 and 75.

Merged on this evidence.

### 25.4 What the misses actually are

The 26 wrong answers are worth separating from the confidence question, because
they are not the failure mode sections 18 to 24 assumed.

Only three sit at a metrical ratio. The other 23 land between 0.95 and 1.09 of
the true tempo -- 124 read as 121.60, 125 as 127.60, 128 as 130.84. These are
near misses, not octave confusion, and they are clustered on a small set of
reported values because the lag search is integer bins and a bin is 5.8 ms. At
128 BPM one bin is 1.5 BPM, so adjacent bins are 130.84, 128.00 and 125.97 with
nothing in between.

Integer-bin quantization alone would cap the error at half a bin, about 0.6%.
The observed errors are 2% to 8%, one to six bins wide, so the search is picking
the wrong bin rather than merely rounding. Quantization is a floor under the
accuracy, not the explanation for it.

Two follow-ups this suggests, neither attempted:

- Interpolate the correlation peak instead of taking the best integer bin.
  That removes the floor and costs a few operations per candidate.
- Look at why the peak lands one to six bins off on this material. The near
  misses cluster around 124-128 BPM, which is where this library sits, so the
  sample is not neutral and the question needs a broader set of tracks.

61.8% exact is the honest number for S4 on real sequenced music, and it is the
next thing worth improving. The confidence term now reports that limitation
rather than hiding it: with grid fit the 26 misses average 44.7 and none exceeds
66, so a host gating at 70 gets 29 answers and all 29 are right.

## 26. Sub-bin period refinement

Section 25.4 read the misses as an accuracy problem. Measuring what the tempo
error costs shows the sharper problem is the answers counted as *correct*.

### 26.1 What a correct answer was worth

The published local grid is one anchor plus a constant period, so any tempo
error accumulates without bound. A grid running at E BPM against music at T BPM
slips `|E - T|` beats every minute.

Of the 29 tracks that were both within 1% of the Rekordbox grid and above
confidence 70 -- the answers a host acts on -- 20 slipped half a beat inside
three minutes, shorter than the tracks themselves. Median time to half a beat
was 2.5 minutes.

That is not a search failure. The lag scan is an integer argmax over 256-frame
bins, and near 128 BPM consecutive bins are 1.6 BPM apart: 126.05, 127.60,
129.20, with nothing between them. A track at exactly 128.00 can only be
gridded at 127.60, which slips half a beat in 1.3 minutes and a whole bar in 5.
S6 is worse, and section 27 covers it separately.

### 26.2 Interpolating the peak, measured and rejected

The obvious fix is a parabola through the winning lag and its two neighbours,
putting the vertex between bins. It was implemented and measured first, and it
made things worse: median tempo error over the same 29 tracks went from 0.154%
to 0.184%, 16 tracks worse against 9 better.

The reason is that the correlation peak is as narrow as the onsets that produce
it, a few bins at most. Three samples straddling a peak that sharp describe
local asymmetry rather than a parabola, and the vertex they imply is noise. The
method assumes a smooth peak the signal does not have.

### 26.3 Measuring across beats instead

Resolution does not have to come from the shape of one peak. Correlating at a
lag of N beats puts the peak near `N * lag`, and dividing the integer argmax
found there by N divides the error by N as well. The same integer search buys N
times the precision.

`APTA_INTERNAL_TEMPO_REFINE_MAX_BEATS` is 16, four bars in common time, reduced
automatically when the evidence is too short to shift that far and keep half of
itself. The search window is +/- N/2 bins around `N * lag`, which is exactly the
half-bin either side of the integer lag, so it cannot walk onto a neighbouring
beat and change the answer's octave.

Measured over the same 68 tracks:

| | before | after |
|---|---:|---:|
| median tempo error | 0.154% | **0.013%** |
| median minutes to half a beat | 2.5 | 31.3 |
| slipping half a beat inside 3 min | 20 of 29 | 5 of 29 |
| exact (within 1%) | 42 of 68 | 43 of 68 |

22 tracks improved, 5 worsened, 2 unchanged. Tracks that read 127.60 now read
128.00, and 124.53 now reads 125.00 -- matching the Rekordbox grid to two
decimals.

The five that worsened are all still within 1%; the largest moves from 126.05 to
126.34 against a truth of 126.00. Those are candidates for genuine tempo
variation that a 16-beat measurement resolves differently from a whole-track
average, and they are not separated here.

Exact matches barely move because the 1% tolerance was already wide enough to
absorb a whole bin. That is the point: the tolerance was hiding the error this
section is about.

### 26.4 Cost

Up to 17 correlations per candidate against several hundred in the scan.
Measured with the section 22 harness, three runs each:

| Row | HEAD | With refinement |
|---|---:|---:|
| `+ BPM` | 476.8 - 500.5 | 493.7 - 496.6 |
| `+ LOCAL_BEATGRID` | 477.5 - 480.7 | 494.9 - 498.2 |

About 3.5% on the local-grid row, which is the tightest of the two and the one
where the change is visible above the 2 to 4 percent host noise. End to end over
68 real tracks the corpus tool went from 78.1 to 84.1 seconds.

Both rows were already above the 500-microsecond figure discussed in section
22.1, and this does not change that argument.

### 26.5 Regression checks

The synthetic corpus is unchanged at 23 of 60 exact. The humanized corpus
improves from 18 to 20. `apta.s4.tempo_subbin` is a new test: a click train
beating every 20,608 frames, exactly 80.5 bins, so the true period sits halfway
between lag 80 and lag 81. It fails on HEAD reporting 127,604 millibpm, and
passes with the refinement. 77/77 tests pass, including under ASan and UBSan.

## 27. The synthetic corpus was measuring its own annotation

Section 25.1 established that the humanized corpus models a population this
library rarely sees. The corpus has a second and larger problem, which the real
recordings exposed by contrast: it reported 38.3% exact where real music
reported 63.2%, and that gap was read as an estimator weakness.

It was not. It was the mix of material.

### 27.1 A pattern can contradict its own annotation

Judging a pattern by whether the estimator gets it right is circular, so the
tool now measures the pattern instead. `pattern_beat_margin()` takes the cyclic
autocorrelation of the onset envelope across the bar and compares the annotated
beat, four sixteenth steps, against the best rival period that is not a whole
number of beats. Whole-beat rivals are excluded because those are the octave
family, which B1's prior exists to resolve; what remains are odd rivals such as
a dotted period at six steps, which no prior addresses.

A negative margin means the pattern does not single out its own annotation.

| Pattern | Beat margin | Population | Exact |
|---|---:|---|---:|
| `four_on_floor` | +0.41 | representative | 9 of 10 |
| `offbeat_bass` | +0.37 | representative | 9 of 10 |
| `sixteenth_hats` | -0.04 | ambiguous | 0 of 10 |
| `ghost_funk` | -0.06 | ambiguous | 0 of 10 |
| `breakbeat` | -0.13 | ambiguous | 5 of 10 |
| `halftime` | -0.34 | ambiguous | 0 of 10 |

The margin predicts the outcome almost exactly, and it is computed from the
pattern definition without reference to any result. `halftime` is the clearest
case: its annotated beat correlates at 0.22 while the six-step period correlates
at 0.56. Its own description calls it "the classic half/double trap". Counting
the estimator's answer there as an error measures the annotation.

### 27.2 What the corpus actually says

Four of six patterns are ambiguous by construction, so two thirds of the corpus
is adversarial. Splitting the populations:

| Population | Tracks | Exact | Octave errors |
|---|---:|---:|---:|
| Representative | 20 | **18 (90%)** | 2 |
| Ambiguous by construction | 40 | 5 (12%) | 30 |
| Blended, as previously reported | 60 | 23 (38%) | 32 |

90% on representative synthetic material sits sensibly beside 63% on real
recordings, which are harder than anything rendered from a step grid. The 38%
figure was an artifact of the population mix and never described the estimator.

Humanized, the representative population gives 16 of 20; S6 gives the same 18 of
20 as S4.

### 27.3 What changed in the tool

The corpus prints the per-pattern margin and its population before any result,
states in its own header that it is a stress test rather than a model of real
music, and points at `--tracks` for an accuracy figure. The summary reports the
two populations separately, with only the representative line labelled as
accuracy. The blended total is still printed, marked as such, because sections
18 to 26 quote it and those numbers should remain locatable.

The classification is derived, not assigned. A pattern added later is measured
and placed automatically, and one edited into ambiguity moves without anyone
remembering to reclassify it.

### 27.4 What this does not fix

Every figure in sections 18 to 26 was measured on the blended population. They
are not restated here, because most were comparisons between two builds over the
same corpus, and a comparison survives a biased population as long as both sides
share it. The absolute rates in those sections do not, and should be read as
describing the corpus rather than the estimator.

The ambiguous patterns are kept. They exercise the octave machinery, and B1's
requirement of zero high-confidence octave errors is meaningful only against
material that offers octave errors to make. What changes is that their rate is
no longer averaged into a number presented as accuracy.

## 28. The global estimator endorses a local candidate

Section 16.8 of the S6 status document rejected both obvious ways of combining
the engines, and closed by noting that S6 is right on 18 tracks where S4 is
badly wrong with no rule able to say which those are. This is a third way,
found by asking what is actually wrong on each of the 25 misses rather than by
proposing a combination and testing it.

### 28.1 Two faults, not one

The corpus tool gained `--candidates`, which dumps every candidate with its
score instead of only the winner. That separates faults the selected tempo
cannot distinguish:

| Fault | Count | What it means |
|---|---:|---|
| Proposed and lost | 11 | The right answer was in the list and was outscored |
| Never proposed | 14 | The search did not offer it at all |

The eleven are strikingly consistent. The correct answer sits at slot 1 or 2
with a score of 53,470 to 64,553 against the winner's 65,535 -- 82 to 98 percent
-- and confidence is low, 40 to 66. The estimator is not confidently wrong; it
is uncertain, and the runner-up is right.

Eight of the 18 tracks S6 gets right are in this group. Those need no new
search, only a reason to prefer a candidate already published.

### 28.2 Promotion, and why it is this weak

S6 supplies that reason. When its nominal tempo lands within one percent of a
candidate that already scored at least 55,000, that candidate moves to the head
of the list.

The rule never computes a tempo. That is deliberate: the combiner rejected in
section 16.8 rescaled S4's winner by a metrical ratio toward S6, which could
produce a value neither engine had proposed -- 240.02 against a truth of
120.00 -- and broke four tracks to fix one. Promotion can only ever choose a
worse member of a list S4 already published, and `apta.s4.endorsement` pins
that: the candidate set is identical with and without the global grid
requested, only its order changes.

S6 runs after S4 within a process call, so S4 reads the previous generation's
value. Analysis is progressive and S6 settles long before the end of a track,
so the published result converges on a current endorsement.

The tempo handed over is the longest segment's, not the first. A track that
opens with an intro at a different feel would otherwise be represented by its
least typical part.

### 28.3 Measured

Over the same 68 Rekordbox-annotated tracks, with both features requested:

| | S4 alone | With endorsement |
|---|---:|---:|
| within 1% | 43 | **50** |
| within 0.1% | 33 | **40** |
| median error among correct | 0.013% | 0.013% |
| median minutes to half a beat | 31.2 | 31.2 |
| precision at confidence 70 | 29/29 | 29/29 |

Seven fixed, none broken. The precision figures are unchanged because promotion
selects an S4 candidate, which carries S4's sub-bin refinement; what improves is
which candidate.

It fires on 8 of 68 tracks and is correct on all 8. One of those was already
correct and stayed so.

Synthetic material is unaffected: the representative population holds at 18 of
20, and the ambiguous one moves from 5 to 6.

### 28.4 The thresholds were chosen on this corpus

One percent and 55,000 were selected by sweeping the same 68 tracks the result
is reported on, which is fitting to the test set and worth naming.

What makes it defensible rather than fitted is the shape of the surface. Every
tolerance from 0.3 to 2 percent and every score threshold from 0 to 58,000
improves on the baseline, over a range of 45 to 49 within 1%. A threshold of
55,000 or above produces zero regressions at every tolerance tested. The gain
sits on a plateau, not a peak.

A second annotated library would still be the thing that confirms it, and none
was available.

### 28.5 What this does not reach

The 14 tracks where the right answer was never proposed are untouched, and ten
of the eighteen S6 gets right are among them. Reaching those means changing the
search, not the selection.

The seven newly-correct tracks all sit at confidence 40 to 48, below the
actionable threshold of 75 and below the gate of 70 where precision is
currently perfect. They are right and not trusted. Raising confidence on an
endorsement is arguable -- independent agreement is evidence -- but section 16.8
measured cross-engine agreement as a gate and found it worse than the shipped
confidence, so it is not done here on a sample of eight.
