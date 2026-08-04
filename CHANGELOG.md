# Changelog

All notable changes to libapta-audio are documented in this file.

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
