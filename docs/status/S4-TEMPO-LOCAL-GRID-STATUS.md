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
