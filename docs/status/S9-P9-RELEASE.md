# Stage S9 P9 — APTA 1.0 release

**Status:** final APTA 1.0 release definition for the governance-approved
`v1.0.0` publication commit.

## Stable identity

```text
specification: stable 1.0
API/package:   stable 1.0.0
container:     stable version 1
shared ABI:    SOVERSION 1
profiles:      WAVEFORM-1.0, ADAPTIVE-WAVEFORM-1.0,
               CORE-ANALYSIS-1.0 and optional +REFERENCE-WAVEFORM-1.0
```

## Finalization scope

P9 removes the `-rc.1` package suffix and promotes the specification labels to
final 1.0. It does not change production analysis behavior, the public API/ABI,
container wire bytes, canonical fixtures, conformance semantics or security
campaign policy.

`release/final-release-v1.json` identifies the accepted RC and the exact files
permitted to change during finalization. `release/check_final_release.py`
verifies that every inherited frozen contract remains byte-identical, validates
the final normative manifest, confirms all S9 blockers remain closed and
rejects any path outside the approved publication boundary.

## Publication workflow

`.github/workflows/release.yml` performs:

- final-release, blocker and legal verification;
- deterministic Linux static/shared/source package checks;
- deterministic Windows static/shared package checks;
- installed-consumer, public conformance and interchange checks;
- final artifact manifests and SHA-256 sidecars;
- governance-approved annotated `v1.0.0` tagging from the merged release commit;
- GitHub release publication with source/binary bundles, checksums, changelog,
  normative manifest and conformance evidence.

The historical P8 release-candidate workflow remains available for manual RC
audit but skips final-version pull requests.

## Retained limitations

- no supported native big-endian release target was available; deterministic
  byte-swap evidence is retained;
- hosted ESP-IDF CI proves firmware compile/link integration and does not claim
  execution on physical hardware;
- POSIX atomic replacement does not `fsync()` the parent directory after rename;
- tempo and beatgrid selection accuracy remain implementation-quality evidence
  outside semantic conformance;
- no reference tempo or reference beatgrid qualifier is defined in APTA 1.0.

## Completion

Stage S9 is complete at the `v1.0.0` tag when the final release workflow has
passed and published the retained release assets. APTA 1.1 planning is a
separate post-1.0 activity and cannot alter the tagged 1.0 contract.
