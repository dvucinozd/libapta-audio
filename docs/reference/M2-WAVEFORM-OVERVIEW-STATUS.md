# M2 waveform processing status

**Milestone status:** Complete  
**Profile status:** Self-tested implementation candidate  
**API version:** 0.1.0 draft  
**Verified merge commit:** `e9eef869578656a25a00ed583b8c657e71549f33`  
**Verification evidence:** GitHub Actions PR CI run `#147` completed successfully

## Advertised capabilities

A context may advertise:

```text
APTA_FEATURE_WAVEFORM_OVERVIEW
APTA_FEATURE_WAVEFORM_DETAIL
```

The context exposes only explicitly requested supported capabilities. A detail-enabled session currently requires both overview and detail because the overview path remains the authoritative PCM ownership and accepted-range layer.

## Implemented processing behaviour

The current implementation provides:

- push-mode PCM input for mono and stereo sources;
- S16, packed S24 little-endian, S32 and F32 conversion;
- deterministic stereo reduction using `(L + R) / 2`;
- copied bounded PCM queue nodes;
- partial push acceptance and backpressure;
- overlap rejection plus feature-specific detail replay;
- out-of-order non-overlapping PCM delivery;
- explicit sparse accepted ranges and gap-preserving demand;
- playback focus and explicit priority-region requests;
- soft-deadline ordering within one effective-priority class;
- FIFO tie-breaking;
- deterministic bounded starvation aging;
- bounded cooperative processing by frame, step and optional soft-time budget;
- overview geometry of 1024 source frames per column;
- detail geometry of 256 source frames per column and 64 columns per tile;
- bounded four-tile detail cache with focus/request protection and deterministic eviction;
- sparse min/max/RMS accumulation;
- ties-to-even quantization and clipping flags;
- immutable overview spans and detail tile snapshots;
- partial, stable and final waveform lifecycle states;
- progressive request status;
- final partial-column handling;
- publication retry after transient allocation failure;
- concurrent immutable result readers;
- cross-thread cooperative cancellation;
- measurable minimum and recommended memory requirements;
- context memory-limit enforcement.

## Container implementation

The version-1 `.apta` implementation includes:

- canonical little-endian fixed header and section directory;
- CRC32C Castagnoli;
- required `WOVR` writer and hardened reader;
- optional `WDTL` writer and hardened reader;
- canonical writer → reader → writer byte identity;
- sparse and partial result preservation;
- source metadata retained after session destruction;
- strict range, offset, overlap, geometry and reserved-field validation;
- configurable file, section, span, column and allocation limits;
- complete cleanup across injected WOVR and WDTL allocation failures;
- sanitizer-backed bounded fuzz smoke with final, sparse-partial and WDTL seeds.

## Runtime verification

The verified suite registers 28 runtime tests covering:

- core lifecycle, ownership and configuration;
- allocator failure cleanup;
- cancellation and cross-thread cancellation visibility;
- public initializers and version rejection;
- overview waveform semantics and block-boundary determinism;
- focus priority;
- deadline ordering for PCM demand and queued PCM processing;
- bounded starvation aging;
- detail geometry, request status, eviction and replay;
- memory requirements and memory limits;
- publication retry;
- WOVR/WDTL canonical writing, parsing, malformed input and round-trip;
- WOVR/WDTL allocation-failure sweeps;
- concurrent immutable result access.

The same CI run completed the AddressSanitizer/UndefinedBehaviorSanitizer build, canonical seed generation and bounded libFuzzer smoke run.

## Threading contract

The public prototype threading rules are documented in [`../api/APTA-THREADING-0.1.md`](../api/APTA-THREADING-0.1.md).

Result acquire/access/release operations may run concurrently with publication. Mutating session calls remain host-serialized except for explicitly thread-safe cancellation request/query operations.

Session destruction must not race with any operation receiving the same session pointer. Acquired immutable results may outlive their session, while the context remains busy until those results are released.

## Deliberate limitations

The implementation does not yet provide:

- PCM pull-mode analysis;
- waveform sources with more than two channels;
- three-band waveform values;
- multiple detail levels or dynamic tile-cache sizing;
- static-workspace-only operation;
- deterministic `META` section support;
- tempo or beatgrid analysis;
- stable API or ABI guarantees;
- measured resource-class or responsiveness claims.

## Conformance position

The bounded M2 waveform-processing milestone is complete.

The implementation satisfies the currently listed functional requirements for `APTA-WAVEFORM-0.1` and the waveform-processing portion of `APTA-ADAPTIVE-WAVEFORM-0.1`. It does not yet make a formal profile claim because the complete fixture, cross-platform, malformed-boundary and resource-measurement evidence package is not available.

See [`../conformance/APTA-WAVEFORM-READINESS-0.1.md`](../conformance/APTA-WAVEFORM-READINESS-0.1.md) for the requirement-by-requirement readiness matrix.

## Next bounded work

The next implementation sequence is:

1. deterministic version-1 `META` support and malformed metadata tests;
2. 32-bit ABI/build job and cross-platform container fixture;
3. complete fixed-header truncation corpus;
4. static-workspace allocator design and implementation;
5. measured embedded memory/stack report;
6. pull-mode PCM source ownership path;
7. optional three-band waveform processing.
