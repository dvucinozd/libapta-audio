# Stage S9 — APTA 1.0 freeze audit

**Status:** Phase 0 audit complete; no APTA 1.0 claim issued  
**Audit baseline:** `44bc78020eb00e51c70fb542550301a058e9133f`  
**Completed implementation stages:** S0 through S8  
**Current specification:** Working Draft 0.1  
**Current public API:** 0.3.0 draft  
**Current container:** version 1 implementation candidate

## 1. Purpose

This audit defines the exact boundary between the completed implementation
candidate and an APTA 1.0 release. It does not add DSP behaviour or change the
public API. Its purpose is to prevent a release-number bump from being mistaken
for specification, ABI, format or conformance stabilisation.

The audit examined:

- the S9 requirements in the architecture roadmap;
- the normative specification set;
- public API and ABI policy;
- public version declarations and structure validation;
- the canonical `.apta` format documents and current section inventory;
- conformance profiles and the current test model;
- Windows, POSIX and ESP-IDF integration evidence;
- packaging and release surfaces.

## 2. Executive conclusion

The implementation is functionally mature enough to begin APTA 1.0 work, but
it is not ready to be labelled 1.0 without a controlled freeze programme.

The strongest parts already exist:

- a portable ISO C11 core;
- progressive waveform, tempo, local-grid, global-grid and dynamic-tempo data;
- bounded cooperative scheduling;
- hardened canonical container parsing and writing;
- POSIX, Windows and ESP-IDF integrations;
- 32-bit, sanitizer, fuzz-smoke, malformed-input and allocation-failure tests;
- independent Python production of a valid WOVR/META fixture;
- bidirectional Linux/Windows container interchange;
- physical ESP32-P4 performance and memory measurements.

The remaining blockers are contractual rather than architectural:

1. consolidate the normative specification around what the implementation
   actually ships;
2. define and test the stable API/ABI compatibility rule;
3. freeze the complete version-1 container section set;
4. separate conformance tests and reports from implementation-only regression
   tests;
5. complete independent interchange evidence for the tempo/grid sections;
6. add installable shared/static package surfaces;
7. run and record release-candidate security, compatibility and release gates.

## 3. Version-domain audit

APTA correctly separates three version domains, but the repository currently
presents four different release-facing numbers:

| Domain | Current value | Freeze problem |
|---|---:|---|
| Specification | 0.1 | Still labelled Working Draft and does not fully incorporate S6/S8-era behaviour |
| Public API | 0.3.0 | Exact equality is currently required, so even a minor bump is a hard compatibility break |
| Container | 1 | Implemented and extensively tested, but its complete section set is not consolidated in one normative document |
| CMake project | 0.1.0 | Does not match the public API version and is not an installable package version |

The repository has no root `VERSION` file and no `CHANGELOG.md`. Those are not
required by the C language or container format, but they are required for one
unambiguous release identity.

### Freeze decision required

APTA 1.0 must publish one release version for the reference implementation while
retaining independent specification, API and container version fields.

Recommended 1.0 mapping:

```text
APTA Specification: 1.0
libapta API:         1.0.0
libapta package:     1.0.0
.apta container:     1
```

Container version 1 should remain version 1 unless the freeze work discovers a
byte-level incompatibility that cannot be handled by a section version. A
marketing release must not force a container-major change.

## 4. Normative specification audit

### 4.1. Master scope lags implementation

`APTA-SPEC.md` still describes APTA Core 0.1 and explicitly presents the public
headers as a non-stable prototype. That is correct today, but S9 must promote a
specific, reviewable document set to normative 1.0 status.

The 1.0 scope must explicitly include or exclude:

- global beatgrid;
- dynamic-tempo segments;
- explicit beat representation;
- locked-range revision proposals and application;
- session seeding from a parsed partial result;
- bounded immutable result slots;
- three-band overview waveform;
- platform-neutral desktop file adapter semantics.

Platform adapters and reference tools remain informative integrations, not core
normative requirements.

### 4.2. Container naming and authority conflict

The primary `specification/file-format.md` document still lists `BGRD` as the
reserved global-grid FourCC and `REVN` as reserved. The implementation and the
completed S6 interchange use canonical `GGRD` and `REVN` version-1 sections.
Separate S6 reference and readiness documents describe those bytes.

This is the most important specification inconsistency in the repository.
Changing the implementation from `GGRD` back to `BGRD` would create needless
format churn and invalidate existing fixtures. The 1.0 specification should
adopt the implemented `GGRD` and `REVN` contracts and remove the obsolete
`BGRD` reservation from normative examples.

### 4.3. Profile text contains historical conditions

`profiles.md` still contains conditional wording such as serialization being
qualified as waveform-only until TEMP/BGRD payloads become normative. TEMP and
LGRD are implemented, and GGRD/REVN now exist. Profile requirements must be
rewritten against the final section inventory rather than historical stage
gates.

### 4.4. Normative algorithm boundary

The project correctly distinguishes semantic conformance from bit-exact
reference-algorithm conformance. APTA 1.0 does not need to standardise the
current tempo estimator as the only valid algorithm.

Recommended 1.0 position:

- standardise waveform, tempo, beatgrid, lifecycle, units, ranges, revision and
  container semantics;
- retain exact reference-waveform qualification where vectors exist;
- do not issue `REFERENCE-TEMPO-1.0` or `REFERENCE-BEATGRID-1.0` until a
  normative algorithm and independently reproducible golden suite exist;
- document current Rekordbox corpus results as reference implementation quality
  evidence, not semantic conformance requirements.

This prevents the still-open high-confidence metrical-error research gate from
blocking the interoperable data-model standard.

## 5. Public API and ABI audit

### 5.1. Existing strengths

The public surface already follows most of the intended stable-ABI pattern:

- opaque context, session and result handles;
- fixed-width externally meaningful values;
- `struct_size` and `api_version` prefixes;
- initialiser functions for configuration and callback structures;
- `extern "C"` compatibility;
- explicit `APTA_API` and `APTA_CALL` macros;
- immutable acquired results;
- documented ownership and callback lifetime rules;
- 32-bit and 64-bit build coverage;
- C and C++ header compile checks.

### 5.2. Exact-version rejection is not a stable minor-version policy

The current validator compares `api_version` for exact equality. A program
compiled with 0.2 headers is rejected by a 0.3 library even when the structure
prefix and requested operation are otherwise compatible.

Before API 1.0, the implementation must define one explicit rule. Recommended:

- major versions must match;
- a library accepts a caller whose minor version is not newer than the library
  when the supplied `struct_size` contains every field required by the called
  operation;
- unknown trailing caller fields are ignored;
- a caller newer than the library receives `APTA_ERROR_INCOMPATIBLE_VERSION`
  unless a documented operation can safely accept its known prefix;
- patch versions never change ABI requirements;
- every public initializer writes the library's compile-time API version;
- compatibility behaviour is pinned by old-header/new-library tests.

The exact encoding may remain unchanged. What must change is the compatibility
predicate and its tests.

### 5.3. No frozen symbol and layout manifests

The project has compile and architecture-width coverage, but no release-level
manifest that declares:

- exported public symbol names;
- public structure size and field-offset expectations for supported ABI
  families;
- calling convention and symbol decoration on Windows;
- accidental symbol leakage from the shared library.

S9 must add machine-readable manifests and CI comparison. Intentional additions
must require an explicit manifest update; removals or incompatible layout
changes must fail the 1.x gate.

### 5.4. Shared-library and installation surface is absent

The root build creates `apta_core` as a static library. Public headers already
contain installation include expressions, but the root build has no complete:

- shared-library target validation;
- symbol export test;
- `install()` rules;
- exported CMake package configuration;
- package version file;
- `pkg-config` file;
- installed-consumer test.

APTA 1.0 should support both static and shared builds where the platform permits
them. The stable ABI claim applies only to the explicitly named shared-library
ABI families and calling conventions.

### 5.5. Result geometry and source identity

The parser restores source sample rate, channel count and length, and session
seeding checks those values. They are not exposed through `apta_result_info_t`,
so a host cannot inspect the same compatibility information before seeding.

More importantly, geometry is not audio identity. Two different tracks with the
same geometry can currently seed each other unless the host enforces its own
identity association.

Before 1.0, decide and document one of these contracts:

1. keep source identity entirely host-managed and state that seeding is unsafe
   without a host identity match; or
2. expose container identity and geometry through public result accessors and
   allow optional library-side identity enforcement.

Recommended: expose read-only geometry and fingerprint information while
retaining host policy over whether a missing or application-opaque fingerprint
is acceptable. Do not require a new mandatory decoded-audio hash in 1.0.

## 6. Container-format audit

### 6.1. Version-1 foundation is strong

The existing implementation already provides:

- a fixed 96-byte header;
- checked little-endian encoding;
- aligned section directory and payloads;
- CRC32C;
- strict bounds, overlap, count and allocation validation;
- canonical serialization;
- META, WOVR, WDTL, TEMP, LGRD, GGRD and REVN support;
- malformed, truncation, allocation-failure, sanitizer and fuzz-smoke coverage;
- writer-reader-writer byte identity for supported canonical content.

The correct default is to freeze this model, not redesign it.

### 6.2. Required S9 format decisions

The final file-format specification must state:

- the authoritative FourCC list and singleton/multiplicity rules;
- section-version compatibility and unknown optional-section behaviour;
- whether lossless rewrite of unknown optional sections is claimed;
- strict versus permissive reserved-field behaviour;
- exact cross-section dependencies;
- maximum normative counts versus configurable implementation limits;
- source fingerprint semantics;
- canonical ordering and byte-identity requirements;
- partial-result and unknown-duration legality for every section;
- migration rules for future container and section versions.

The reference parser currently need not preserve unknown optional sections
unless the project explicitly claims lossless rewrite. Recommended 1.0 policy:
accept and safely skip unknown optional sections, but do not claim preservation
through parse-to-result-to-serialize unless a dedicated opaque-section API is
added and tested.

### 6.3. Independent fixture coverage is incomplete

The independent Python fixture proves that another producer can create a valid
META/WOVR container without linking libapta. It does not independently encode
TEMP, LGRD, GGRD or REVN.

APTA 1.0 requires at least:

- one independent full-feature producer fixture;
- one libapta-produced full-feature fixture consumed by an independent parser;
- semantic checks for all section fields;
- canonical byte or declared tolerance checks;
- Linux, Windows and at least one different-width or embedded consumer path.

## 7. Conformance audit

### 7.1. Regression strength versus conformance packaging

The repository has a strong regression suite, but a stable conformance suite is
more than the test count in the reference implementation. It needs:

- versioned test-suite identity;
- machine-readable fixture manifest;
- profile selection;
- mandatory, optional and skipped-test classification;
- external implementation entry points;
- machine-readable result report;
- declared implementation, platform, compiler and build configuration;
- compatibility and deviation reporting.

Tests that include private headers or inspect private state remain implementation
regressions and must not be counted as portable public conformance tests.

### 7.2. Proposed 1.0 claims

Recommended initial claims:

```text
APTA-WAVEFORM-1.0
APTA-ADAPTIVE-WAVEFORM-1.0
APTA-CORE-ANALYSIS-1.0
+REFERENCE-WAVEFORM-1.0
```

Do not issue a reference-tempo or reference-beatgrid qualifier in the initial
1.0 release.

Resource classes should remain optional measured claims. No resource class
should be certified solely from host emulation. Existing ESP32-P4 measurements
may support a named self-tested report after its workload and limits are frozen.

### 7.3. Platform independence

Windows and ESP-IDF satisfy the roadmap requirement for two independent
platform integrations: neither adapter depends on the other and both consume
the public model. They are still integrations of the same reference library,
not independent DSP implementations.

The 1.0 release notes must not call them two independent implementations.
Stronger evidence should include an independently written fixture producer and
parser, plus actual `.apta` exchange with an ESP-IDF target or another consumer
application.

## 8. Security and fuzzing audit

Parser fuzz smoke, sanitizer execution and permanent malformed fixtures already
exist. S9 must turn the phrase “parser fuzzing” into a reproducible release
gate:

- named fuzz targets and seed manifest;
- fixed sanitizer/toolchain configurations;
- minimum campaign duration or execution count;
- retained crash and timeout corpus;
- zero known sanitizer findings;
- maximum input and allocation limits;
- recorded command lines and source commit;
- a release report rather than an informal statement that smoke passed.

A long campaign is a release-candidate gate, not a requirement for every pull
request.

## 9. Packaging and release audit

The following release surfaces are missing or not frozen:

- root `VERSION`;
- `CHANGELOG.md`;
- installable headers and libraries;
- CMake package config and version config;
- `pkg-config` metadata;
- installed static/shared consumer tests;
- Windows DLL import/export validation;
- release archive contents and checksums;
- source and binary artifact naming;
- API documentation generation or a declared documentation package;
- signed/tagged release procedure;
- compatibility support window for 1.x.

A legal review is already called out by the architecture. It remains a release
process gate, not an implementation task.

## 10. Scope recommended for APTA 1.0

Include:

- PCM push and pull;
- bounded cooperative processing and cancellation;
- focus and priority regions;
- immutable generations and diagnostics;
- static workspace and bounded result slots;
- overview, detail and optional three-band overview waveform;
- BPM candidates, local grid and confidence;
- global grid, dynamic-tempo segments, explicit beats and revisions;
- partial-result waveform seeding;
- META/WOVR/WDTL/TEMP/LGRD/GGRD/REVN container sections;
- semantic waveform and tempo/grid profiles;
- POSIX, Windows and ESP-IDF reference integrations.

Explicitly defer from the 1.0 core:

- musical key;
- downbeat/bar inference;
- phrase analysis;
- codec ownership;
- playback ownership;
- USB/filesystem ownership in the core;
- three-band detail tiles;
- bit-exact reference tempo/beatgrid conformance;
- mandatory source-content hashing;
- ARM64 Windows certification;
- universal hard-real-time guarantees.

## 11. Freeze blockers and disposition

| ID | Blocker | Required disposition before 1.0 RC |
|---|---|---|
| S9-B01 | Normative file-format document conflicts with implemented GGRD/REVN | Consolidate and approve the complete section contracts |
| S9-B02 | API minor versions are rejected by exact equality | Implement and test a stable compatibility predicate |
| S9-B03 | No frozen symbol/layout manifests | Add manifests and CI compatibility checks |
| S9-B04 | No install/shared/package surface | Add and test installed static/shared consumption |
| S9-B05 | Conformance is not independently packaged | Create versioned public suite, manifest and reports |
| S9-B06 | Independent fixture covers only META/WOVR | Add independent full-feature producer and consumer evidence |
| S9-B07 | No release fuzz-campaign record | Define and execute the RC campaign |
| S9-B08 | Version and release identity are fragmented | Add VERSION, changelog and version-source policy |
| S9-B09 | Result identity/geometry host contract is incomplete | Freeze and document the selected identity policy |
| S9-B10 | 1.0 profile and qualifier claims are not final | Approve named profiles and prohibited overclaims |

## 12. Readiness judgement

S9 may begin immediately. No architectural rewrite is required.

APTA 1.0 release readiness is reached only after every blocker above is either:

- closed by implementation, normative text and tests; or
- explicitly deferred by a documented scope decision that leaves the stable
  contract internally consistent.

The implementation must remain behaviourally frozen during the final release
candidate except for defects required to satisfy a failed conformance,
compatibility, security or packaging gate.
