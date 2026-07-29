# APTA development roadmap status

**Roadmap source:** [`../architecture/APTA-ARCHITECTURE-DRAFT.md`](../architecture/APTA-ARCHITECTURE-DRAFT.md)  
**Current completed stage:** S5 — Reference desktop tools  
**Next stage:** S6 — Global grid and dynamic tempo  
**Latest implementation merge:** `b971be282dea0b7fb168e938d39356152537f4b1`  
**Latest full verification:** GitHub Actions PR CI run `#246`, 63 runtime tests

## Status summary

| Stage | Status | Evidence |
|---|---|---|
| S0 — Foundation | Functionally complete | Repository, licensing, charter, terminology, non-goals, contribution and security policy |
| S1 — Portable core API | Functionally complete implementation candidate | Opaque handles, allocator abstraction, PCM push/pull, bounded processing, cancellation, immutable snapshots |
| S2 — Waveform Profile | Functionally complete implementation candidate | Overview, detail tiles, progressive coverage, WOVR/WDTL serialization and vectors |
| S3 — `.apta` container | Functionally complete implementation candidate | Header, directory, META, WOVR, WDTL, TEMP, LGRD, CRC, hardened parser/writer and fuzzing |
| S4 — Tempo and local grid | Functionally complete implementation candidate | BPM, candidates, confidence, local grid, locking and progressive lifecycle |
| S5 — Reference desktop tools | Functionally complete implementation candidate | POSIX adapter, WAV decoder boundary, analyzer, inspector, validator and generated-fixture integration |
| S6 — Global grid and dynamic tempo | Not started | Global refinement, multi-segment/dynamic tempo and explicit beats remain |
| S7 — ESP-IDF port | Not started as an independent port | Core bounded-memory work is ready for integration, but adapter/backend/example evidence remains |
| S8 — Second independent platform | Not started | Independent platform integration remains |
| S9 — APTA 1.0 | Not started | Stable specification/API/format and multi-platform conformance remain |

## S0 — Foundation

Implemented:

- repository and project identity;
- Apache-2.0 reference implementation licensing;
- specification/documentation structure;
- terminology and explicit non-goals;
- contribution, governance and security material.

The architecture draft remains a working draft rather than a stable standard.

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

Next implementation scope:

1. global grid refinement across the analysed source;
2. multiple tempo/grid segments;
3. dynamic-tempo representation;
4. explicit beat representation where required;
5. revision and replacement behavior for progressively refined grids;
6. new conformance vectors for tempo ramps, abrupt changes and ambiguous transitions.

S6 must preserve S4 local evidence/applicability semantics and must not mutate already acquired result generations.

## Claims

“Stage complete” in this document means that the functional items listed for the stage in the architecture roadmap exist in the reference implementation and have self-tested evidence.

It does not mean:

- stable APTA 1.0 specification;
- stable public API or ABI;
- certified profile conformance;
- independent implementation interoperability;
- measured resource-class certification;
- completion of later stages.
