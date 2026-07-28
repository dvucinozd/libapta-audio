# M1 portable core prototype status

**Status:** Implementation candidate  
**API version:** 0.1.0 draft  
**Profile conformance:** None claimed yet

## Implemented

The current portable core implements:

- C11 public headers with C++ linkage guards;
- fixed-width public status, state and feature types;
- opaque context, session and result objects;
- context and session configuration initializers;
- output/view structure initializers;
- optional custom allocator, logger and monotonic clock callbacks;
- context memory accounting and configured allocation limit;
- context/session/result ownership rules;
- immutable result generations with atomic reference counting;
- result lifetime beyond session destruction;
- push PCM validation and accepted-frame reporting;
- pull-source registration contract;
- explicit end-of-input signalling;
- playback focus and region-request API surfaces;
- cooperative process entry point;
- thread-safe cancellation request;
- session lifecycle transitions;
- C/C++ compile checks and native layout assertions;
- linked runtime tests for lifecycle, ownership, cancellation, initializers, version rejection and allocation failures.

## Deliberately not implemented

The M1 core does not yet implement an analysis feature.

`apta_context_get_capabilities()` therefore returns zero and context creation rejects non-zero `requested_capabilities`.

A session can be created only with `requested_features == 0`. PCM accepted by such a session is validated but does not need to be retained because no analysis output was requested.

The implementation MUST NOT claim:

- APTA Waveform Profile conformance;
- Adaptive Waveform Profile conformance;
- BPM or beatgrid capability;
- `.apta` writer or reader support;
- stable API or ABI status.

## Why capability zero is required

Advertising waveform capability before the accumulator, coverage model and immutable waveform payload exist would violate the capability rule:

> An implementation must not advertise a capability it cannot expose using the normative result model.

The lifecycle scaffold is intentionally testable without making a false interoperability claim.

## M1 exit evidence

| Requirement | Evidence |
|---|---|
| Opaque handles | `include/apta/apta_types.h` |
| Fixed-width public types | `include/apta/apta_types.h`, ABI compile assertions |
| Initializers | `apta_config.c`, `apta_initializers.c` |
| Allocator abstraction | `apta_memory.c`, allocation-failure test |
| PCM push contract surface | `apta_session_source.c` |
| Focus and request API surface | `apta_session_scheduler.c` |
| Cooperative processing | `apta_session_lifecycle.c` |
| Immutable generations | `apta_result.c`, core smoke test |
| Cancellation | cancellation test |
| C/C++ header compilation | compile-only targets |
| Linkable core | `apta_core` CMake target |

## Remaining M1 hardening

Before marking M1 complete:

- confirm CI build and all runtime tests pass;
- add a 32-bit ABI-layout CI job or cross-compile check;
- define and test concurrent result acquire/release stress behaviour;
- add explicit custom memory-limit tests;
- document destruction concurrency rules;
- decide whether API version validation remains exact during pre-1.0 or accepts compatible older minor structures.

## Next milestone

M2 begins by implementing one real capability:

```text
APTA_FEATURE_WAVEFORM_OVERVIEW
```

The first waveform implementation will provide:

- copied bounded PCM queue;
- non-overlapping out-of-order source ranges;
- deterministic mono/stereo reduction;
- sparse overview-column accumulators;
- explicit coverage spans and gaps;
- immutable waveform payloads in result generations;
- bounded processing by frame and step budget;
- golden-vector tests independent of PCM block boundaries.

Only after those tests pass will the context advertise overview waveform capability.
