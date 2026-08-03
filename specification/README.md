# APTA specification

This directory contains the normative Adaptive Progressive Track Analysis 1.0
release-candidate document set. Start with
[`APTA-SPEC.md`](APTA-SPEC.md) and verify the exact candidate files through
[`APTA-1.0-NORMATIVE-MANIFEST.md`](APTA-1.0-NORMATIVE-MANIFEST.md).

The release-candidate label does not authorise a final APTA 1.0 claim. Final
publication requires completion of the Stage S9 container, conformance,
interoperability, security and packaging gates.

Normative documents use RFC 2119/8174-style requirement terms such as MUST,
MUST NOT, SHOULD, SHOULD NOT and MAY.

Normative candidate documents:

- [`APTA-SPEC.md`](APTA-SPEC.md) — master scope, precedence and version domains.
- [`APTA-1.0-NORMATIVE-MANIFEST.md`](APTA-1.0-NORMATIVE-MANIFEST.md) — exact
  candidate paths and repository blob hashes.
- [`normative-language.md`](normative-language.md)
- [`terminology.md`](terminology.md)
- [`time-model.md`](time-model.md)
- [`pcm-input.md`](pcm-input.md)
- [`progressive-scheduling.md`](progressive-scheduling.md)
- [`waveform.md`](waveform.md)
- [`tempo.md`](tempo.md)
- [`beatgrid.md`](beatgrid.md)
- [`lifecycle.md`](lifecycle.md)
- [`confidence.md`](confidence.md)
- [`result-model.md`](result-model.md)
- [`container-v1-registry.md`](container-v1-registry.md) — authoritative
  version-1 FourCC registry, dependencies, ordering, strictness, finality and
  future-compatibility rules.
- [`file-format.md`](file-format.md)
- [`global-grid-container.md`](global-grid-container.md)
- [`wovr-state-flags.md`](wovr-state-flags.md)
- [`profiles.md`](profiles.md)
- [`extensions.md`](extensions.md)
- [`conformance.md`](conformance.md)

Architecture drafts, status reports, reference contracts, implementation
readiness records and examples are informative. They cannot override the
normative set.

The original architecture proposed a separate `processing-model.md`; APTA 1.0
defines that material across PCM input, progressive scheduling, lifecycle and
result-model documents.

The specification is distributed under the repository's
[Apache License 2.0](../LICENSE). Proposed normative changes follow
[`../CONTRIBUTING.md`](../CONTRIBUTING.md) and the decision process in
[`../GOVERNANCE.md`](../GOVERNANCE.md).
