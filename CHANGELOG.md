# Changelog

All notable changes to libapta-audio are documented in this file.

## [Unreleased] - 1.1.0 development

### Added

- optional musical-key, meter/downbeat and calibrated-quality feature bits,
  immutable views, initializers and result accessors;
- a bounded `apta_result_builder_t` that validates and deep-copies imported
  source information, metadata, provenance, waveform, tempo, grid, revision,
  key, meter and quality data into a normal immutable result;
- container-version-1 `MKEY`, `MTRD` and `CONF` optional sections with
  deterministic writing, strict parsing, CRC and allocation-limit checks, and
  frozen-1.0-reader compatibility evidence;
- `apta_output_stream_t` and `apta_input_stream_t`, streaming serialization,
  selective streaming parsing and caller-provided bounded scratch storage;
- LP64, ILP32 and pointer-32/alignment-64 public-layout evidence plus the 1.1
  exported-symbol and public-header delta manifests;
- native BPM confidence calibration (Task 6): the accepted
  `isotonic-pav-clamped-v1` model (protocol ID 1867860160) publishes an
  optional quality record for `APTA_FEATURE_BPM` when a host requests
  `APTA_FEATURE_CALIBRATED_QUALITY`; the model can only lower a reported
  confidence, never raise it, and acceptance evidence is frozen in
  `docs/status/APTA-1.1-CONFIDENCE-CALIBRATION-PROTOCOL.md`;
- `apta-meter-trace`, an opt-in development diagnostic tool
  (`APTA_ENABLE_EXPERIMENTAL_METER_TRACE=ON`) that emits APTA's published
  lattice and the internal per-beat strength series from real analyzer
  sessions; it is never built into shipped configurations.

### Compatibility boundary

- this is development-branch state, not a released 1.1 compatibility claim;
- the frozen APTA 1.0 normative manifest and published `v1.0.0`/`v1.0.1`
  releases are unchanged;
- container version remains 1; older readers validate and skip the new
  optional sections under the existing extension rule;
- package and encoded public-API version metadata remain at 1.0.1 and 1.0.0
  respectively until the 1.1 release-preparation task deliberately updates and
  requalifies them.

### Not yet included

- the manually verified final DJ acceptance corpus and its frozen evaluator
  verdict;
- physical ESP32-P4 memory, timing and coexistence evidence on real hardware;
- a `v1.1.0` tag or release artifacts.

## [1.0.1] - 2026-08-04

### Corrected

- aligned current normative component status labels, API/package documentation,
  governance and contribution guidance with the final APTA 1.0 contract;
- published APTA 1.0 errata 1 and refreshed the normative Git-blob manifest for
  the editorially corrected document set;
- corrected public conformance-suite and ESP-IDF component release metadata;
- replaced the one-off 1.0.0 publication workflow with a version-derived stable
  maintenance-release workflow.

### Compatibility boundary

- package version: `1.0.1`;
- public API version: `1.0.0`;
- specification version: stable `1.0`;
- container version: stable `1`;
- shared-library ABI: `SOVERSION 1`;
- no public symbol, structure layout, canonical fixture, wire rule, parser/writer
  behavior or production DSP contract changed;
- the `v1.0.0` tag and its original evidence remain immutable.

### Security and licensing

- `1.0.1` supersedes `1.0.0` as the maintained release;
- licensing remains Apache License 2.0 with no bundled third-party source tree
  and no project-specific NOTICE payload.

## [1.0.0] - 2026-08-04

### Finalized

- removed the release-candidate suffix from package metadata without changing the public API, ABI, container wire format or production analysis behavior;
- promoted the APTA specification and normative manifest from release candidate to final 1.0 status;
- added a final-release verifier that proves the P8 frozen contracts remain unchanged outside the explicitly approved version and publication files;
- added deterministic Linux and Windows final-package validation and governance-approved `v1.0.0` publication automation;
- published the final roadmap, security-policy and release documentation boundary.

### Compatibility boundary

- package version: `1.0.0`;
- public API version: `1.0.0`;
- specification version: stable `1.0`;
- container version: stable `1`;
- shared-library ABI: `SOVERSION 1`;
- the finalization changes no exported symbol, public structure layout, canonical fixture, conformance profile, parser/writer behavior or DSP result contract;
- desktop file adapters, the WAV reference decoder and CLI tools remain source components rather than stable exported package components;
- POSIX, Windows and ESP-IDF evidence represents platform integrations of libapta, not independent DSP implementations;
- exact qualification is published only for REFERENCE-WAVEFORM-1.0; no REFERENCE-TEMPO-1.0 or REFERENCE-BEATGRID-1.0 claim is made.

### Known limitations

- no supported native big-endian release target was available; deterministic byte-swap evidence is retained instead;
- hosted ESP-IDF CI compiles and links firmware and the strict interchange probe but does not claim execution on physical hardware;
- POSIX atomic replacement does not `fsync()` the parent directory after rename, an accepted power-loss durability limitation rather than a parser memory-safety defect;
- tempo and beatgrid selection accuracy remain implementation-quality evidence outside semantic conformance.

### Security and licensing

- `1.0.0` is the first maintained stable 1.x release under `SECURITY.md`;
- source and binary archives are distributed under Apache License 2.0 and include `LICENSE`, `VERSION` and this changelog;
- the core release package contains no bundled third-party source tree and no identified project-specific NOTICE payload.

## [1.0.0-rc.1] - 2026-08-04

### Added

- stable public C API and ABI version 1.0.0 with frozen public-header, exported-symbol and LP64/ILP32/LLP64 data-layout manifests;
- normative APTA specification 1.0 release-candidate set with a blob-hashed authority manifest;
- normative APTA container-version-1 registry and canonical, malformed and future-compatibility fixture manifests for META, WOVR, WDTL, TEMP, LGRD, GGRD and REVN;
- installable static and shared core packages with relocatable CMake metadata, pkg-config metadata, license/version/changelog payloads and reproducible source/binary archives;
- versioned installed-package public conformance suite for WAVEFORM-1.0, ADAPTIVE-WAVEFORM-1.0, CORE-ANALYSIS-1.0 and the optional REFERENCE-WAVEFORM-1.0 qualifier;
- independent full-feature producer and consumer evidence with byte-identical Linux, Windows and ILP32 interchange;
- ESP-IDF 5.5.4 and 6.0.2 firmware-build evidence for ESP32 and ESP32-S3 integrations;
- frozen nine-target Clang libFuzzer campaign, ASan/UBSan release evidence and security-review invariants.

### Changed

- package, API and specification version metadata now agree on the APTA 1.0 release-candidate contract while container version remains independently versioned at 1;
- the shared-library ABI filename policy is fixed at SOVERSION 1 for the 1.x line;
- API compatibility accepts the documented 1.x major/minor structure-prefix contract rather than exact encoded-version equality;
- the security policy names the active 1.0.0-rc.x line and the upcoming maintained 1.x line.

### Compatibility boundary

- package version: `1.0.0-rc.1`;
- public API version: `1.0.0`;
- specification version: `1.0` release candidate;
- container version: `1`;
- no public API, ABI, normative requirement or wire-format change is permitted after this freeze without resetting the release-candidate number and all affected evidence;
- desktop file adapters, the WAV reference decoder and CLI tools remain source components and are not exported as stable package components;
- POSIX, Windows and ESP-IDF reports prove platform integrations of libapta, not independent DSP implementations;
- exact qualification is published only for REFERENCE-WAVEFORM-1.0; no REFERENCE-TEMPO-1.0 or REFERENCE-BEATGRID-1.0 claim is made.

### Known limitations

- no supported native big-endian release target was available; deterministic byte-swap evidence is retained instead;
- hosted ESP-IDF CI compiles and links the strict interchange probe but does not claim execution on physical hardware;
- POSIX atomic replacement does not `fsync()` the parent directory after rename, an accepted power-loss durability limitation rather than a parser memory-safety defect;
- tempo and beatgrid selection accuracy remain implementation-quality evidence outside semantic conformance.

### Security and licensing

- the current `1.0.0-rc.x` release candidate is supported according to `SECURITY.md` until superseded;
- release source and binary archives are distributed under Apache License 2.0 and include the repository `LICENSE` file;
- no third-party source bundle or additional project-specific NOTICE obligation is included in the core release package.
