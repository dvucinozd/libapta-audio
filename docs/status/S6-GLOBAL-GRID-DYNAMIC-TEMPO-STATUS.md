# Stage S6 — Global grid and dynamic tempo status

**Stage status:** Complete self-tested implementation candidate  
**Architecture source:** `docs/architecture/APTA-ARCHITECTURE-DRAFT.md`, Stage S6  
**Core implementation merge:** `501e42a06bbb912980f8b35c06488befa2fd2a86`  
**Container implementation merge:** `9d80f680469a7bec4e914c061e306f880bfc3f36`  
**Primary verification:** GitHub Actions PR CI runs `#266` and `#275`  
**Registered runtime tests:** 68  
**Formal Core Profile claim:** not issued

## 1. Stage scope

The architecture roadmap defines Stage S6 as:

- global refinement;
- segment representation;
- explicit beat representation;
- revision model.

All four functional items are implemented in the portable reference library and covered by runtime, sanitizer, serialization, truncation, allocation-failure and fuzz-smoke evidence.

## 2. Reference processing model

Stage S6 consumes the same normalized PCM stream used by waveform and Stage S4 processing. It does not introduce another decoder, channel-conversion path or copied PCM queue.

```text
normalized PCM
  ├── waveform processing
  ├── Stage S4 local tempo/grid
  └── 2048-frame global onset-energy bins
          ↓
      positive energy flux
          ↓
      bounded window tempo/phase estimates
          ↓
      adjacent-window consolidation
          ↓
      global grid segments
          ↓
      optional explicit beats
          ↓
      immutable revision publication
```

The algorithm is an informative deterministic reference backend. Third-party implementations may use different DSP while preserving the public data model, lifecycle and container contracts.

## 3. Bounded global evidence

The reference implementation uses fixed limits:

- 2048 source frames per global onset bin;
- 16384 resident global bins;
- 128 bins per refinement window;
- 64 bins minimum contiguous evidence;
- 256 bins for stable-state eligibility;
- at most 8 global grid segments;
- at most 4096 explicit beats.

At 48 kHz, the resident global-bin capacity covers approximately 699 seconds of source audio. The implementation degrades explicitly when bounded representation limits prevent complete detail; it does not allocate an unbounded beat array.

## 4. Global grid model

The result exposes `APTA_FEATURE_GLOBAL_BEATGRID` through the existing `apta_grid_view_t` data model.

A global grid contains:

- requested range;
- evidence range;
- applicability range;
- explicit coverage range;
- lifecycle state and independent confidence;
- one to eight ordered non-overlapping segments;
- optional explicit beats;
- grid flags, including dynamic-tempo and degraded-state indicators.

Each segment contains:

- applicability range;
- fractional source-frame anchor;
- beat ordinal;
- fractional frames-per-beat period;
- nominal tempo in millibpm;
- beat count;
- confidence and lifecycle state;
- segment identity;
- revision identity;
- flags.

## 5. Dynamic tempo and explicit beats

`APTA_FEATURE_DYNAMIC_TEMPO` requires `APTA_FEATURE_GLOBAL_BEATGRID`, which in turn requires BPM and waveform overview processing.

The reference implementation publishes:

- `APTA_GRID_REPRESENTATION_SEGMENTS` for a global constant-period result when explicit beats are not requested or required;
- `APTA_GRID_REPRESENTATION_HYBRID` when dynamic-tempo support is requested or multiple tempo regions require explicit beat positions.

Explicit beats are ordered by source-frame position and strictly increasing ordinal. Every beat and segment carries the active revision identifier.

## 6. Progressive revisions

Stage S6 introduces the public revision view in `include/apta/apta_s6.h`:

```c
apta_result_get_grid_revision(result, &revision);
apta_session_apply_grid_revision(session, revision_id);
```

The revision view exposes:

- current and previous revision identifiers;
- `PENDING` or `APPLIED` state;
- confidence;
- affected range;
- proposed representation;
- proposed segment and beat counts;
- conflict, dynamic-tempo and degraded flags.

A revision identifier changes only when global grid geometry changes. Lifecycle-only changes do not fabricate a new geometry revision.

## 7. Locked local ranges

Stage S4 local locking remains authoritative until the host explicitly accepts a conflicting Stage S6 refinement.

When a global refinement conflicts with a locked local range:

1. the existing locked local grid remains unchanged;
2. the immutable result publishes a `PENDING` revision;
3. the revision identifies the affected range and proposed global geometry;
4. the host may call `apta_session_apply_grid_revision` with the exact revision identifier;
5. successful application publishes a new immutable generation with `APPLIED` revision state.

A stale or unavailable revision identifier is rejected. Previously acquired results remain unchanged.

## 8. Immutable lifecycle

Global grid publication follows the existing result-generation contract:

```text
ABSENT
  ↓ sufficient global evidence
PROVISIONAL
  ↓ evidence and confidence threshold
STABLE
  ↓ completed source/session
FINAL
```

Results own their copied segment, beat and revision data. A result may outlive its session. Acquiring a later generation never mutates an earlier generation.

## 9. Memory and bounded execution

Heap sessions use context-owned immutable S6 result arrays.

Known-duration sessions using `APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS` reserve, per immutable result slot:

- one S6 result extension;
- one global coverage range;
- eight segment records;
- 4096 beat records.

The session-side global bins, state and beat staging array may reside in the caller static workspace. After successful bounded session creation, the verified S6 processing/publication path performs no context allocator calls. Acquired pooled results may outlive the session and caller workspace.

No resource-class certification is claimed until complete workspace, pool, stack and latency measurements are published for a declared workload and target.

## 10. `.apta` interchange

Stage S6 defines two optional singleton version-1 sections:

- `GGRD` — global ranges, representation, segments and explicit beats;
- `REVN` — revision identity, state, affected range and proposed geometry.

`GGRD` and `REVN` are emitted together, with `REVN` immediately following `GGRD` in canonical directory order. Existing `TEMP` and `LGRD` version-1 output remains byte-compatible.

See [`../reference/APTA-S6-CONTAINER-0.1.md`](../reference/APTA-S6-CONTAINER-0.1.md).

## 11. Verification evidence

The 68-test suite includes dedicated Stage S6 coverage for:

- constant global grid;
- abrupt 12-bin to 9-bin tempo transition;
- multiple ordered global segments;
- hybrid representation and explicit beats;
- immutable retained mid-analysis generation;
- locked-range pending revision and explicit apply;
- static workspace plus bounded two-slot publication;
- zero allocator callbacks after successful bounded create;
- result lifetime after session destruction and workspace overwrite;
- canonical `GGRD`/`REVN` serialization;
- byte-identical writer → reader → writer round trip;
- strict section pairing and order;
- version, CRC, reserved-field, count, range and revision consistency validation;
- rejection of every truncated prefix and a trailing byte;
- parser allocation-limit and every-allocation failure sweeps;
- canonical `valid-s6.apta` libFuzzer seed.

CI run `#266` verified the core implementation with 66 runtime tests. CI run `#275` verified the complete S6 container package with:

- GCC and Clang builds;
- C11 and C++11 public-header checks;
- all 68 runtime tests;
- AddressSanitizer;
- UndefinedBehaviorSanitizer;
- canonical S6 seed generation;
- bounded 2000-run libFuzzer smoke.

## 12. Conformance position

The project may accurately state:

> `libapta` 0.1.0 contains a self-tested reference implementation candidate for architecture Stage S6: bounded global beatgrid refinement, multiple tempo/grid segments, optional explicit beats, dynamic-tempo representation, immutable progressive revisions and canonical `GGRD`/`REVN` interchange.

The project does not yet claim APTA 1.0 stability or certified Core Profile conformance. Remaining gates include stable API/ABI and specification approval, independent implementation interoperability, broader legal audio vectors, measured target resource evidence, maintained long-running fuzz evidence and multi-platform validation.

## 13. Next architecture stage

The next roadmap stage is:

```text
Stage S7 — ESP-IDF port
```

Its scope includes:

- ESP allocator and clock integration;
- optional ESP-DSP backend;
- cooperative scheduler example;
- embedded memory profiles;
- target build and runtime evidence.

## 14. Performance: precomputed onset flux

`apta_internal_s6_refresh()` previously recomputed the onset flux inside the
autocorrelation lag loop of `apta_s6_estimate_window()`. Because S6 analyses the
evidence range as a sequence of windows, that recomputation ran once per lag
*per window*.

The flux is now computed once per refresh into
`apta_internal_s6_session_state_t.global_flux`, a `float` array of
`APTA_INTERNAL_GLOBAL_BIN_CAPACITY` entries allocated through
`apta_internal_session_allocate()` alongside `global_bins`. It is indexed
linearly as `flux[bin_index - flux_base_bin]`, where `flux_base_bin` records the
evidence start of the refresh that filled it.

One fill serves every window. Flux depends on the window start only at the
window's first bin, where the predecessor is treated as absent, so each window
needs a single boundary value patched before it is analysed rather than a fill
of its own. Windows are disjoint and contiguous, so a patched entry is read only
by the window that starts on it.

`apta_s6_flux_uncached()` remains the single definition of the flux computation
and is called only by the fill loop and the per-window boundary patch.

Measured on an x86-64 host for a 5-minute 44.1 kHz track in 1024-frame blocks,
the `+ GLOBAL_BEATGRID` row of `apta_session_process()` fell from 14,331.1 to
1,205.8 microseconds per call, and `+ DYNAMIC_TEMPO` from 14,204.0 to 1,158.9.

The static workspace grew by 64 KiB for S6. The three published ESP-IDF memory
profiles still fit and still report two allocator calls each.

`apta.s6.global_grid`, `apta.s6.revision` and `apta.s6.bounded` pass without
modification.
