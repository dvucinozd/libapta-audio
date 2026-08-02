# APTA development roadmap status

- **Roadmap source:** [`../architecture/APTA-ARCHITECTURE-DRAFT.md`](../architecture/APTA-ARCHITECTURE-DRAFT.md)
- **Current completed stage:** S7 — ESP-IDF port
- **Current implementation stage:** S8 — Windows platform integration; local validation complete, MSVC CI pending
- **Next completed-stage gate:** green Windows/MSVC adapter, tool and runtime CI
- **Current source parent:** `c9743e37333fcb7aae499f6d3ff0431a709c77b7` plus the Stage S8 working branch
- **Current public API:** 0.3.0 draft
- **Current CTest inventory:** 85 default POSIX tests; 84 Windows tests; 72 core-only tests
- **Latest all-green native CI:** GitHub Actions run `30744535644` at the current source baseline
- **Current-head platform/fixture checks:** ESP-IDF run `30744535677` and reference-fixture run `30744535645` passed

## Status summary

| Stage | Status | Evidence |
|---|---|---|
| S0 — Foundation | Functionally complete foundation | Repository, specification structure, charter, terminology, Apache-2.0 licensing and contribution/governance/security policies exist |
| S1 — Portable core API | Functionally complete implementation candidate | Opaque handles, allocator abstraction, PCM push/pull, bounded processing, cancellation, immutable snapshots, workspace query and checkpoint seeding |
| S2 — Waveform Profile | Functionally complete implementation candidate | Overview, detail tiles, three-band overview, progressive coverage, WOVR/WDTL serialization and vectors |
| S3 — `.apta` container | Functionally complete implementation candidate | Header, directory, META, WOVR, WDTL, TEMP, LGRD, GGRD, REVN, CRC, hardened parser/writer and fuzzing |
| S4 — Tempo and local grid | Functionally complete implementation candidate | BPM, candidates, confidence, local grid, locking and progressive lifecycle |
| S5 — Reference desktop tools | Functionally complete implementation candidate | POSIX/WAV input, analyzer, inspector, validator, global/dynamic selection, diagnostics and generated fixtures |
| S6 — Global grid and dynamic tempo | Functionally complete implementation candidate | Global refinement, multiple segments, dynamic tempo, explicit beats, immutable revisions and GGRD/REVN interchange |
| S7 — ESP-IDF port | Complete self-tested and cross-build-verified implementation candidate | ESP adapter, optional ESP-DSP helper, cooperative example, bounded profiles, 5.5.4/6.0.2 firmware builds and manual P4 measurements |
| S8 — Second independent platform | Implementation complete locally; MSVC CI pending | Native Windows adapter, WAV pull runtime, CLI tools and bidirectional Linux/Windows `.apta` interchange pass locally |
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
- static workspace and optional bounded result slots;
- queried static-workspace requirements;
- seeding a fresh session's waveform coverage from a compatible parsed result.

Status: complete functional implementation candidate. Stable API/ABI is not yet claimed.

## S2 — Waveform Profile

Implemented:

- progressive overview waveform;
- three-band overview columns behind `APTA_FEATURE_WAVEFORM_3BAND`, with the
  filterbank declared in `specification/waveform.md` section 5.3.1 as that
  section requires of producers;
- overview coverage confidence, independent of the tempo engine;
- bounded detail tiles;
- sparse coverage and gaps;
- focus/request scheduling;
- WOVR and WDTL interchange;
- golden, malformed, allocation and concurrency tests.

Deferred:

- three-band values for detail tiles. Detail tiles are replayable for arbitrary
  frame ranges and a recursive filter cannot be resumed at an arbitrary offset
  without per-tile history or a warm-up. They leave the band bytes and the flag
  zero, which the container permits.

Status: complete functional implementation candidate. Formal profile conformance remains withheld. Three-band values remain outside bit-exact reference conformance until a reference-filterbank profile is standardised, as `specification/waveform.md` states.

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

Status: complete self-tested functional implementation candidate. Independent
188-track validation confirms a substantial endorsement accuracy gain but
reopens the zero-high-confidence-metrical-error acceptance gate; confidence 75
admits three S4 errors and two endorsed S4 errors. See
[`S4-TEMPO-LOCAL-GRID-STATUS.md`](S4-TEMPO-LOCAL-GRID-STATUS.md) and
[`PHASE4-INDEPENDENT-TEMPO-CORPUS-STATUS.md`](PHASE4-INDEPENDENT-TEMPO-CORPUS-STATUS.md).
Phase 5 added a frozen development/hold-out protocol and rejected its first
confidence/endorsement candidate when the development gains did not generalize;
no candidate behavior was shipped. See
[`PHASE5-TEMPO-GENERALIZATION-STATUS.md`](PHASE5-TEMPO-GENERALIZATION-STATUS.md).
Phase 6 implemented a bounded, compile-time B3 three-band onset candidate for
S4. It improved the full 188-track S4 result by four and the endorsed result by
three without breaking a previously correct selection. The candidate remains
opt-in because the 48-track partition had already been observed in phase 5 and
the high-confidence metrical-error gate is unchanged. Applying B3 to S6 was
measured and rejected; S6 retains its production broadband envelope. See
[`PHASE6-MULTIBAND-ONSET-STATUS.md`](PHASE6-MULTIBAND-ONSET-STATUS.md).
Phase 7 made the S4 and S6 refresh scans cooperative under the existing step
and soft-time budgets. On the ESP32-P4 full profile it reduced the measured
maximum process call from 19.323 ms to 12.355 ms, with p99 <= 8.2 ms and 42.1%
worst-call margin inside a 1024-frame audio block. See
[`PHASE7-P4-BOUNDED-REFRESH-STATUS.md`](PHASE7-P4-BOUNDED-REFRESH-STATUS.md).

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
- capability-derived `--features all` selection;
- global/dynamic feature selection and `GGRD`/`REVN` inspection;
- surfaced result diagnostics;
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
- ESP-IDF 6.0.2 / ESP32-S3 ESP-DSP cross-build;
- manual ESP32-P4 timing and memory measurements for the seven feature sets in
  the cooperative example.

The current source tree registers 83 tests in the default POSIX configuration
and 71 in a core-only configuration without the POSIX desktop adapters and
tools. The ESP-IDF matrix links and verifies complete firmware artifacts and
runs component-size reports.

For current source baseline `9709c67`, native CI run `30744535644` passed the
80-test POSIX job, the 68-test 32-bit core job, the sanitized 80-test parser job
with fuzz smoke and the Windows/MSVC 68-test core job. Reference-fixture run
`30744535645` passed for the same commit. ESP-IDF run `30744535677` also
passed, covering the bounded memory profiles and the supported cross-build
matrix.

Status: complete self-tested and cross-build-verified implementation candidate.
Manual ESP32-P4 measurements now cover average, histogram-bounded p99 and
maximum process-call timing, heap cleanup/largest block and stack high-water
evidence for one board/configuration. CI still does not execute firmware on
hardware; a long playback/watchdog soak, hardware decoder/USB integration and
repeatable measurements on intended production targets remain stronger
validation gates rather than absent Stage S7 implementation items.

See [`S7-ESP-IDF-PORT-STATUS.md`](S7-ESP-IDF-PORT-STATUS.md), [`../reference/APTA-ESP-IDF-MEMORY-PROFILES-0.1.md`](../reference/APTA-ESP-IDF-MEMORY-PROFILES-0.1.md), [`../conformance/APTA-S7-READINESS-0.1.md`](../conformance/APTA-S7-READINESS-0.1.md) and [`../../ports/espidf/README.md`](../../ports/espidf/README.md).

## S8 — Second independent platform

Windows is the selected second platform. The working branch implements:

- a native Win32 file adapter with a platform-neutral UTF-8 path API;
- checked 64-bit reads and flushed atomic result-file replacement;
- the WAV pull decoder and reference CLI tools over that adapter;
- native adapter, malformed-input, pull-analysis and generated CLI tests;
- regular consumption of the independently produced Python container fixture;
- an updated Windows CI job that builds adapters and tools and executes their
  runtime tests with warnings as errors.

Local Windows x64 MinGW builds and native runtime tests pass. Linux-produced
containers validate and inspect on Windows, Windows-produced containers
validate on Linux, and the independent Python fixture validates on Windows.
The final stage-completion gate is the updated MSVC CI run; until it passes,
S8 remains an implementation candidate rather than the current completed
stage.

See [`S8-WINDOWS-PORT-STATUS.md`](S8-WINDOWS-PORT-STATUS.md) and
[`../../ports/windows/README.md`](../../ports/windows/README.md).

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
