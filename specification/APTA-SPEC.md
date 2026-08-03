# Adaptive Progressive Track Analysis Specification

**Short name:** APTA  
**Status:** APTA 1.0 Release Candidate Draft  
**Normative language:** [`normative-language.md`](normative-language.md)  
**Normative manifest:** [`APTA-1.0-NORMATIVE-MANIFEST.md`](APTA-1.0-NORMATIVE-MANIFEST.md)  
**Reference implementation:** `libapta`

## 1. Purpose

Adaptive Progressive Track Analysis defines a portable processing and result
model for analysing prerecorded audio without requiring completion of
full-track analysis before useful results become available.

A conforming implementation can progressively expose waveform, tempo and
beatgrid information while the host application retains control of playback,
decoding, storage, scheduling and device resources.

## 2. Core principle

> Playback and user interaction MUST NOT depend on completion of full-track
> analysis.

An implementation claiming an adaptive profile MUST be able to prioritise an
explicitly requested source-frame region and requested analysis features over
unrelated background analysis.

## 3. Scope of APTA 1.0

APTA 1.0 standardises:

- source-frame time and half-open ranges;
- immutable source PCM format per analysis session;
- PCM push and pull input contracts;
- explicit end-of-input signalling;
- bounded cooperative processing and cancellation;
- playback focus and priority-region requests;
- immutable result generations and diagnostics;
- static session workspaces and optional bounded immutable result slots;
- waveform overview, detail tiles, coverage and gaps;
- optional three-band overview waveform values;
- BPM, tempo candidates, ambiguity and confidence;
- local beatgrid, fractional positions and stable-range locking;
- global beatgrid, dynamic-tempo segments and explicit-beat representation;
- pending and applied grid revisions;
- partial waveform-result seeding into a compatible new session;
- portable `.apta` interchange using container version 1;
- capability discovery, conformance profiles and extension behaviour.

The complete version-1 standard section set is:

```text
META WOVR WDTL TEMP LGRD GGRD REVN
```

Sections are optional or profile-dependent according to
[`file-format.md`](file-format.md),
[`global-grid-container.md`](global-grid-container.md) and
[`profiles.md`](profiles.md).

## 4. Features deferred from APTA 1.0 core

The following require a separately versioned extension, integration contract or
application policy:

- musical key and key changes;
- downbeat, meter, bar and phrase classification;
- proprietary cue, playlist or library formats;
- audio decoding;
- playback or real-time audio output;
- USB, filesystem or network ownership by the portable core;
- user-interface behaviour;
- three-band detail tiles;
- mandatory decoded-audio content hashing;
- bit-exact reference tempo or beatgrid algorithms;
- universal hard-real-time guarantees.

An implementation MUST NOT advertise a deferred capability through the APTA
1.0 core capability mask unless a corresponding normative extension is
implemented.

## 5. Normative document set

The candidate normative set is listed with immutable repository blob hashes in
[`APTA-1.0-NORMATIVE-MANIFEST.md`](APTA-1.0-NORMATIVE-MANIFEST.md).

It contains:

1. this master specification;
2. [`normative-language.md`](normative-language.md);
3. [`terminology.md`](terminology.md);
4. [`time-model.md`](time-model.md);
5. [`pcm-input.md`](pcm-input.md);
6. [`progressive-scheduling.md`](progressive-scheduling.md);
7. [`waveform.md`](waveform.md);
8. [`tempo.md`](tempo.md);
9. [`beatgrid.md`](beatgrid.md);
10. [`lifecycle.md`](lifecycle.md);
11. [`confidence.md`](confidence.md);
12. [`result-model.md`](result-model.md);
13. [`file-format.md`](file-format.md);
14. [`global-grid-container.md`](global-grid-container.md);
15. [`wovr-state-flags.md`](wovr-state-flags.md);
16. [`profiles.md`](profiles.md);
17. [`extensions.md`](extensions.md);
18. [`conformance.md`](conformance.md).

Architecture drafts, implementation status records, readiness reports,
reference integration documents, examples and code comments are informative.
They do not override this normative set.

## 6. Public API candidate

The current public API candidate is maintained under
[`../include/apta/`](../include/apta/).

The headers are the reference implementation vehicle for this specification.
They remain a draft ABI until Stage S9 P2 completes version compatibility,
layout, symbol and installed-consumer gates. The API version number is
independent of the specification and container versions.

Public API and ABI policy is documented in
[`../docs/api/APTA-PUBLIC-API-ABI-POLICY-0.1.md`](../docs/api/APTA-PUBLIC-API-ABI-POLICY-0.1.md).
That policy is informative until incorporated into the final 1.0 normative
manifest or replaced by a normative API contract.

## 7. Separation of responsibilities

The APTA core owns:

- analysis state and feature scheduling decisions inside a session;
- waveform, tempo, beatgrid, confidence and revision results;
- immutable result generations;
- portable result parsing and serialization.

The APTA core does not own:

- audio playback;
- codec implementation;
- file, USB or network access;
- operating-system tasks or threads;
- application event loops;
- device-specific resource arbitration;
- user-interface policy.

A platform adapter MAY provide helpers for those facilities, but the portable
core MUST remain usable without them.

## 8. Conformance model

A conforming implementation MUST:

- name every claimed profile and optional qualifier;
- implement every mandatory requirement of the claim;
- expose only capabilities it can represent through the normative result model;
- follow specified ownership, lifetime, revision and error contracts;
- reject invalid external `.apta` data safely;
- pass the applicable versioned conformance tests;
- publish a conformance report identifying implementation, platform, build,
  source limits, suite version, fixture manifest and deviations.

Algorithmic identity is not generally required. A third-party implementation
MAY use different DSP algorithms while preserving specified units, ranges,
state transitions, serialization and observable API behaviour.

APTA 1.0 defines `+REFERENCE-WAVEFORM-1.0` where exact vectors exist. It does
not define reference-tempo or reference-beatgrid qualifiers.

## 9. Platform and implementation claims

POSIX, Windows and ESP-IDF are reference integrations of `libapta`. They
provide multi-platform evidence but are not three independent DSP
implementations.

A conformance report MUST distinguish:

- an independent platform integration of the same implementation;
- an independently written producer or consumer of `.apta`;
- an independent analysis implementation.

## 10. Version separation

The project maintains independent version domains:

- APTA specification version;
- `libapta` API and ABI version;
- `libapta` package/release version;
- `.apta` container version;
- individual container section versions;
- extension versions.

Compatibility in one domain MUST NOT be inferred from a number in another
domain.

The intended final mapping is specification 1.0, API/package 1.0.0 and
container version 1. Release-candidate values remain non-final until the S9
release gates pass.

## 11. Document precedence

When requirements conflict, precedence is:

1. the normative document dedicated to the subject;
2. this master specification;
3. another document in the normative manifest;
4. public API documentation;
5. architecture documents;
6. reference implementation and integration documentation;
7. examples.

A conflict between two dedicated normative documents is a specification defect
and MUST be resolved before final APTA 1.0 publication.

## 12. Change control during the release-candidate cycle

A normative candidate change MUST include:

- the problem and affected requirement;
- compatibility impact;
- corresponding implementation and conformance changes when applicable;
- an updated normative manifest;
- maintainer approval under project governance.

Editorial changes that do not alter requirements may update document hashes
without changing specification semantics. A public API or wire-format change
resets affected release-candidate evidence.
