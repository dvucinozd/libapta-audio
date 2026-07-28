# Adaptive Progressive Track Analysis Specification

**Short name:** APTA  
**Status:** Working Draft 0.1  
**Normative language:** [`normative-language.md`](normative-language.md)  
**Reference implementation:** `libapta`

## 1. Purpose

Adaptive Progressive Track Analysis defines a portable processing and result model for analysing prerecorded audio without requiring completion of full-track analysis before useful results become available.

A conforming implementation can progressively expose waveform, tempo and beatgrid information while the host application retains control of playback, decoding, storage, scheduling and device resources.

## 2. Core principle

> Playback and user interaction MUST NOT depend on completion of full-track analysis.

The implementation MUST be able to prioritise an explicitly requested source-frame region and requested analysis features over unrelated background analysis.

## 3. Scope of APTA Core 0.1

APTA Core 0.1 standardises:

- source-frame time and half-open ranges;
- immutable source PCM format per analysis session;
- PCM push and pull input contracts;
- explicit end-of-input signalling;
- bounded cooperative processing;
- playback focus and priority-region requests;
- waveform overview and detail coverage;
- BPM and local beatgrid result models;
- progressive result lifecycle;
- confidence values;
- immutable result generations;
- portable `.apta` interchange requirements;
- capability discovery and conformance behaviour.

## 4. Deferred features

The following features are not part of APTA Core 0.1 unless defined by a separately versioned extension:

- musical key and key changes;
- downbeat and bar classification;
- phrase classification;
- proprietary cue, playlist or library formats;
- audio decoding;
- playback or real-time audio output;
- USB, filesystem or network ownership;
- user-interface behaviour.

An implementation MUST NOT advertise a deferred capability through the core capability mask unless the corresponding normative extension is implemented.

## 5. Normative document set

The specification is divided into the following documents:

1. [`normative-language.md`](normative-language.md)
2. [`terminology.md`](terminology.md)
3. [`time-model.md`](time-model.md)
4. [`pcm-input.md`](pcm-input.md)
5. [`progressive-scheduling.md`](progressive-scheduling.md)
6. `waveform.md`
7. `tempo.md`
8. `beatgrid.md`
9. `lifecycle.md`
10. `confidence.md`
11. `result-model.md`
12. `file-format.md`
13. `profiles.md`
14. `extensions.md`
15. `conformance.md`

Documents that do not yet exist are planned work and do not create normative requirements.

## 6. Separation of responsibilities

The APTA core owns:

- analysis state;
- analysis scheduling decisions inside a session;
- waveform, tempo, beatgrid and confidence results;
- result generations and serialization.

The APTA core does not own:

- audio playback;
- codec implementation;
- file, USB or network access;
- operating-system tasks or threads;
- application event loops;
- device-specific resource arbitration;
- user-interface policy.

A platform adapter MAY provide helpers for those facilities, but the portable core MUST remain usable without them.

## 7. Conformance model

A conforming implementation MUST:

- implement every mandatory requirement of the claimed profile;
- expose only capabilities it can represent through the normative result model;
- follow the specified ownership, lifetime and error contracts;
- reject invalid external `.apta` data safely;
- pass the applicable conformance tests.

Algorithmic identity is not generally required. A third-party implementation MAY use different DSP algorithms while preserving specified units, ranges, state transitions, serialization and observable API behaviour.

## 8. Version separation

The project maintains three independent version domains:

- APTA specification version;
- `libapta` API and ABI version;
- `.apta` container version.

Compatibility in one domain MUST NOT be inferred from a version number in another domain.

## 9. Document precedence

When requirements conflict, precedence is:

1. the normative specification document dedicated to the subject;
2. this master specification;
3. public API documentation;
4. architecture documents;
5. reference implementation documentation;
6. examples.

Architecture drafts and implementation notes are informative unless a normative specification explicitly incorporates them.
