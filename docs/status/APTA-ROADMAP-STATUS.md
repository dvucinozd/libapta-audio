# APTA development roadmap status

- **Current completed stage:** S9 — APTA 1.0
- **Active development line:** `1.1.0` branch, infrastructure tasks 1–4 complete
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
- **P11:** complete — quick-start examples, repository UX and contributor
  onboarding without changing the stable contracts.
- **P12:** source complete, owner setting pending — security automation,
  immutable workflow dependencies and future release provenance are present;
  GitHub dependency-graph activation is still required for the full review gate.
- **P13:** source complete, publication pending — a deterministic standalone
  ESP-IDF component, registry metadata, packaged-example evidence and an exact-
  tag publication workflow are present; first staging and production uploads
  require a later stable tag, namespace and owner token.

## APTA 1.0 claims

The stable profiles are `WAVEFORM-1.0`, `ADAPTIVE-WAVEFORM-1.0` and
`CORE-ANALYSIS-1.0`, with optional exact-vector
`+REFERENCE-WAVEFORM-1.0`.

No reference tempo or reference beatgrid qualifier is defined. Algorithmic
accuracy metrics remain implementation-quality evidence unless a normative
profile explicitly states otherwise.

POSIX, Windows and ESP-IDF are integrations of the same reference
implementation. Independent producer/consumer evidence validates the container
contract; it is not a claim of three independent DSP engines.

## Release evidence

The accepted P8 candidate head
`81e2584df7f466cd53cb21d1eb4623f5d94e1035` passed the native Linux/Windows,
ILP32, sanitizer/fuzz, independent-fixture, ESP-IDF, package, conformance and
interoperability gates.

The P9 release verifier permits only version-bound and publication changes from
that accepted candidate. The stable release workflow regenerates deterministic Linux and Windows
package evidence. `v1.0.1` publishes editorial errata and corrected release
metadata without changing API/ABI, wire bytes or production DSP behavior.

## Known limitations

- no native big-endian release target was available;
- ESP-IDF CI is firmware-build evidence, not physical-device execution;
- POSIX atomic replacement omits parent-directory `fsync()` after rename;
- tempo/beatgrid selection accuracy is outside semantic conformance;
- desktop adapters and CLI tools are source components, not stable exported
  package components;
- the ESP Component Registry version is not published until a later stable tag
  is validated against staging and uploaded with owner credentials.

## Post-1.0 planning boundary

APTA 1.1 development is separate from the stable 1.0 release. The result/API
extensions, validated external-result builder, optional DJ sections and
streaming container I/O are implemented on the `1.1.0` branch. Algorithmic,
ESP32-P4 profile, corpus, release-version and publication work remains open;
the branch must not be represented as a stable release. The detailed boundary
is maintained in
[`APTA-1.1-DEVELOPMENT-STATUS.md`](APTA-1.1-DEVELOPMENT-STATUS.md).

The `v1.0.0` tag remains immutable and `v1.0.1` remains the maintained stable
coherence release until a fully qualified 1.1 release supersedes it.
