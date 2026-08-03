# Conformance profiles and resource classes

**Status:** APTA 1.0 Release Candidate Draft

## 1. Purpose

APTA separates functional capability profiles from resource classes.

A capability profile defines observable behaviour, public data models and
interchange requirements. A resource class defines measured memory and
execution limits. A resource class does not add analysis features.

A conformance claim MUST identify every profile, optional qualifier, platform,
source limit and resource class it claims.

## 2. Common APTA 1.0 baseline

Every APTA 1.0 profile requires:

- source-frame time and half-open ranges;
- fixed-width public value semantics;
- immutable result generations;
- lifecycle and confidence rules for supported features;
- safe rejection of unsupported feature requests;
- bounded allocation according to configured limits;
- safe parsing of untrusted `.apta` data when container support is claimed;
- no mandatory ownership of codecs, filesystems, USB, networking, playback,
  operating-system workers or user interfaces by the portable core.

A profile claim covers only the named implementation version, platform, build
configuration and source limits recorded by the conformance report.

## 3. APTA Waveform Profile 1.0

Identifier:

```text
APTA-WAVEFORM-1.0
```

Required capabilities:

- PCM push input;
- explicit end-of-input;
- overview waveform;
- exact analysed coverage and explicit gaps;
- immutable waveform snapshots;
- semantic waveform conformance;
- version-1 `WOVR` serialization and parsing;
- canonical writer and malformed-container conformance for required waveform
  cases.

Optional capabilities:

- PCM pull input;
- detail tiles and version-1 `WDTL`;
- three-band overview waveform;
- partial `.apta` results;
- session seeding from compatible parsed waveform coverage;
- `+REFERENCE-WAVEFORM-1.0`.

Three-band detail tiles are not required by APTA 1.0.

## 4. APTA Adaptive Waveform Profile 1.0

Identifier:

```text
APTA-ADAPTIVE-WAVEFORM-1.0
```

This profile includes every `APTA-WAVEFORM-1.0` requirement and additionally
requires:

- playback focus;
- explicit priority-region requests;
- bounded cooperative processing;
- push-mode PCM demand or equivalent documented pull-mode demand;
- local publication before unrelated full-track completion when required input
  is available;
- priority preservation;
- documented starvation prevention;
- bounded detail retention or eviction policy when detail is claimed;
- progressive request status;
- cancellation that preserves already acquired immutable results.

Moving focus MUST change future scheduling priority without invalidating
already published stable output.

## 5. APTA Core Analysis Profile 1.0

Identifier:

```text
APTA-CORE-ANALYSIS-1.0
```

This profile includes every `APTA-ADAPTIVE-WAVEFORM-1.0` requirement and
additionally requires:

- BPM in milli-BPM units;
- ordered tempo candidates or explicit ambiguity flags;
- local beatgrid with fractional-frame timing;
- evidence, applicability and coverage ranges;
- per-range lifecycle and confidence;
- stable-range locking and revision protection;
- global beatgrid;
- dynamic-tempo segment representation;
- explicit-beat and hybrid representations when required by the result;
- immutable pending/applied revision metadata;
- version-1 `TEMP`, `LGRD`, `GGRD` and `REVN` interchange;
- semantic tempo, beatgrid, revision and cross-section conformance.

A conforming implementation MAY use a different DSP algorithm. Semantic
conformance does not require identical selected BPM, confidence or beat
positions unless an available reference-algorithm qualifier is also claimed.

## 6. Capability dependencies

The following dependencies apply to the 1.0 reference public capability model:

- `WAVEFORM_DETAIL` requires `WAVEFORM_OVERVIEW`;
- `WAVEFORM_3BAND` requires `WAVEFORM_OVERVIEW`;
- `LOCAL_BEATGRID` requires `BPM`;
- `GLOBAL_BEATGRID` requires `BPM`;
- `DYNAMIC_TEMPO` requires `GLOBAL_BEATGRID`;
- `GRID_LOCKING` requires a beatgrid capability;
- serialized `LGRD` requires `TEMP`;
- serialized `GGRD` and `REVN` form a required pair when either is present.

An implementation MUST reject or clearly report a request whose mandatory
dependency is unavailable.

## 7. Features deferred from the 1.0 core

The following are not requirements of any APTA 1.0 core profile:

- musical key or key-change analysis;
- downbeat, meter or bar classification;
- phrase or section classification;
- proprietary cue, playlist or library formats;
- audio decoding;
- audio playback or real-time output;
- USB, filesystem or network ownership by the portable core;
- three-band detail tiles;
- mandatory decoded-audio content hashing;
- universal hard-real-time guarantees.

They require a separately versioned extension, platform contract or
application policy.

## 8. Reference algorithm qualifiers

Functional profiles do not generally require one DSP algorithm.

### 8.1. Reference waveform

Identifier:

```text
+REFERENCE-WAVEFORM-1.0
```

This qualifier requires exact reference vectors for:

- mono and stereo reduction;
- minimum and maximum quantization;
- RMS quantization;
- column boundaries;
- partial final columns;
- clipping flags;
- block-split independence.

Three-band values are excluded unless a later qualifier explicitly defines a
bit-exact filterbank.

### 8.2. Tempo and beatgrid

APTA 1.0 does not define these qualifiers:

```text
+REFERENCE-TEMPO-1.0
+REFERENCE-BEATGRID-1.0
```

An implementation MUST NOT claim them. Current reference implementation corpus
results are quality evidence, not semantic conformance requirements.

## 9. Resource measurement rules

A resource claim records at least:

- context and session allocations;
- static workspace;
- queued PCM copies;
- internal DSP state;
- unpublished work state;
- one current result generation retained by the session;
- whether additional application-retained generations are included;
- stack use when measurable;
- source duration, format, block size, feature set and scheduling workload;
- hardware, clock, compiler and optimization settings;
- process-call duration distribution and publication latency where claimed.

A host-only emulation MUST NOT be presented as physical-target resource
certification.

## 10. Resource class R0 — Static 128

Identifier:

```text
APTA-R0-STATIC-128K
```

Requirements:

- no heap allocation after successful session creation;
- maximum supplied session workspace: 131,072 bytes;
- cooperative single-thread processing;
- documented PCM block, duration, waveform-column and retained-result limits;
- rejection of configurations that do not fit rather than hidden allocation
  outside the declared workspace.

Waveform-only capability is permitted.

## 11. Resource class R1 — Embedded 512

Identifier:

```text
APTA-R1-EMBEDDED-512K
```

Requirements:

- peak declared session memory no greater than 524,288 bytes under the exact
  conformance workload;
- bounded cooperative processing;
- no mandatory operating-system worker;
- documented internal/external memory policy;
- documented PCM queue, duration and retained-result limits.

A Core Analysis claim is valid only if its complete declared feature set fits.

## 12. Resource class R2 — Embedded 4M

Identifier:

```text
APTA-R2-EMBEDDED-4M
```

Requirements:

- peak declared session memory no greater than 4,194,304 bytes;
- adaptive waveform;
- sufficient memory for every claimed local/global analysis feature;
- documented internal, external and DMA-capable memory use;
- measured process-call distribution and publication latency.

## 13. Resource class R3 — General purpose

Identifier:

```text
APTA-R3-GENERAL
```

R3 has no normative low-memory ceiling, but an implementation MUST still:

- enforce configured allocation limits;
- report measured peak memory for the declared workload;
- reject untrusted container sizes before unbounded allocation;
- document worker and threading behaviour.

## 14. Responsiveness qualifiers

A resource claim may include:

```text
COOPERATIVE
INTERACTIVE
PLAYBACK-AWARE
```

`COOPERATIVE` means the host controls every processing call and the portable
core creates no mandatory worker.

`INTERACTIVE` means the named workload publishes requested local output within
a documented percentile latency on declared hardware.

`PLAYBACK-AWARE` means playback-critical requests preserve priority and bounded
call behaviour under the declared mixed workload.

These are measured claims, not universal hard-real-time guarantees.

## 15. Claim format

A complete claim uses:

```text
Implementation: <name and version>
Profile: <profile identifier>
Resource: <resource identifier or none>
Qualifiers: <zero or more qualifiers>
Platform: <CPU, toolchain, OS or RTOS>
Build: <relevant options>
Source limits: <sample rates, channels, duration and block limits>
Conformance suite: <version>
Fixture manifest: <hash>
Deviations: <none or explicit list>
```

A skipped mandatory test invalidates the corresponding claim unless a
normative platform exception is recorded by the suite.
