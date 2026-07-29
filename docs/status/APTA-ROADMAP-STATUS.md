# APTA development roadmap status

- **Roadmap source:** [`../architecture/APTA-ARCHITECTURE-DRAFT.md`](../architecture/APTA-ARCHITECTURE-DRAFT.md)
- **Current completed stage:** S7 — ESP-IDF port
- **Next stage:** S8 — Second independent platform
- **Latest implementation baseline:** `cf622e84854aae53cedeca0ba06725479d84b8eb`
- **Latest all-green native CI:** run `#294` at `1bfcea06fdca41e2abac9573d481aed44e7bc39e`
- **Current-head CI boundary:** run `#295`; POSIX, 32-bit and parser-hardening jobs passed, Windows/MSVC compilation failed
- **Current-head platform/fixture checks:** ESP-IDF run `#17` and reference-fixture run `#3` passed

## Status summary

| Stage | Status | Evidence |
|---|---|---|
| S0 — Foundation | Functionally complete foundation | Repository, specification structure, charter, terminology, Apache-2.0 licensing and contribution/governance/security policies exist |
| S1 — Portable core API | Functionally complete implementation candidate | Opaque handles, allocator abstraction, PCM push/pull, bounded processing, cancellation, immutable snapshots |
| S2 — Waveform Profile | Functionally complete implementation candidate | Overview, detail tiles, progressive coverage, WOVR/WDTL serialization and vectors |
| S3 — `.apta` container | Functionally complete implementation candidate | Header, directory, META, WOVR, WDTL, TEMP, LGRD, GGRD, REVN, CRC, hardened parser/writer and fuzzing |
| S4 — Tempo and local grid | Functionally complete implementation candidate | BPM, candidates, confidence, local grid, locking and progressive lifecycle |
| S5 — Reference desktop tools | Functionally complete implementation candidate | POSIX adapter, WAV decoder boundary, analyzer, inspector, validator and generated-fixture integration |
| S6 — Global grid and dynamic tempo | Functionally complete implementation candidate | Global refinement, multiple segments, dynamic tempo, explicit beats, immutable revisions and GGRD/REVN interchange |
| S7 — ESP-IDF port | Complete self-tested and cross-build-verified implementation candidate | ESP allocator/clock/logger, optional ESP-DSP helper, IDF component, cooperative example, bounded profiles and 5.5.4/6.0.2 firmware builds |
| S8 — Second independent platform | Not started | A Windows core CI preflight exists, but it is not a completed platform integration and the current MSVC job is failing |
| S9 — APTA 1.0 | Not started | Stable specification/API/format and multi-platform conformance remain |

## S0 — Foundation

Implemented:

- repository and project identity;
- specification/documentation structure;
- terminology and explicit non-goals.

Release-policy foundation:

- the root [`../../LICENSE`](../../LICENSE) contains the complete Apache
  License 2.0 and matches tracked source/build/test SPDX identifiers;
- [`../../CONTRIBUTING.md`](../../CONTRIBUTING.md),
  [`../../GOVERNANCE.md`](../../GOVERNANCE.md) and
  [`../../SECURITY.md`](../../SECURITY.md) define contribution, decision and
  coordinated-disclosure processes;
- `SECURITY.md` provides a private email contact. GitHub's structured
  private-report form is documented as an additional channel only when it is
  available and enabled.

The architecture draft remains a working draft rather than a stable standard.
Its licensing section now records the repository's Apache-2.0 model.

## S1 — Portable core API

Implemented:

- opaque context/session/result handles;
- versioned public structures;
- allocator, logger and monotonic-clock abstractions;
- push and pull PCM input;
- bounded cooperative processing;
- cancellation;
- immutable result generations;
- result lifetime beyond session destruction;
- static workspace and optional bounded result slots.

Status: complete functional implementation candidate. Stable API/ABI is not yet claimed.

## S2 — Waveform Profile

Implemented:

- progressive overview waveform;
- bounded detail tiles;
- sparse coverage and gaps;
- focus/request scheduling;
- WOVR and WDTL interchange;
- golden, malformed, allocation and concurrency tests.

Status: complete functional implementation candidate. Formal profile conformance remains withheld.

## S3 — `.apta` container

Implemented:

- fixed header and section directory;
- little-endian encoding;
- CRC32C;
- deterministic META;
- WOVR and WDTL;
- TEMP and LGRD for Stage S4;
- GGRD and REVN for Stage S6;
- canonical writer and hardened parser;
- truncation, malformed, allocation-failure and fuzz coverage;
- independent WOVR+META fixture producer.

Status: complete functional container foundation for current features. Future stages may add new optional versioned sections.

## S4 — Tempo and local grid

Implemented:

- BPM model in millibpm;
- ordered candidate model;
- independent confidence;
- one local constant-period grid segment;
- requested/evidence/applicability/coverage ranges;
- stable range locking;
- `PROVISIONAL → STABLE → FINAL` lifecycle;
- TEMP/LGRD interchange;
- push, pull, heap, static-workspace and bounded-result support.

Status: complete self-tested implementation candidate. See [`S4-TEMPO-LOCAL-GRID-STATUS.md`](S4-TEMPO-LOCAL-GRID-STATUS.md).

## S5 — Reference desktop tools

Implemented:

- checked 64-bit POSIX read-only file adapter;
- replaceable decoder callback boundary outside the core;
- reference WAV decoder for PCM16, packed PCM24, PCM32 and float32;
- standard and `WAVE_FORMAT_EXTENSIBLE` input;
- pull-mode decoder bridge;
- `apta-analyze` with canonical atomic output;
- `apta-inspect` with human and JSON output;
- `apta-validate` with normal, strict and quiet modes;
- runtime-generated deterministic audio fixture;
- end-to-end valid and truncated-container CLI tests.

Status: complete self-tested implementation candidate. See [`S5-REFERENCE-DESKTOP-TOOLS-STATUS.md`](S5-REFERENCE-DESKTOP-TOOLS-STATUS.md) and [`../reference/APTA-DESKTOP-TOOLS-0.1.md`](../reference/APTA-DESKTOP-TOOLS-0.1.md).

The portable core still does not own codecs, filesystems, threads or application scheduling.

## S6 — Global grid and dynamic tempo

Implemented:

- bounded global refinement over a long-range onset envelope;
- one to eight ordered global tempo/grid segments;
- dynamic-tempo feature and hybrid representation;
- bounded explicit beat arrays where required;
- immutable geometry revision identifiers;
- pending revisions for conflicts with locked local ranges;
- explicit host application of an exact pending revision;
- heap, static-workspace and bounded-result operation;
- canonical GGRD/REVN version-1 interchange;
- strict malformed, truncation, allocation-failure, sanitizer and fuzz evidence.

S6 preserves S4 local evidence/applicability and locking semantics. It never mutates already acquired result generations.

Status: complete self-tested implementation candidate. See [`S6-GLOBAL-GRID-DYNAMIC-TEMPO-STATUS.md`](S6-GLOBAL-GRID-DYNAMIC-TEMPO-STATUS.md), [`../reference/APTA-S6-CONTAINER-0.1.md`](../reference/APTA-S6-CONTAINER-0.1.md) and [`../conformance/APTA-S6-READINESS-0.1.md`](../conformance/APTA-S6-READINESS-0.1.md).

## S7 — ESP-IDF port

Implemented:

- capability-aware aligned allocator using ESP-IDF heap capabilities;
- configurable memory-class-to-capability mapping;
- strict and fallback allocation policies;
- monotonic nanosecond clock derived from `esp_timer_get_time()`;
- optional `esp_log` logger binding;
- optional ESP-DSP dot-product helper with scalar fallback;
- full ESP-IDF component build of the existing portable core;
- cooperative application-managed scheduler example;
- bounded waveform, local-performance and global-dynamic memory profiles;
- host-stub adapter regression;
- ESP-IDF 5.5.4 / ESP32 scalar cross-build;
- ESP-IDF 6.0.2 / ESP32 scalar cross-build;
- ESP-IDF 6.0.2 / ESP32-S3 ESP-DSP cross-build.

The default POSIX suite registers 69 tests. A core-only configuration without
the POSIX desktop adapters and tools registers 59 tests. The ESP-IDF matrix
links and verifies complete firmware artifacts and runs component-size reports.

For implementation baseline `cf622e8`, CI run `#295` passed the 69-test POSIX
job, the 59-test 32-bit core job and the sanitized 69-test parser job with fuzz
smoke. Its new Windows/MSVC job failed during compilation because the runner's
C11 atomic support was not enabled, so the current head does not have an
all-green native CI claim. ESP-IDF run `#17` and reference-fixture run `#3`
passed for the same commit. The latest all-green native workflow remains run
`#294` on the preceding documentation-only S7 baseline `1bfcea0`.

Status: complete self-tested and cross-build-verified implementation candidate. Physical-board execution, stack high-water marks, on-target latency, fragmentation and hardware decoder/USB integration remain stronger validation gates rather than absent Stage S7 implementation items.

See [`S7-ESP-IDF-PORT-STATUS.md`](S7-ESP-IDF-PORT-STATUS.md), [`../reference/APTA-ESP-IDF-MEMORY-PROFILES-0.1.md`](../reference/APTA-ESP-IDF-MEMORY-PROFILES-0.1.md), [`../conformance/APTA-S7-READINESS-0.1.md`](../conformance/APTA-S7-READINESS-0.1.md) and [`../../ports/espidf/README.md`](../../ports/espidf/README.md).

## S8 — Second independent platform

Next implementation scope:

Implement and validate at least one independent platform integration, such as:

1. Zephyr;
2. Windows;
3. STM32 bare metal;
4. Linux DJ player;
5. mobile application.

The selected platform must consume the public APTA API and `.apta` data model without depending on ESP-IDF adapter internals. Its evidence should include platform build/runtime tests and interchange with the existing reference implementation.

The existing Windows core CI job is only a portability preflight. It does not
provide a Windows adapter, runtime integration or S8 completion evidence, and
its current MSVC C11-atomics failure must be resolved before it can serve as a
green build gate.

## Claims

“Stage complete” in this document means that the functional items listed for the stage in the architecture roadmap exist in the reference implementation and have self-tested evidence appropriate to their layer.

It does not mean:

- stable APTA 1.0 specification;
- stable public API or ABI;
- certified profile conformance;
- physical-device certification for every supported build target;
- independent implementation interoperability;
- measured resource-class certification;
- completion of later stages.
