# APTA development roadmap status

- **Current completed stage:** S9 — APTA 1.0
- **Active development line:** `1.1.0`; core DJ-analysis implementation substantially complete, evidence/qualification gates remain
- **Stable specification:** APTA 1.0
- **Stable public API:** 1.0.0
- **Maintained package:** 1.0.1
- **Stable container:** version 1
- **Stable shared ABI:** SOVERSION 1
- **Current release tag:** `v1.0.1`
- **First stable tag:** `v1.0.0` (immutable)
- **License:** Apache-2.0

## Status summary

| Stage | Status | Release boundary |
|---|---|---|
| S0 — Foundation | Complete | Project, terminology, governance, contribution, licensing and security foundations |
| S1 — Portable core API | Complete | Push/pull PCM, bounded processing, cancellation, immutable results, static workspaces and result slots |
| S2 — Waveform Profile | Complete | Overview/detail waveform, coverage, focus scheduling and WOVR/WDTL |
| S3 — `.apta` container | Complete | Canonical container version 1, CRC32C and hardened parser/writer |
| S4 — Tempo and local grid | Complete | BPM candidates, confidence, locking and TEMP/LGRD |
| S5 — Reference desktop tools | Complete | Native file/WAV adapters and analyze/inspect/validate tools |
| S6 — Global grid and dynamic tempo | Complete | GGRD/REVN, segments, explicit beats and immutable revisions |
| S7 — ESP-IDF port | Complete integration | ESP-IDF 5.5.4/6.0.2 component and retained firmware-build evidence |
| S8 — Windows platform | Complete integration | Native Windows adapter, tools and Linux/Windows interchange |
| S9 — APTA 1.0 | Complete | Stable specification, API/ABI, container, packages, conformance, interoperability, security campaign and release publication |

## Post-release repository quality

- **P10:** complete — APTA 1.0.1 documentation and release coherence.
- **P11:** complete — quick-start examples, repository UX and contributor onboarding without changing the stable contracts.
- **P12:** source complete, owner setting pending — security automation and immutable workflow dependencies are present; GitHub dependency-graph activation is still an owner-side setting.
- **P13:** source complete, publication pending — deterministic standalone ESP-IDF component packaging and guarded registry publication exist; first publication requires a later exact stable tag and owner credentials.

## APTA 1.0 claims

The stable profiles remain `WAVEFORM-1.0`, `ADAPTIVE-WAVEFORM-1.0` and
`CORE-ANALYSIS-1.0`, with optional exact-vector
`+REFERENCE-WAVEFORM-1.0`.

No APTA 1.1 development work changes the meaning of the immutable 1.0 tags or
the maintained `v1.0.1` release.

## APTA 1.1 implementation status

The `1.1.0` development line now contains:

- extended result/API model for musical key, meter/downbeat and calibrated quality;
- bounded external-result builder;
- deterministic `MKEY`, `MTRD` and `CONF` container sections;
- bounded streaming container I/O;
- relation-aware tempo/grid ensemble implementation candidate;
- pre-registered deterministic confidence-calibration contract;
- native bounded meter/downbeat analysis;
- native bounded global musical-key analysis;
- provisional/stable/final immutable publication regression coverage;
- ESP-IDF 6.0.2 `esp32p4` firmware CI;
- deterministic 30-minute P4 capacity/layout gate;
- frozen final DJ acceptance evaluator and protocol.

The P4 capacity profile covers 86,400,000 frames (30 minutes at 48 kHz), 2,637
overview columns at 32,768 frames/column, 3,072 mutable S6 beat records and two
immutable result slots, for 9,216 resident explicit beat records. Its measured
minimum workspace is 932,960 bytes and bounded result pool is 531,232 bytes,
1,464,192 bytes combined. This is CI/layout evidence, not physical-device
performance evidence.

The detailed current boundary and exact remaining blockers are maintained in
[`APTA-1.1-DEVELOPMENT-STATUS.md`](APTA-1.1-DEVELOPMENT-STATUS.md).
The current private-corpus preparation and manual-review boundary is recorded in
[`APTA-1.1-FINAL-DJ-CORPUS-STATUS.md`](APTA-1.1-FINAL-DJ-CORPUS-STATUS.md).

Native trace evidence on the Ballroom development partition has since shown
that meter/downbeat accuracy is bounded upstream by beat-lattice quality (its
information ceiling is ~9/40 tracks, with production scoring already at that
ceiling), so downbeat candidate work is deprioritized in favour of the
tempo/grid ensemble boundary; see the closing analysis in
[`APTA-1.1-METER-DOWNBEAT-VALIDATION.md`](APTA-1.1-METER-DOWNBEAT-VALIDATION.md).

## What still blocks APTA 1.1 release

- ~~fresh relation-aware tempo/grid corpus qualification~~ — **closed 2026-08-25**: the ensemble passed all five frozen gates on a formal 48-track owner-supplied fresh set (25 -> 29 exact within 1%, zero broken selections, no safety regressions); see [`APTA-1.1-TEMPO-ENSEMBLE-EVALUATION.md`](APTA-1.1-TEMPO-ENSEMBLE-EVALUATION.md);
- ~~fitting and untouched-holdout acceptance of a production calibrated confidence model~~ — **closed 2026-08-25**: the `isotonic-pav-clamped-v1` tempo model (ID 1867860160) passed both calibration gates on a 48-row untouched holdout and is integrated as a bounded static lookup publishing an optional BPM quality record; see [`APTA-1.1-CONFIDENCE-CALIBRATION-PROTOCOL.md`](APTA-1.1-CONFIDENCE-CALIBRATION-PROTOCOL.md);
- independent manually verified >=48-track final DJ corpus passing frozen key/meter/downbeat/grid and high-confidence safety gates;
- physical ESP32-P4 memory/timing evidence when hardware is available;
- final API/ABI/wire freeze, version bump, package evidence, exact release candidate verification, tag and publication.

Until those gates close, `VERSION` remains `1.0.1`; the `1.1.0` branch is not a
stable release claim.

## Known limitations

- no native big-endian release target is available;
- ESP-IDF CI is firmware-build evidence, not physical-device execution;
- POSIX atomic replacement omits parent-directory `fsync()` after rename;
- algorithmic accuracy is not a conformance claim until the corresponding frozen acceptance protocol is passed;
- desktop adapters and CLI tools are source components, not stable exported package components;
- the ESP Component Registry version is not published until a later stable tag is validated and uploaded with owner credentials.

The `v1.0.0` tag remains immutable and `v1.0.1` remains the maintained stable
coherence release until a fully qualified 1.1 release supersedes it.
