# Stage S9 P2 — Public API and ABI freeze status

**Status:** complete; all P2 exit gates passed  
**Branch:** `agent/s9-p2-api-abi-freeze`  
**Validated head:** `59e0fff80b28f9d297f7968909da7aa60defe220`  
**Work order:** [`../roadmap/APTA-1.0-WORK-ORDER.md`](../roadmap/APTA-1.0-WORK-ORDER.md)

## Frozen version model

- package version source: root `VERSION` = `1.0.0-rc.1`;
- public API version: `1.0.0`;
- specification version: `1.0`;
- container version: `1`, retained as an independent compatibility axis;
- CMake configuration checks reject disagreement between the authoritative
  package version, project metadata and public version macros;
- `apta-version` reports package, API, specification and container versions in
  one machine-readable line.

## API compatibility contract

- API major versions must match;
- callers using an older or equal minor version are accepted when the required
  `struct_size` prefix is present;
- patch differences do not change ABI requirements;
- unsupported newer caller minors and different majors are rejected;
- every extensible structure remains guarded by `struct_size` and
  `api_version` before fields are read;
- the checked-in `tests/compat/1.0.0/include/apta` snapshot compiles and runs an
  old-header/new-library client against current static and shared builds.

## Frozen structure and symbol surface

- machine-readable public layout manifests are checked in for LP64, ILP32 and
  LLP64 data models;
- CI compares sizes, alignments and field offsets generated from public headers
  only;
- `abi/public-symbols-1.0.txt` is the authoritative sorted public symbol list;
- ELF shared builds use the frozen version script and export no private
  `apta_internal_*` or composition-layer symbols;
- PE/COFF builds use explicit MSVC exports and are inspected from the produced
  DLL;
- shared-library tests verify exact symbol equality, not only symbol presence;
- the public headers define deprecation and calling-convention macros for the
  1.x compatibility line.

## Source information and checkpoint identity

- `apta_result_get_source_info()` exposes sample rate, channel count, channel
  layout, total frames, fingerprint kind and fingerprint bytes;
- source identity is serialized and parsed through container version 1;
- missing identity remains host policy by default;
- `APTA_SESSION_FLAG_REQUIRE_SOURCE_IDENTITY_FOR_SEEDING` provides an explicit
  strict policy;
- absent, matching and mismatching identity cases and geometry conflicts are
  covered by regression tests;
- equal geometry is never treated as proof that two decoded audio sources are
  identical.

## Validation evidence

All checks passed at validated head `59e0fff80b28f9d297f7968909da7aa60defe220`:

- GitHub Actions CI run `30845312071`:
  - Linux 64-bit static tests and shared ABI gates;
  - Linux 32-bit ILP32 build and tests;
  - Windows static tests and shared DLL ABI gates;
  - parser sanitizers and bounded fuzz smoke;
- reference-fixture run `30845312056`:
  - independent fixture generation, manifest validation, parsing and
    byte-identical reproduction;
- ESP-IDF run `30845312038`:
  - bounded memory profiles;
  - ESP-IDF 5.5.4 / ESP32 scalar firmware;
  - ESP-IDF 6.0.2 / ESP32 scalar firmware;
  - ESP-IDF 6.0.2 / ESP32-S3 ESP-DSP firmware.

The native ABI matrix covers C/C++ header compilation, LP64, ILP32 and LLP64
layouts, old-header/new-library runtime compatibility and exact ELF/PE export
surfaces.

## P2 exit decision

P2 is complete. The APTA 1.0 release-candidate public API and ABI are frozen.
Any later S9 public API addition, removal, layout change or exported-symbol
change requires an explicit freeze exception with compatibility rationale and
updated manifests.

No analysis algorithm, canonical waveform/tempo/grid payload or container
section semantics changed in P2.

## Next phase

P3 freezes container version 1: the complete byte-level section registry,
future compatibility policy, canonical fixtures and independent full-feature
interchange authority.
