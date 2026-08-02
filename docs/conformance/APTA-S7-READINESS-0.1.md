# APTA Stage S7 ESP-IDF readiness 0.1

**Status:** Verified implementation-candidate readiness record  
**Implementation merge:** `5f949197c2600a9c83a9187ec3625c841ab7f076`  
**Native CI:** run `#291`  
**ESP-IDF build matrix:** run `#13`  
**Formal platform-conformance claim:** not issued

## Scope

This document records evidence for architecture Stage S7 — ESP-IDF port. It distinguishes repository implementation/build readiness from physical-device runtime and resource certification.

## Readiness matrix

| Requirement | Status | Evidence |
|---|---|---|
| ESP-IDF component packaging | Verified | Full portable core registered through `idf_component_register()` |
| Public ESP-IDF adapter API | Verified | `apta_espidf.h` builds in native and IDF targets |
| Capability-aware allocator | Verified | Host callback regression and IDF link builds |
| Aligned allocation | Verified | Host regression checks requested alignment |
| Capability fallback | Verified | Specialized allocation failure retries default capabilities |
| Strict capability mode | Verified | Fallback-disabled failure path tested |
| Current deallocation API | Verified | Adapter uses `heap_caps_free()` |
| Monotonic clock | Verified | `esp_timer_get_time()` binding and unit conversion tested |
| ESP logging | Verified | Public log-level mapping and tag handling tested |
| Scalar DSP helper | Verified | Host runtime result and ESP32 links |
| Optional ESP-DSP helper | Verified for build/link | ESP32-S3 build resolves `dsps_dotprod_f32()` |
| Cooperative processing example | Verified for build/link | Complete firmware built for all matrix targets |
| ESP-IDF 5.5.4 compatibility | Verified for build/link | ESP32 scalar firmware artifact |
| ESP-IDF 6.0.2 compatibility | Verified for build/link | ESP32 scalar and ESP32-S3 ESP-DSP artifacts |
| Bounded embedded profiles | Verified on host reference build | Three profiles finish without post-create allocations |
| Native regressions | Verified | 69 CTest tests |
| Sanitizer coverage | Verified | ASan and UBSan jobs pass |
| Parser fuzz smoke retained | Verified | Existing canonical corpus and bounded libFuzzer run pass |
| Physical ESP32-P4 execution | Verified manually for one configuration | P4 v1.3, 360 MHz, ESP-IDF 5.5 feature sweep |
| Physical ESP32-S3 execution | Open | CI does not flash a board |
| On-target stack measurement | Verified manually for one P4 configuration | 5,776-word high-water value after the complete sweep |
| On-target latency measurement | Verified manually for one P4 configuration | Full profile average 3.657 ms, p99 <= 8.2 ms, max 12.355 ms |
| On-target heap fragmentation | Partially verified manually | Zero cleanup delta and 33,030,144-byte largest free block after the P4 sweep |
| USB/filesystem/decoder integration | Open | Application-specific hardware integration remains |
| Independent on-device `.apta` interoperability | Open | Requires another producer/consumer or target execution |
| Certified resource class | Open | No target-specific stack/latency/heap certification |

## Build evidence

The ESP-IDF workflow builds the reference example in four parallel gates:

1. bounded embedded memory profiles;
2. ESP-IDF 5.5.4 / ESP32 / scalar;
3. ESP-IDF 6.0.2 / ESP32 / scalar;
4. ESP-IDF 6.0.2 / ESP32-S3 / ESP-DSP.

Every build job produces and verifies a non-empty `apta_cooperative_scheduler.bin` artifact.

## Memory evidence

The profile job verifies:

```text
WAVEFORM_8S             workspace=131072  pool=55664   allocations=2
PERFORMANCE_LOCAL_6S    workspace=262144  pool=46736   allocations=2
GLOBAL_DYNAMIC_10_9S    workspace=1572864 pool=399408  allocations=2
```

No allocator callback occurs after successful session creation in any profile.

These values are integration evidence from the reference profile program. They exclude context object size, task stack, application buffers, IDF overhead and target fragmentation.

## Size evidence

The reference example's component archive contribution is:

```text
IDF 5.5.4 / ESP32 / scalar       36786 bytes
IDF 6.0.2 / ESP32 / scalar       36311 bytes
IDF 6.0.2 / ESP32-S3 / ESP-DSP   36342 bytes
```

Archive contribution is configuration- and linker-dependent. It is recorded for reproducibility, not specified as a compatibility guarantee.

## Stage-completion interpretation

Architecture Stage S7 is complete because every functional roadmap item has an implementation and an appropriate self-test or cross-build:

- allocator;
- clock;
- optional target helper backend;
- cooperative example;
- embedded memory profiles.

The remaining open gates concern repeated production-target measurements,
playback/watchdog and hardware-I/O coexistence, and independent
interoperability—not missing Stage S7 source or build functionality. The
single-board Phase 7 evidence is recorded in
[`../status/PHASE7-P4-BOUNDED-REFRESH-STATUS.md`](../status/PHASE7-P4-BOUNDED-REFRESH-STATUS.md).

## Claims not authorized by this record

This readiness record does not authorize claims of:

- APTA 1.0 stability;
- stable public API or ABI;
- certified ESP32 compatibility for every board/configuration;
- hard real-time behavior;
- watchdog-safe playback coexistence;
- a named APTA resource class;
- independent implementation conformance.
