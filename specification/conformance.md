# Conformance model

**Status:** APTA Working Draft 0.1

## 1. Purpose

The APTA conformance suite verifies interoperability, safety and claimed profile behaviour without requiring every implementation to use the same DSP algorithm.

A passing result applies only to the named implementation version, profile, platform, build configuration and fixture-manifest version.

## 2. Conformance layers

The suite is divided into:

1. public API compile conformance;
2. native ABI policy conformance;
3. behavioural API conformance;
4. semantic analysis conformance;
5. reference-algorithm conformance where defined;
6. container-format conformance;
7. malformed-input and fuzz conformance;
8. progressive scheduling conformance;
9. resource-class conformance.

An implementation MUST pass every layer required by its claim.

## 3. Public API compile conformance

Required checks include:

- all public headers compile as ISO C11;
- umbrella and individual headers compile independently where documented;
- public headers compile as C++11 or later under `extern "C"`;
- headers do not require operating-system, filesystem, USB or codec headers;
- public declarations are valid with shared-library visibility macros enabled and disabled;
- deprecated declarations follow the documented compatibility policy.

Warnings treated as errors SHOULD be enabled for the reference build.

## 4. ABI policy conformance

Before stable ABI status, the project records layout rather than promising permanence.

Required checks include:

- fixed-width scalar sizes;
- `struct_size` at offset zero;
- `api_version` at offset four for extensible structures;
- opaque object types remain incomplete in public headers;
- no public ABI-sensitive bit-fields;
- no C enum or C `bool` storage in ABI-sensitive structures;
- no packed native public structures;
- expected 32-bit and 64-bit layout manifests;
- symbol export and calling-convention checks on supported shared-library targets.

A layout manifest change requires review and an explicit compatibility decision.

## 5. Behavioural API conformance

Behavioural tests cover:

- configuration initializers;
- null and invalid-argument handling;
- unsupported-version handling;
- context and session creation/destruction;
- custom allocator success and failure;
- static-workspace boundaries;
- cancellation;
- PCM push partial acceptance and backpressure;
- PCM pull ownership and exactly-once release;
- explicit end of input;
- focus and region requests;
- request cancellation and status;
- immutable result acquire/release lifetime;
- diagnostics and failure scope.

Every output parameter MUST be checked for its documented state on success, positive status and error.

## 6. Semantic waveform conformance

Semantic waveform tests verify:

- source-frame geometry;
- half-open coverage;
- explicit gaps;
- column and tile identity;
- lifecycle behaviour;
- valid flags and normalized units;
- serialization round-trip;
- deterministic results independent of PCM block boundaries.

They do not require bit-identical optional three-band values unless a reference-filterbank qualifier is claimed.

## 7. Reference waveform conformance

The `REFERENCE-WAVEFORM-0.1` qualifier requires exact expected values for:

- mono and stereo reduction;
- peak quantization;
- RMS quantization;
- column boundaries;
- partial final columns;
- clipping flags;
- block-split independence.

Golden vectors MUST store source PCM generation parameters, expected packed columns and a fixture hash.

## 8. Tempo and beatgrid semantic conformance

Until a reference algorithm is normatively defined, tempo and beatgrid conformance verifies:

- units and ranges;
- evidence and applicability scope;
- lifecycle and confidence separation;
- explicit ambiguity;
- monotonic beat positions;
- ordinal continuity;
- fractional-frame encoding;
- segment and explicit representation authority;
- stable-range revision protection;
- serialization semantics when the corresponding section version exists.

Independent algorithms are not required to choose identical BPM or beat positions.

## 9. Numerical consistency tests

Where multiple fields represent the same underlying timing, the suite checks consistency.

For a constant-period grid segment:

```text
BPM = 60 * sample_rate / frames_per_beat
```

The profile defines permitted tolerance for the redundant milli-BPM value.

Fixed-point expansion tests MUST include long segments and compare direct ordinal multiplication against repeated period advancement without allowing unbounded accumulated rounding error.

## 10. Progressive scheduling conformance

A scheduling test uses a controlled source and records processing decisions and publication events.

Required cases include:

- local focus request before full-track overview completion;
- focus movement to a distant region;
- simultaneous low- and high-priority requests;
- a missing-PCM dependency;
- deadline ordering within one priority class;
- cancellation of one request sharing work with another;
- lower-priority starvation prevention;
- backpressure recovery;
- bounded processing using frame and step budgets.

A conforming implementation MUST demonstrate that a higher-priority runnable request is not repeatedly bypassed by lower-priority discretionary work.

`soft_time_budget_us` is evaluated statistically and diagnostically, not as a universal hard deadline.

## 11. Fixture manifest

Every fixture set has a machine-readable manifest containing:

- fixture identifier;
- fixture format and generation method;
- sample rate and channel layout;
- frame count;
- cryptographic hash of source bytes;
- redistribution license;
- expected test categories;
- exact expected data or tolerance rule;
- known ambiguity annotations;
- manifest schema version.

A test report MUST record the fixture-manifest hash.

## 12. Required audio fixtures

The suite SHOULD include legally redistributable examples covering:

- silence;
- impulses;
- clipped and low-level signals;
- stereo cancellation;
- click tracks from 40 to 300 BPM;
- half-time and double-time patterns;
- swing and syncopation;
- long intros and breakdowns;
- abrupt tempo changes;
- gradual tempo ramps;
- live drums;
- unknown-duration progressive input;
- PCM gaps and out-of-order delivery;
- 44.1 kHz and 48 kHz;
- mono and stereo.

Synthetic fixtures SHOULD be generated from checked-in source parameters rather than opaque binary-only files when practical.

## 13. Container conformance

Writer tests verify canonical output rules.

Reader tests verify acceptance of valid non-canonical ordering and safe skipping of unknown optional sections.

Round-trip tests verify preservation of:

- source identity fields;
- feature state;
- exact coverage;
- waveform packed values;
- confidence;
- provisional/final flags;
- unknown optional sections when lossless rewrite is claimed.

Cross-platform tests MUST include a file written on one architecture and read on another architecture with different native pointer width or endianness when such a platform is supported.

## 14. Malformed-input corpus

The mandatory negative corpus includes:

- every fixed-header truncation boundary;
- invalid magic and versions;
- invalid header and section CRC;
- arithmetic overflow attempts;
- directory outside file bounds;
- overlapping sections;
- unsupported required sections;
- excessive counts and sizes;
- non-zero reserved fields in strict mode;
- malformed metadata;
- invalid waveform spans and column bounds;
- duplicate tile identity;
- inconsistent tempo/period fields when those sections become normative.

The parser MUST fail safely without out-of-bounds access, unbounded allocation, use-after-free or integer wraparound.

## 15. Fuzzing

Fuzz targets SHOULD include:

- complete container parser;
- fixed-header parser;
- section directory parser;
- metadata parser;
- WOVR parser;
- WDTL parser;
- serializer/deserializer round-trip;
- PCM block/range validation;
- request state transitions.

Fuzzing MUST run with sanitizers on at least one supported host platform.

Crash inputs become permanent regression fixtures after minimization, subject to security-handling policy.

## 16. Allocation-failure testing

The reference test allocator can fail the Nth allocation.

For each API operation with allocations, tests SHOULD sweep failure points and verify:

- no leaks;
- no double free;
- session remains valid or transitions to a documented failure state;
- output parameters remain safe;
- existing immutable results remain valid;
- retry behaviour is documented.

## 17. Resource-class testing

A resource-class test records:

- peak heap and workspace usage;
- peak stack usage when measurable;
- maximum queued PCM bytes;
- retained result memory;
- process-call duration distribution;
- local publication latency;
- tested source duration and request workload;
- hardware, clock, compiler and optimization settings.

A class fails when its normative memory ceiling is exceeded under the declared conformance workload.

## 18. Test determinism

Test harnesses MUST distinguish:

- exact deterministic expectations;
- bounded numerical tolerance;
- semantic invariants;
- statistical performance measurements.

A flaky statistical threshold MUST NOT be presented as exact deterministic conformance.

Reference golden-vector generation MUST be reproducible from documented source and tool versions.

## 19. Conformance report

A report contains:

- implementation and version;
- source commit;
- claimed profile and resource class;
- optional qualifiers;
- platform and toolchain;
- build options;
- conformance-suite version;
- fixture-manifest hash;
- passed, failed and skipped tests;
- deviations;
- measured resource values;
- sanitizer and fuzzing summary.

Skipped mandatory tests invalidate the corresponding claim unless an approved platform exception is recorded.

## 20. Certification status

The project may publish self-tested conformance reports before an independent certification process exists.

A self-tested report MUST be labelled as such. Passing the reference suite does not grant trademark or certification rights unless project governance separately defines them.
