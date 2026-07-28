# APTA waveform profile readiness report 0.1

**Report type:** self-tested implementation readiness, not certification  
**Implementation:** `libapta` 0.1.0 draft  
**Source commit:** `e9eef869578656a25a00ed583b8c657e71549f33`  
**Primary verification:** GitHub Actions PR CI run `#147`  
**Runtime tests:** 28  
**Resource-class claim:** none

## Claim position

The current implementation satisfies the functional requirements currently listed for:

- `APTA-WAVEFORM-0.1`; and
- the waveform-processing portion of `APTA-ADAPTIVE-WAVEFORM-0.1`.

A formal profile claim is intentionally withheld because the complete conformance evidence package is not yet available. In particular, the repository does not yet contain a finalized machine-readable fixture manifest and hash, 32-bit/cross-endian validation, complete mandatory truncation fixtures, or measured resource-class results.

## Common baseline

| Requirement | Implementation | Evidence position |
|---|---|---|
| Source-frame time and half-open ranges | Implemented | Runtime waveform, region and container tests |
| Fixed-width public values | Implemented | C11/C++11 compile and ABI-layout checks |
| Immutable result generations | Implemented | Result lifecycle and concurrency tests |
| Lifecycle and confidence rules | Implemented for waveform features | Overview/detail accessors and serialization tests |
| Unsupported feature rejection | Implemented | Core/API contract tests |
| Bounded allocation and configured limits | Implemented | Memory-limit and parser-limit tests |
| No mandatory codec/filesystem/USB/network/UI ownership | Implemented | Public API and core architecture |

## `APTA-WAVEFORM-0.1`

| Required capability | Status | Evidence |
|---|---|---|
| PCM push input | Implemented and tested | Mono/stereo and supported sample-format tests |
| Explicit end of input | Implemented and tested | Final-column and lifecycle tests |
| Overview waveform | Implemented and tested | Golden peak/RMS and determinism tests |
| Explicit coverage and gaps | Implemented and tested | Sparse/out-of-order overview tests |
| Immutable waveform snapshots | Implemented and tested | Result ownership and concurrency tests |
| Semantic waveform conformance | Implementation candidate | Geometry, flags, lifecycle and block-boundary tests |
| Version-1 `WOVR` writer and reader | Implemented and tested | Canonical writer, round-trip and hardened reader tests |
| Required malformed waveform rejection | Broad coverage implemented | Negative corpus, allocation sweep, sanitizers and fuzz smoke |

### Optional waveform capabilities

| Optional capability | Status |
|---|---|
| PCM pull input | Not implemented |
| Detail tiles | Implemented, bounded four-tile cache |
| Three-band waveform | Not implemented |
| `REFERENCE-WAVEFORM-0.1` qualifier | Not formally claimed; core peak/RMS behaviour is deterministic |
| Partial `.apta` results | Implemented for `WOVR`; detail serialization preserves partial state |

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
| Low-memory detail retention/eviction | Implemented | Four-tile focus-protected LRU/replay test |
| Progressive request status | Implemented | Detail/overview request progress tests |
| Focus movement changes future work without mutating old results | Implemented | Immutable results plus focus-triggered detail replay |

## Container-format coverage

The canonical writer currently emits:

1. required `WOVR`; and
2. optional `WDTL` when detail tiles are present.

The reader validates CRC32C, directory and payload ranges, overlap, reserved fields, waveform geometry, packed columns, lifecycle states, duplicate identities, configured file/section/span/column/allocation limits, and unsupported required sections.

Writer/reader/writer byte identity is covered for overview-only and detail-containing results. Overview-only output remains byte-identical after introduction of `WDTL` support.

## Safety and hardening evidence

The CI configuration currently runs:

- ISO C11 and C++11 public-header compilation;
- normal optimized build and 28 runtime tests;
- AddressSanitizer and UndefinedBehaviorSanitizer runtime tests;
- canonical final, sparse-partial and WDTL fuzz seeds;
- bounded libFuzzer smoke execution;
- allocation-failure sweeps for WOVR and WDTL parser-owned allocations;
- concurrent immutable-result reader testing;
- cross-thread cancellation testing.

## Missing evidence for a formal claim

The following items remain open:

- machine-readable fixture manifest with a recorded cryptographic manifest hash;
- exhaustive fixed-header truncation fixtures at every boundary;
- complete malformed `META` coverage once metadata support is implemented;
- 32-bit ABI/build execution;
- cross-endian or independently produced container fixtures;
- shared-library symbol/export checks on supported platforms;
- long-running fuzz campaign report and retained minimized corpus;
- measured peak stack and process-call latency;
- declared and measured resource-class workload;
- independent ESP-IDF integration validation;
- governance approval for publishing an official profile claim.

## Current permitted statement

The project may accurately state:

> `libapta` 0.1.0 is a self-tested implementation candidate for APTA Waveform 0.1 and the waveform-processing portion of Adaptive Waveform 0.1. No certified profile or resource-class claim is currently made.
