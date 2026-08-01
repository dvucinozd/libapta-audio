# APTA waveform profile readiness report 0.1

**Report type:** self-tested implementation readiness, not certification  
**Implementation:** `libapta` 0.1.0 draft  
**Source commit:** `b1c9100b2acee13188c650e71e6364bacbae7e7c`  
**Primary verification:** GitHub Actions PR CI run `#188`  
**Runtime tests:** 43  
**Resource-class claim:** none

> Evidence snapshot: statuses and the 43-test count below apply to source commit
> `b1c9100b`. PCM pull, Stage S4–S7 features and later platform/build evidence
> are tracked in
> [`../status/APTA-ROADMAP-STATUS.md`](../status/APTA-ROADMAP-STATUS.md).

## Claim position

The current implementation satisfies the functional requirements currently listed for:

- `APTA-WAVEFORM-0.1`; and
- the waveform-processing portion of `APTA-ADAPTIVE-WAVEFORM-0.1`.

A formal profile claim is intentionally withheld because the complete conformance evidence package is not yet available. The repository still lacks measured resource-class results, cross-endian execution, shared-library export evidence, a maintained long-running fuzz campaign and independent ESP-IDF integration evidence.

## Common baseline

| Requirement | Implementation | Evidence position |
|---|---|---|
| Source-frame time and half-open ranges | Implemented | Runtime waveform, region and container tests |
| Fixed-width public values | Implemented | C11/C++11 compile, ABI-layout and 32-bit execution checks |
| Immutable result generations | Implemented | Heap and pooled result lifetime, metadata ownership and concurrency tests |
| Lifecycle and confidence rules | Implemented for waveform features | Overview/detail accessors, bounded publication and serialization tests |
| Unsupported feature rejection | Implemented | Core/API contract tests |
| Bounded allocation and configured limits | Implemented | Memory-limit, parser-limit, allocation-sweep, workspace and result-pool tests |
| No mandatory codec/filesystem/USB/network/UI ownership | Implemented | Public API and core architecture |

## `APTA-WAVEFORM-0.1`

| Required capability | Status | Evidence |
|---|---|---|
| PCM push input | Implemented and tested | Mono/stereo and supported sample-format tests |
| Explicit end of input | Implemented and tested | Final-column, pooled completion and lifecycle tests |
| Overview waveform | Implemented and tested | Golden peak/RMS, determinism and bounded WOVR tests |
| Explicit coverage and gaps | Implemented and tested | Sparse/out-of-order overview tests |
| Immutable waveform snapshots | Implemented and tested | Heap/pooled ownership and concurrency tests |
| Semantic waveform conformance | Implementation candidate | Geometry, flags, lifecycle and block-boundary tests |
| Version-1 `WOVR` writer and reader | Implemented and tested | Canonical writer, round-trip and hardened reader tests |
| Required malformed waveform rejection | Broad coverage implemented | Negative corpus, allocation sweep, truncation corpus, sanitizers and fuzz smoke |

### Optional waveform capabilities

| Optional capability | Status |
|---|---|
| PCM pull input | Not implemented at this snapshot; implemented later |
| Detail tiles | Implemented, bounded four-tile cache |
| Three-band waveform | Implemented for overview columns; see specification/waveform.md section 5.3.1. Detail tiles leave bands zero and unflagged. |
| `REFERENCE-WAVEFORM-0.1` qualifier | Not formally claimed; core peak/RMS behaviour is deterministic |
| Partial `.apta` results | Implemented for `WOVR`; detail serialization preserves partial state |
| Deterministic `META` | Implemented with owned bounded public API and canonical CBOR |
| Bounded immutable result slots | Implemented for known-duration workspace sessions |

## `APTA-ADAPTIVE-WAVEFORM-0.1`

| Additional requirement | Status | Evidence |
|---|---|---|
| Playback focus | Implemented | Focus-priority processing test |
| Explicit priority-region requests | Implemented | Request contract and scheduler tests |
| Bounded cooperative processing | Implemented | Frame/step work-budget tests |
| Push-mode PCM demand | Implemented | Overview and detail demand tests |
| Local detail publication before unrelated background completion | Implemented | Detail region and replay tests |
| Priority preservation | Implemented | Temporary effective-priority projection with restoration |
| Starvation prevention policy | Implemented | Bounded skip-aging test |
| Low-memory detail retention/eviction | Implemented | Four-tile focus-protected LRU/replay and maximum-capacity tests |
| Progressive request status | Implemented | Detail/overview request progress tests |
| Focus movement changes future work without mutating old results | Implemented | Immutable results plus focus-triggered detail replay |

## Container-format coverage

The canonical writer emits:

1. required `WOVR`;
2. optional `WDTL` when detail tiles are present; and
3. optional deterministic-CBOR `META` when metadata is present.

The reader validates CRC32C, directory and payload ranges, overlap, reserved fields, waveform geometry, packed columns, lifecycle states, duplicate identities, deterministic recognized metadata, configured file/section/span/column/allocation limits, and unsupported required sections.

Writer/reader/writer byte identity is covered for overview-only, detail-containing, metadata-containing and combined detail-plus-metadata results. Waveform-only output remains byte-identical when metadata is absent.

Every byte-prefix truncation of writer-generated canonical WOVR, WDTL and META fixtures is rejected as corrupt. A valid file with one trailing byte is also rejected.

An independently implemented Python producer generates a committed 303-byte WOVR+META fixture with a machine-readable SHA-256 manifest. The C library parser accepts it and the library writer reproduces it byte-identically.

A retained pooled META/WOVR/WDTL result is also serialized after its session is destroyed and its caller workspace is overwritten. The resulting container parses in an independent context and reproduces the same public metadata, overview and detail views.

## Safety and hardening evidence

The CI configuration and verified evidence include:

- ISO C11 and C++11 public-header compilation;
- normal optimized build and 43 runtime tests;
- AddressSanitizer and UndefinedBehaviorSanitizer runtime tests;
- actual GCC/G++ `-m32` compilation and execution of the runtime suite;
- canonical final, sparse-partial, WDTL and META fuzz seeds;
- bounded libFuzzer smoke execution;
- allocation-failure sweeps for WOVR, WDTL and META parser-owned allocations;
- exhaustive canonical prefix truncation for WOVR, WDTL and META;
- independent producer, fixture manifest and byte-identity consumer test;
- concurrent ordinary and pooled immutable-result reader testing;
- cross-thread cancellation testing;
- caller-owned session workspace tests for PCM, accepted ranges, overview accumulators and session metadata;
- fixed-slot pool layout, allocation, exhaustion, retry and lifetime tests;
- maximum public metadata and four-tile detail capacity tests;
- pooled serialization after session/workspace destruction.

## Bounded workspace and result position

Workspace sessions keep mutable state in caller-owned storage:

- session object;
- queued PCM;
- accepted ranges;
- overview accumulators;
- session metadata.

When `APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS` is enabled, immutable results use one preallocated context-owned two-slot pool. The context ownership is required because acquired results may outlive the session and caller workspace.

For the verified known-duration waveform configuration, successful create performs the context-object allocation and one complete pool allocation. Metadata replacement, PCM push, cooperative processing, WOVR/WDTL publication, result acquire/release, serialization into caller storage and session destruction make no later context-allocator calls.

Slot retention may produce the explicit transient `APTA_ERROR_RESULT_SLOTS_EXHAUSTED`. State, metadata and waveform publication paths preserve retry information and progress after an older result is released.

This evidence closes the zero-allocation immutable-publication implementation gap. It does not establish a resource-class ceiling because total context, pool, workspace, stack and latency measurements have not been published.

## Missing evidence for a formal claim

The following items remain open:

- cross-endian execution or an independently verified big-endian consumer;
- shared-library symbol/export checks on supported platforms;
- long-running fuzz campaign report and retained minimized corpus;
- measured peak stack and process-call latency;
- declared and measured resource-class workload;
- independent ESP-IDF integration validation;
- governance approval for publishing an official profile claim.

## Current permitted statement

The project may accurately state:

> `libapta` 0.1.0 is a self-tested implementation candidate for APTA Waveform 0.1 and the waveform-processing portion of Adaptive Waveform 0.1. Known-duration workspace sessions support preallocated two-slot immutable META/WOVR/WDTL publication without context allocation after successful creation. No certified profile or resource-class claim is currently made.
