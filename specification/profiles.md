# Conformance profiles and resource classes

**Status:** APTA Working Draft 0.1

## 1. Purpose

APTA separates functional capability profiles from resource classes.

A capability profile states what observable behaviour and data models an implementation supports.

A resource class states measured memory and execution constraints. It does not add analysis features.

A conformance claim MUST name both when a resource claim is made.

## 2. Common baseline

Every APTA Core 0.1 profile requires:

- source-frame time;
- half-open ranges;
- fixed-width public value semantics;
- immutable result generations;
- lifecycle and confidence rules applicable to supported features;
- safe handling of unsupported feature requests;
- bounded allocation according to configured limits;
- no mandatory ownership of codecs, filesystems, USB, networking, playback or UI.

## 3. APTA Waveform Profile 0.1

Identifier:

```text
APTA-WAVEFORM-0.1
```

Required capabilities:

- PCM push input;
- explicit end-of-input;
- overview waveform;
- explicit coverage and gaps;
- immutable waveform snapshots;
- semantic waveform conformance;
- version-1 `WOVR` serialization and parsing;
- malformed-container rejection for required waveform cases.

Optional capabilities:

- PCM pull input;
- detail tiles;
- three-band waveform;
- reference-waveform conformance;
- partial `.apta` results.

## 4. APTA Adaptive Waveform Profile 0.1

Identifier:

```text
APTA-ADAPTIVE-WAVEFORM-0.1
```

This profile includes every Waveform Profile requirement and additionally requires:

- playback focus;
- explicit priority-region requests;
- bounded cooperative processing;
- push-mode PCM demand or equivalent documented pull-mode demand;
- local detail publication before unrelated background completion;
- priority preservation;
- starvation prevention policy;
- low-memory detail retention or eviction policy;
- progressive request status.

The implementation MUST demonstrate that moving focus changes future work priority without invalidating already published stable output.

## 5. APTA Core Analysis Profile 0.1

Identifier:

```text
APTA-CORE-ANALYSIS-0.1
```

This profile includes every Adaptive Waveform Profile requirement and additionally requires:

- BPM values in milli-BPM units;
- tempo candidates or explicit ambiguity flags;
- local beatgrid;
- fractional-frame beat positions;
- per-range lifecycle and confidence;
- stable-range revision protection;
- serialization only for features whose section payload version is normative.

Until `TEMP` and `BGRD` payloads become normative, an implementation MAY claim the processing/API portion of this profile but MUST qualify file-format conformance as waveform-only.

## 6. Deferred profile features

The following are not Core 0.1 profile requirements:

- musical key;
- downbeat or bar classification;
- phrase classification;
- proprietary cue/library formats;
- source decoding;
- playback output.

They require separate extension identifiers and conformance tests.

## 7. Reference algorithm qualifiers

Functional profiles do not generally require one DSP algorithm.

An implementation may append qualifiers:

```text
+REFERENCE-WAVEFORM-0.1
+REFERENCE-TEMPO-<version>
+REFERENCE-BEATGRID-<version>
```

Only a qualifier with an existing normative algorithm and golden-vector suite may be claimed.

APTA Core 0.1 currently defines reference-waveform peak/RMS behaviour but not a bit-exact three-band, tempo or beatgrid algorithm.

## 8. Resource measurement rules

Peak memory includes:

- context and session allocations;
- static workspace;
- queued PCM copies;
- internal DSP state;
- current unpublished work;
- one current published result generation retained by the session.

A resource claim MUST state whether additional concurrently retained application-acquired snapshots are excluded or included.

Peak memory MUST be measured using the claimed source limits, feature set and scheduling workload.

Stack usage MUST be measured or bounded separately when the platform provides meaningful stack accounting.

## 9. Resource class R0 — Static 128

Identifier:

```text
APTA-R0-STATIC-128K
```

Requirements:

- no heap allocation after successful session creation;
- maximum provided workspace: 131,072 bytes;
- cooperative single-thread processing;
- documented maximum PCM block size;
- documented maximum waveform columns and retained tiles;
- waveform-only capability is permitted.

The implementation MUST reject configurations that cannot fit rather than allocating outside the workspace.

## 10. Resource class R1 — Embedded 512

Identifier:

```text
APTA-R1-EMBEDDED-512K
```

Requirements:

- peak session memory no greater than 524,288 bytes under the declared workload;
- bounded cooperative processing;
- no mandatory operating-system thread;
- adaptive waveform support when claimed with the Adaptive Waveform Profile;
- documented internal-RAM versus external-RAM policy;
- documented PCM queue and detail-tile limits.

This class is intended for constrained microcontrollers without assuming PSRAM.

## 11. Resource class R2 — Embedded 4M

Identifier:

```text
APTA-R2-EMBEDDED-4M
```

Requirements:

- peak session memory no greater than 4,194,304 bytes under the declared workload;
- adaptive waveform;
- sufficient working memory for local tempo and beatgrid when claimed with Core Analysis;
- documented use of internal RAM, external RAM and DMA-capable memory;
- measured publication latency and process-call duration distribution.

## 12. Resource class R3 — General purpose

Identifier:

```text
APTA-R3-GENERAL
```

R3 has no normative low memory ceiling, but the implementation MUST still:

- enforce configurable allocation limits;
- report measured peak memory for the conformance workload;
- reject untrusted container sizes before unbounded allocation;
- document thread and worker behaviour.

## 13. Processing responsiveness qualifiers

A resource claim may include one measured responsiveness qualifier:

```text
COOPERATIVE
INTERACTIVE
PLAYBACK-AWARE
```

`COOPERATIVE` means the host controls every processing call and the core creates no mandatory worker.

`INTERACTIVE` means named conformance workloads publish requested local waveform output within a documented percentile latency on declared hardware.

`PLAYBACK-AWARE` means playback-critical requests preserve priority and bounded-call behaviour under the declared mixed workload.

These qualifiers are measured claims, not universal hard real-time guarantees.

## 14. Claim format

A complete claim uses:

```text
<implementation> <version>
Profile: <profile identifier>
Resource: <resource identifier or none>
Qualifiers: <zero or more qualifiers>
Platform: <CPU, toolchain, OS/RTOS>
Source limits: <sample rates, channels, maximum duration/block size>
Conformance suite: <suite version and fixture manifest hash>
```

Example:

```text
libapta 0.1.0
Profile: APTA-ADAPTIVE-WAVEFORM-0.1
Resource: APTA-R1-EMBEDDED-512K
Qualifiers: +REFERENCE-WAVEFORM-0.1, COOPERATIVE
Platform: ESP32-S3, ESP-IDF, Xtensa
Source limits: 44.1/48 kHz, mono/stereo, declared block and duration limits
Conformance suite: 0.1, manifest <hash>
```

A claim MUST list deviations and unsupported optional capabilities.
