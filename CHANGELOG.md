# Changelog

All notable changes to libapta-audio are documented in this file.

## [1.0.0-rc.1] - 2026-08-03

### Added

- stable public C API and ABI version 1.0.0 with frozen header, symbol and data-layout manifests;
- normative APTA container-version-1 registry and canonical/malformed conformance fixtures;
- installable static and shared core packages with CMake and pkg-config metadata;
- clean external-consumer verification on Linux, ILP32 and Windows.

### Changed

- package, API and specification version metadata now agree on the APTA 1.0 release-candidate contract;
- the shared-library ABI filename policy is fixed at SOVERSION 1 for the 1.x line.

### Compatibility

- container version remains 1;
- desktop adapters, the WAV reference decoder and CLI tools are not exported as stable package components in this release candidate.
