# M1 portable core prototype status

**Status:** Completed and superseded by [`M2-WAVEFORM-OVERVIEW-STATUS.md`](M2-WAVEFORM-OVERVIEW-STATUS.md)  
**API version:** 0.1.0 draft  
**Profile conformance:** None claimed

M1 established the portable C11 lifecycle, ownership, allocation, cancellation and immutable-result foundation.

The statements in the original M1 snapshot that the capability mask was zero are historical. The current implementation now advertises `APTA_FEATURE_WAVEFORM_OVERVIEW` and is tracked by the M2 status document.

## M1 deliverables retained

- C11 public headers with C++ linkage guards;
- fixed-width public types and opaque handles;
- context, session and result lifecycle;
- custom allocator, logger and monotonic-clock callbacks;
- bounded allocation accounting;
- immutable result generations with atomic reference counting;
- result lifetime beyond session destruction;
- push PCM contract surface;
- explicit end-of-input signalling;
- focus and region-request API surfaces;
- cooperative processing and cancellation;
- public structure initializers;
- C/C++ compile checks and ABI layout assertions;
- linked runtime tests for lifecycle, ownership, cancellation, allocation failure and version rejection.

## Historical M1 limitations

M1 intentionally advertised no analysis capability. That restriction was removed only after the M2 overview accumulator, sparse coverage model, immutable waveform payload and golden tests were added.

M1 never claimed:

- APTA Waveform Profile conformance;
- Adaptive Waveform Profile conformance;
- BPM or beatgrid capability;
- `.apta` reader or writer support;
- stable API or ABI status.

Those non-claims remain in force unless a later status document explicitly changes them.
