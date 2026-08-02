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

## 15. Performance: refresh gated on evidence growth

`apta_internal_s6_refresh()` ran the per-window autocorrelation on every
`apta_session_process()` call. It now re-runs only when the evidence range has
grown by `APTA_INTERNAL_S6_REFRESH_MIN_NEW_BINS` (default 32) since the last
one. Global bins hold 2048 frames, so 32 bins is about 1.5 s at 44.1 kHz and a
1024-frame process call advances the range by at most half a bin. The constant
is `#ifndef`-guarded.

As in S4, `end_of_input_signalled` and a session state of
`APTA_SESSION_COMPLETED` bypass the gate so the grid can reach its final state,
and an evidence range that moved backwards resets the tracker.

Unlike S4 the whole refresh is gated rather than only its inner loops. The S6
requested and applicability ranges are derived from the evidence range, not from
the session focus, so skipping the refresh cannot strand focus-driven
republication the way it does in S4.

The `+ GLOBAL_BEATGRID` row of `apta_session_process()` fell from 1,205.8 to
421.9 microseconds per call, and `+ DYNAMIC_TEMPO` from 1,158.9 to 418.6.
Measured against the pre-A1 baseline the two rows are 34.0x and 33.9x cheaper.

`apta.s6.global_grid`, `apta.s6.revision` and `apta.s6.bounded` pass without
modification.

## 16. S6 measured on real audio, and what that found

Every S6 figure before this section came from synthetic material. That was not a
choice: no shipped tool could request the feature. `apta-analyze --features all`
stopped at the local grid, no token existed for the global grid or dynamic
tempo, and the only reference to either was in a cost probe that is not in the
build. S6 had never run over a real recording.

### 16.1 Making it reachable

`--features` gains `global` and `dynamic`, and `all` now means all.
`apta-inspect` prints a `GGRD` section, including the nominal tempo, which it
did not report for any grid before. The accuracy corpus gains `--global`, which
requests S6 and judges its nominal tempo in place of S4's, so every figure the
harness produces then describes S6.

### 16.2 First measurement

68 tracks from a Rekordbox USB export, the same ground truth section 25 of the
S4 status document describes.

| | exact of 68 | octave-family errors | high-confidence octave errors |
|---|---:|---:|---:|
| As found | 8 (11.8%) | 40 (58.8%) | 30 |
| S4, for comparison | 43 (63.2%) | 1 | 0 |

S6 reported 19 distinct tempi for 68 tracks. Of the 60 wrong answers, 25 landed
on half the true tempo and 11 on a third; values like 41.68 and 44.55 BPM were
common. No threshold separated right from wrong anywhere in the range, and B1's
requirement of zero high-confidence octave errors failed by 30.

Two independent causes, and the measurement separates them cleanly.

### 16.3 The prior S6 never got

B1 gave S4 a log-normal preferred-tempo prior because autocorrelation peaks at
every multiple of the true period, so a bare argmax is free to pick any member
of the octave family. S6 does its own independent scan and never received it.

Adding it, with the same centre and width:

| | exact | octave-family errors | high-confidence octave errors |
|---|---:|---:|---:|
| Before | 8 | 40 | 30 |
| With the prior | 17 | 2 | **0** |

The gate that decides whether a window has enough evidence still measures the
raw correlation rather than the weighted score. Weighting it would reject
correct answers at the edges of the prior for having an unfashionable tempo.

Other errors rose from 20 to 49 in the same step. That is the expected shape:
the prior fixed which octave S6 picks, leaving the errors that come from not
being able to express the answer at all.

### 16.4 The resolution S6 cannot express

A global bin is 2048 frames, 46 ms. Near 128 BPM consecutive integer lags are 13
BPM apart, and the entire 110-150 BPM range contains three reachable values:
117.45, 129.20 and 143.55. The worst-case error from the grid alone is 6.8%.

The same sub-bin refinement section 26 of the S4 document describes applies
here, and matters more: correlating across N beats and dividing recovers N times
the precision. It also helps less, because the analysis window bounds how far
the measurement can reach -- a window is 128 bins, under six seconds.

| | exact of 68 | octave-family | other |
|---|---:|---:|---:|
| As found | 8 | 40 | 20 |
| + prior | 17 | 2 | 49 |
| + refinement | **52 (76.5%)** | 3 | 13 |

### 16.5 S6 is not simply better than S4 now

52 of 68 against S4's 43 is not the whole picture, and reading it as "S6 wins"
would repeat the mistake section 26 of the S4 document warns about: exact is a
1% pass mark, and 1% at 128 BPM is 1.3 BPM.

| | within 1% | median error among those | median minutes to half a beat | within 0.1% |
|---|---:|---:|---:|---:|
| S4 | 43 | 0.013% | 31.3 | 33 of 68 |
| S6 | 52 | 0.557% | 0.66 | 16 of 68 |

S4 is far more precise when it is right; S6 is more often roughly right. On the
34 tracks where both land within 1%, S4 is closer on 30. S6 is right on 18
tracks where S4 is wrong, and those are not near misses -- S4 reports 145.83
against a truth of 124.00, or 137.13 against 127.00, while S6 gets both.

The two engines fail differently because they are built differently: S6's longer
windows make it robust about which tempo region to believe, S4's finer bins make
it precise once the region is settled.

Section 24.2 of the S4 document proposed comparing independent estimates as a
source of confidence and noted that neither engine looks at the other. Both
obvious ways to use the comparison were simulated over these 68 tracks before
being proposed as work, and both fail. Section 16.8 records that.

The headroom is real -- taking whichever engine is closer on each track would
reach 61 of 68 against S4's 43 -- but neither simple rule captures it.

### 16.6 Cost, which fell

The shared correlation helper indexes its slice with a 32-bit offset where the
previous per-engine copies carried 64-bit bin indices through the innermost
loop. Measured with the section 22 harness of the S4 document, three runs:

| Row | Before | After |
|---|---:|---:|
| `+ BPM` | 497.2 - 512.7 | 394.5 - 395.8 |
| `+ LOCAL_BEATGRID` | 478.9 - 498.2 | 398.9 - 401.5 |
| `+ GLOBAL_BEATGRID` | 553.9 - 563.1 | 447.3 - 458.1 |
| `+ DYNAMIC_TEMPO` | 558.5 - 581.3 | 439.9 - 446.8 |
| `+ WAVEFORM_DETAIL + GRID_LOCKING` | 572.4 - 593.4 | 475.5 - 494.9 |

The `+ BPM` row isolates the refactor, since S6 is not requested there. All
seven rows are now under 500 microseconds per call, including the three that
section 22.1 of the S4 document recorded as the one success condition of seven
that the work did not meet. That claim is now out of date and is corrected
there.

This is a host measurement. On RV32IMAFC a 64-bit index costs a register pair
and multi-instruction arithmetic, so the same change should help at least as
much on target -- but that is an expectation, not a measurement.

### 16.7 Verification

The synthetic corpus is unchanged for S4 at 23 of 60. S4's output over the 68
real tracks is byte-identical after the refactor, which is the check that the
shared helper did not alter the local estimator. 77/77 tests pass, including
under ASan and UBSan.

### 16.8 Both ways of combining the engines, simulated and rejected

Now that both engines produce a tempo for the same track, the two uses section
24.2 suggested can be evaluated without writing them. Both were, over these 68
tracks, and neither survives.

**S6 selects, S4 supplies the value.** Keep S4's answer; where it disagrees with
S6 by more than three percent, rescale it by the metrical ratio that brings it
closest to S6. S4 still supplies the precision, S6 only decides which multiple
of it to believe.

| | within 1% | within 0.1% | median error |
|---|---:|---:|---:|
| S4 alone | 43 | 33 | 0.013% |
| S6 selects, S4 values | 40 | 30 | 0.015% |

It fixes one track and breaks four. The failure is not marginal: S6 answers
234.91 where the truth is 120.00 and S4 already had 120.01, and the rule dutifully
drags a correct answer to 240.02. Trusting S6 for selection requires knowing
when S6 is trustworthy, and S6's own confidence does not say -- it averages 83.6
when right and 76.8 when wrong.

**Agreement between the engines as confidence.** The disagreement does separate
the populations, with a median of 0.7% where S4 is right against 4.4% where it
is wrong. It is still not worth having:

| Gate | precision | recall |
|---|---:|---:|
| agreement within 1% | 94.1% | 74.4% |
| agreement within 2% | 87.5% | 81.4% |
| shipped confidence >= 70 | **100%** | 67.4% |
| shipped confidence >= 60 | 97.4% | 86.0% |
| both, confidence >= 70 and agreement within 2% | 100% | 60.5% |

The grid-fit confidence merged earlier already admits no wrong answer at 70, so
there is nothing for agreement to catch; combining them only costs recall.
Section 24.2 named this as one of two candidates for a confidence that knows
whether the answer is right. The other one, grid fit, took the whole job.

What remains is that S6 is right on 18 tracks where S4 is badly wrong, and no
rule tried here can tell which those are in advance.
