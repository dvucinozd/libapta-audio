# APTA 1.0 implementation work order

**Status:** proposed execution order  
**Parent stage:** S9 — APTA 1.0  
**Audit:** [`../status/S9-FREEZE-AUDIT.md`](../status/S9-FREEZE-AUDIT.md)  
**Starting baseline:** `44bc78020eb00e51c70fb542550301a058e9133f`

## 1. Objective

Produce the first stable APTA specification, public API/ABI, version-1 `.apta`
format, installable reference implementation and versioned conformance suite
without adding unrelated analysis features.

This work order treats 1.0 as a compatibility and evidence programme. It is not
a request to redesign the completed S0–S8 implementation.

## 2. Global execution rules

1. Use one branch and one pull request per phase unless a phase explicitly
   requires smaller independently reviewable changes.
2. Every compatibility-affecting decision must be recorded in normative text
   before or in the same pull request as implementation changes.
3. No new key, downbeat, phrase, codec, playback, USB or UI feature enters S9.
4. Do not rename an implemented container FourCC merely to match an obsolete
   draft example. Normative text follows the approved stable wire contract.
5. Preserve canonical writer byte output unless a reviewed format defect makes
   a change unavoidable.
6. Any unavoidable pre-1.0 byte change requires fixture regeneration, a format
   decision record and compatibility impact statement.
7. Private implementation regressions and public conformance tests remain
   distinct.
8. Warnings-as-errors, 32-bit, Windows, sanitizers, fuzz smoke, ESP-IDF and
   independent fixture workflows remain required throughout S9.
9. After the release-candidate freeze, production behaviour changes only to fix
   a failed compatibility, conformance, security or packaging gate.
10. Do not claim an independently implemented DSP engine when the evidence is
    multiple integrations of `libapta`.

## 3. Phase sequence

```text
P0  Freeze audit                                  COMPLETE
P1  Normative authority and 1.0 scope
P2  Version model and public API/ABI freeze
P3  Container v1 normative freeze
P4  Installable static/shared package
P5  Public conformance suite and reports
P6  Independent interchange and platform evidence
P7  Security and fuzz release campaign
P8  Release candidate freeze
P9  APTA 1.0 release
```

Phases P1 through P4 define the contract. P5 through P7 prove it. P8 freezes it.
P9 publishes it.

---

## P0 — Freeze audit

**Status:** complete in the S9 audit package.

Deliverables:

- `docs/status/S9-FREEZE-AUDIT.md`;
- blocker inventory S9-B01 through S9-B10;
- recommended 1.0 scope;
- explicit separation between semantic and reference-algorithm conformance.

Exit gate:

- audit merged without production-code changes;
- S9 work proceeds from the approved blocker inventory.

---

## P1 — Normative authority and 1.0 scope

**Branch:** `agent/s9-p1-normative-scope`  
**Change class:** specification and documentation only

### P1.1. Establish the normative document set

Tasks:

- change the master specification from Working Draft 0.1 to a 1.0 release
  candidate state, not final 1.0 yet;
- enumerate every normative document and its version;
- mark architecture, status, readiness and reference documents as informative;
- add a normative-document manifest containing path, title, version and hash;
- define how editorial corrections differ from normative changes.

Acceptance:

- one manifest is the authority for the complete 1.0 normative set;
- every normative link resolves;
- no informative status document can silently override normative text.

### P1.2. Freeze feature scope

Tasks:

- approve inclusion of push/pull PCM, bounded processing, cancellation, focus,
  immutable results, static workspaces and bounded result slots;
- approve overview/detail/three-band-overview waveform scope;
- approve BPM, candidates, local grid, confidence, locking, global grid,
  dynamic tempo, explicit beats and revisions;
- approve partial waveform seeding;
- explicitly defer key, downbeat, phrase, codec/playback/USB ownership,
  three-band detail and mandatory content hashing.

Acceptance:

- every public capability bit is either part of a named 1.0 profile or clearly
  identified as an optional/deferred extension;
- README, master spec, profiles and architecture status use the same scope.

### P1.3. Approve initial conformance claims

Recommended identifiers:

```text
APTA-WAVEFORM-1.0
APTA-ADAPTIVE-WAVEFORM-1.0
APTA-CORE-ANALYSIS-1.0
+REFERENCE-WAVEFORM-1.0
```

Tasks:

- remove historical conditions referring to TEMP/BGRD becoming normative;
- define mandatory and optional capabilities for each profile;
- state that reference tempo and beatgrid qualifiers are unavailable in 1.0;
- retain resource classes as optional measured claims.

Acceptance:

- profile names, dependencies and permitted claims are unambiguous;
- no current quality metric is misrepresented as semantic conformance.

### P1.4. Resolve named specification conflicts

Tasks:

- adopt `GGRD` and `REVN` as the implemented section names;
- remove obsolete normative `BGRD` examples or clearly reserve them as unused;
- align TEMP/LGRD/GGRD/REVN terminology across file-format, beatgrid,
  lifecycle, profiles and conformance documents;
- align partial/final and unknown-duration rules.

Acceptance:

- repository search finds no contradictory normative FourCC table;
- all current writer section codes have one normative definition.

### P1 exit gate

- documentation/link checks pass;
- an explicit maintainer approval records the 1.0 scope and profile set;
- no code changed.

---

## P2 — Version model and public API/ABI freeze

**Branch:** `agent/s9-p2-api-abi-freeze`  
**Dependencies:** P1

### P2.1. Create one release-version source

Tasks:

- add root `VERSION` with `1.0.0-rc.1` during the candidate cycle;
- derive CMake project/package version from the authoritative source or pin it
  through a consistency check;
- update public API and specification macros deliberately;
- add CI that fails when VERSION, CMake and public package version disagree;
- retain container version as an independent constant.

Acceptance:

- one command/report prints package, API, specification and container versions;
- no contradictory version appears in generated package metadata.

### P2.2. Implement stable API compatibility

Tasks:

- replace exact API-version equality with an approved major/minor/patch
  compatibility predicate;
- retain `struct_size` prefix validation for every field read;
- reject unsupported newer required fields safely;
- document old-caller/new-library and new-caller/old-library behaviour;
- add a checked-in 1.0 public-header snapshot used to compile compatibility
  clients against later 1.x library builds;
- test same-major older-minor acceptance and major-version rejection.

Recommended rule:

- major must match;
- older or equal caller minor is accepted when the required structure prefix is
  present;
- patch does not change the ABI requirement;
- a newer caller minor is rejected unless an API-specific documented prefix
  rule permits it.

Acceptance:

- compatibility matrix tests pass on Linux 64-bit, Linux 32-bit and Windows;
- existing invalid-version tests are updated rather than weakened;
- no structure read can pass beyond caller-provided `struct_size`.

### P2.3. Freeze public structure layouts

Tasks:

- generate machine-readable manifests for every extensible public structure;
- record size, alignment and field offsets for supported 32-bit and 64-bit ABI
  families;
- cover Windows calling-convention-sensitive declarations;
- fail CI on unapproved layout drift;
- document which structures are data-transfer views and which are caller-owned
  configurations.

Acceptance:

- layout manifests are reproducible from public headers only;
- all fields stored in ABI-sensitive structures use approved types;
- manifest updates require an explicit compatibility note.

### P2.4. Freeze the public symbol surface

Tasks:

- generate a sorted public symbol manifest;
- ensure every exported function uses `APTA_API` and `APTA_CALL`;
- hide private symbols in shared builds;
- add ELF and PE/COFF export inspection tests;
- define deprecation macros and the 1.x removal policy.

Acceptance:

- static and shared builds expose the same documented public API;
- no private `apta_internal_*` symbol is exported;
- removal of a 1.x public symbol fails CI.

### P2.5. Complete result source information contract

Tasks:

- expose read-only source geometry through a stable result accessor or approved
  result-info extension;
- expose fingerprint kind and bytes when present;
- document seeding behaviour for absent, opaque, matching and mismatching
  identities;
- retain host control over whether missing identity is acceptable;
- add seeding identity/geometry tests.

Acceptance:

- hosts can inspect every compatibility value the library itself uses;
- the API does not imply that equal geometry proves equal audio.

### P2 exit gate

- API version is `1.0.0-rc.1` or the agreed RC value;
- API compatibility, layout and symbol manifests pass on every native CI job;
- C and C++ installed-header compilation passes;
- public API additions after this phase require an S9 freeze exception.

---

## P3 — Container version-1 normative freeze

**Branch:** `agent/s9-p3-container-v1-freeze`  
**Dependencies:** P1; coordinate public identity access with P2

### P3.1. Consolidate the byte-level specification

Tasks:

- move or incorporate GGRD and REVN version-1 contracts into the normative
  file-format set;
- define META, WOVR, WDTL, TEMP, LGRD, GGRD and REVN in one authoritative
  section registry;
- define section multiplicity, ordering, dependencies and version handling;
- state exact strict/permissive reserved-field behaviour;
- state partial-result and unknown-duration legality per section;
- define canonical output and non-canonical valid input separately.

Acceptance:

- every byte emitted by the canonical writer has a normative field definition;
- every parser cross-section rejection has a normative rule;
- independent implementers need no private source or status document to decode
  a full-feature file.

### P3.2. Freeze future compatibility policy

Tasks:

- define container-major versus section-version changes;
- define unknown optional and unknown required section handling;
- explicitly decline lossless unknown-section preservation unless an opaque
  preservation API is implemented;
- define extended-header compatibility;
- define migration expectations for future canonical writers.

Acceptance:

- version-1 readers can determine whether a future file is rejectable,
  partially readable or safely skippable;
- release documentation states what a 1.x writer may add without changing
  container version.

### P3.3. Pin canonical fixtures

Tasks:

- retain the existing independent WOVR/META fixture;
- add canonical libapta fixtures for every legal section combination;
- add malformed boundary fixtures for each section;
- add a complete full-feature canonical fixture with all current sections;
- generate hashes and a versioned fixture manifest.

Acceptance:

- writer output is byte-identical on Linux and Windows;
- parser-writer byte identity passes for every canonical fixture;
- every fixture records producer, schema version and SHA-256.

### P3 exit gate

- normative format documents and implementation agree;
- no unreviewed byte output changed;
- complete canonical and malformed manifests pass on 32/64-bit, Windows and
  sanitizer jobs;
- container version remains 1 unless a separately approved incompatibility
  report proves a major bump necessary.

---

## P4 — Installable static/shared package

**Branch:** `agent/s9-p4-packaging`  
**Dependencies:** P2

### P4.1. Native CMake package

Tasks:

- support static and shared `apta_core` builds;
- add install rules for public headers and libraries;
- export stable targets under the `apta::` namespace;
- generate `APTAConfig.cmake` and `APTAConfigVersion.cmake`;
- preserve component separation for desktop adapters and tools;
- add build-tree and install-tree consumer projects.

Acceptance:

```cmake
find_package(APTA 1.0 CONFIG REQUIRED)
target_link_libraries(consumer PRIVATE apta::core)
```

works from a clean external source tree for static and shared installations.

### P4.2. pkg-config and platform packages

Tasks:

- generate `libapta.pc` for core;
- document Windows import-library/DLL layout;
- document source-vendoring and ESP-IDF component paths;
- decide whether adapters have separate package components;
- ensure experimental/internal options do not leak into installed interfaces.

Acceptance:

- `pkg-config --cflags --libs libapta` builds the external C consumer;
- Windows external consumer loads the DLL and calls the public smoke API;
- installed headers contain no source-tree-relative includes.

### P4.3. Package version and ABI metadata

Tasks:

- set shared-library version/SOVERSION policy;
- define debug/release artifact names;
- include license, changelog and version metadata in release archives;
- add checksum generation in release workflow preparation.

Acceptance:

- installed package version matches root VERSION;
- the 1.x ABI filename policy is documented and tested.

### P4 exit gate

- clean installed-consumer tests pass on Linux and Windows;
- static/shared symbol manifests pass;
- source and binary package contents are reproducible enough for release
  verification;
- no direct source-tree include is needed by consumers.

---

## P5 — Public conformance suite and reports

**Branch:** `agent/s9-p5-conformance-suite`  
**Dependencies:** P1, P2, P3, P4

### P5.1. Separate public conformance from private regression

Tasks:

- classify every existing test as public conformance, reference qualifier,
  implementation regression, performance evidence or fuzz/security;
- move or wrap public tests so they consume only installed public headers and
  libraries;
- prohibit private-header access in the public suite;
- retain private white-box tests in the normal implementation CI.

Acceptance:

- the conformance suite runs against an installed libapta package;
- another implementation can supply a compatible adapter without compiling
  libapta private sources.

### P5.2. Versioned suite and fixture manifest

Tasks:

- add conformance suite versioning;
- define machine-readable test and fixture manifests;
- mark tests mandatory, optional, qualifier-specific or platform-exempt;
- record fixture hashes and generators;
- provide deterministic JSON report output.

Required report fields:

- implementation/version and source revision;
- claimed profile and qualifiers;
- platform/toolchain/build options;
- suite and fixture-manifest versions/hashes;
- pass/fail/skip counts;
- deviations and platform exceptions;
- sanitizer/fuzz evidence references where applicable.

Acceptance:

- skipped mandatory tests invalidate the corresponding profile claim;
- the same report schema is produced on Linux, Windows and ESP-IDF integration
  runs where supported.

### P5.3. Profile gates

Tasks:

- implement gates for WAVEFORM-1.0, ADAPTIVE-WAVEFORM-1.0 and
  CORE-ANALYSIS-1.0;
- implement exact REFERENCE-WAVEFORM-1.0 vectors;
- keep tempo/grid selection accuracy outside semantic conformance;
- test semantic consistency, ranges, lifecycle, revision and serialization.

Acceptance:

- each claim has an explicit mandatory test set;
- no unavailable reference-tempo qualifier can be emitted.

### P5 exit gate

- reference implementation passes every claimed profile from an installed
  package;
- JSON reports are retained as CI artifacts;
- public suite documentation is sufficient for a third-party implementation.

---

## P6 — Independent interchange and platform evidence

**Branch:** `agent/s9-p6-interoperability`  
**Dependencies:** P3, P5

### P6.1. Independent full-feature producer

Tasks:

- extend or create a producer that does not import, link or invoke libapta;
- encode META, WOVR, WDTL, TEMP, LGRD, GGRD and REVN from explicit values;
- publish source, manifest and hashes;
- validate and semantically inspect the fixture through libapta on Linux and
  Windows.

Acceptance:

- libapta accepts and byte-reproduces the canonical independent fixture;
- every field has an independent expected-value assertion.

### P6.2. Independent consumer

Tasks:

- implement a small parser/validator independent of libapta or use a separately
  reviewed language implementation;
- consume a libapta full-feature canonical fixture;
- verify header, directory, CRC and section semantics;
- ensure the implementation is not generated by copying private C parser code.

Acceptance:

- independent consumer output matches the canonical semantic manifest;
- failures identify exact format misunderstandings rather than relying only on
  byte equality.

### P6.3. Platform exchange

Tasks:

- retain Linux↔Windows exchange;
- execute container consumption or production on an ESP-IDF target where
  practical;
- include a 32-bit consumer path;
- add cross-endian evidence when a supported target or deterministic endian
  harness is available, otherwise record it as a declared 1.0 limitation.

Acceptance:

- at least two independent platform integrations pass the applicable public
  conformance profile;
- release text distinguishes platforms from independent DSP implementations.

### P6 exit gate

- independent producer and consumer evidence passes;
- Windows and ESP-IDF integration reports use the versioned suite;
- all interoperability artifacts and hashes are retained.

---

## P7 — Security and fuzz release campaign

**Branch:** `agent/s9-p7-security-campaign` or evidence-only branch  
**Dependencies:** P3, P5

### P7.1. Freeze fuzz target inventory

Required targets:

- complete container parser;
- fixed header and section directory;
- META, WOVR, WDTL, TEMP/LGRD and GGRD/REVN readers;
- serializer/parse round trip;
- PCM block/range validation;
- request and revision state transitions where practical.

Tasks:

- version seed corpora;
- define timeout, memory and maximum-input limits;
- run ASan/UBSan and the supported fuzz engine;
- minimize and retain every discovered crash/regression input.

### P7.2. Release campaign

The maintainer must approve a reproducible threshold, for example:

- fixed minimum executions per target; or
- fixed wall-clock duration per target on declared hardware;
- zero sanitizer findings, crashes, hangs and unbounded allocations;
- complete command lines, compiler versions and source revision recorded.

The exact threshold is a project policy decision; it must not be invented by
individual CI runs.

### P7.3. Security review

Tasks:

- review all external size/count arithmetic and allocation-limit paths;
- review atomic file replacement and temporary-file cleanup;
- review UTF-8/UTF-16 conversion bounds on Windows;
- verify SECURITY.md release contact and supported-version policy;
- resolve or explicitly accept every known security deviation.

### P7 exit gate

- campaign report merged;
- no unresolved known parser memory-safety defect;
- minimized corpus retained;
- supported-version security policy names the upcoming 1.0 line.

---

## P8 — Release candidate freeze

**Branch:** `release/1.0.0-rc.1`  
**Dependencies:** P1 through P7

Tasks:

- set version to `1.0.0-rc.1`;
- create `CHANGELOG.md` with the complete compatibility boundary;
- freeze normative document hashes;
- freeze public symbol/layout manifests;
- freeze canonical fixture and conformance manifests;
- run complete native, Windows, 32-bit, sanitizer, fuzz, ESP-IDF,
  interoperability, installed-consumer and package gates;
- generate release archives and checksums;
- perform legal/license review;
- publish RC artifacts without a final 1.0 conformance overclaim.

RC change policy:

- documentation clarifications that do not alter requirements are permitted;
- production changes require a recorded failed gate;
- public API or wire-format change resets the RC number and affected evidence;
- new features are prohibited.

Exit gate:

- all required checks pass from the RC commit;
- no S9 blocker remains open;
- the maintainer approves finalisation.

---

## P9 — APTA 1.0 release

**Branch:** final release preparation from the accepted RC

Tasks:

- remove the RC suffix without changing API, ABI or wire behaviour;
- set specification to 1.0 final;
- regenerate version-bound package metadata only;
- rerun deterministic compatibility and package checks;
- create the signed or otherwise governance-approved `v1.0.0` tag;
- publish source archives, supported binaries, checksums, changelog,
  conformance reports and normative document manifest;
- update roadmap status to S9 complete;
- open the 1.1 planning boundary separately.

Exit gate:

```text
Specification: stable 1.0
API/package:   stable 1.0.0
Container:     stable version 1
Profiles:      named 1.0 claims with retained reports
Platforms:     POSIX, Windows and ESP-IDF evidence recorded
Security:      release campaign passed
Compatibility: symbol, layout, fixture and installed-consumer gates passed
```

## 4. Blocker-to-phase mapping

| Audit blocker | Closing phase |
|---|---|
| S9-B01 GGRD/REVN normative conflict | P1, P3 |
| S9-B02 exact API-version equality | P2 |
| S9-B03 missing symbol/layout manifests | P2 |
| S9-B04 missing install/shared package | P4 |
| S9-B05 conformance not independently packaged | P5 |
| S9-B06 partial independent fixture coverage | P6 |
| S9-B07 no release fuzz campaign | P7 |
| S9-B08 fragmented release identity | P2, P8 |
| S9-B09 incomplete result identity contract | P2, P3 |
| S9-B10 unapproved profile claims | P1, P5 |

## 5. Required continuous gates

Every implementation phase must preserve:

- default POSIX runtime suite;
- core-only build and tests;
- Linux 32-bit core build and tests;
- Linux Clang ASan/UBSan tests;
- Windows/MSVC warnings-as-errors build and tests;
- independent reference fixture workflow;
- supported ESP-IDF firmware matrix;
- `git diff --check` and documentation link checks.

Phases add gates cumulatively. A later phase must not remove an earlier phase's
gate merely to obtain a green result.

## 6. Completion rule

S9 is complete only when P9 is published. Completing the audit, specification
rewrite, package work or conformance suite individually does not authorise an
APTA 1.0 claim.
