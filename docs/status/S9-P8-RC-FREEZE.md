# Stage S9 P8 — APTA 1.0 release-candidate freeze

**Status:** release-candidate implementation prepared. Completion requires all
workflows and retained artifacts from the final `release/1.0.0-rc.1` head to
pass and be inspected before merge.

## Scope

P8 freezes the release identity and every release-facing contract established
by P1 through P7. It adds no feature, public API, ABI, normative requirement or
container byte change.

The candidate identity is:

```text
package:       1.0.0-rc.1
public API:    1.0.0
specification: 1.0 release candidate
container:     version 1
shared ABI:    SOVERSION 1
```

## Frozen contract set

`release/rc-freeze-v1.json` records 22 Git-blob identities covering release
identity, normative authority, ABI, fixtures, public conformance,
interoperability, security evidence, blocker closure and legal review.

`release/check_rc_freeze.py` recomputes every Git blob hash, validates version
defines, runs the normative-manifest checker and requires all ten S9 blockers
to be closed.

## Legal and license review

`release/legal-review-v1.json` records the repository-level release audit. The
core source and binary archives are Apache-2.0, contain `LICENSE`, `VERSION` and
`CHANGELOG.md`, and do not bundle a third-party source tree or project-specific
NOTICE payload. This is a project packaging review, not legal advice.

## Required RC gates

The final RC head must pass Linux and Windows static/shared native, ABI,
package, conformance and interchange gates; Linux ILP32; ASan/UBSan and the
fixed P7 fuzz campaign; independent fixture regeneration; ESP-IDF 5.5.4/6.0.2
builds; reproducible archives with SHA-256 sidecars; and freeze/legal reports.

The normal CI, reference-fixture and ESP-IDF workflows retain their existing
responsibilities. `.github/workflows/release-candidate.yml` adds RC-specific
freeze review and publishes reproducible Linux and Windows package archives.

## RC change policy

- documentation clarification is allowed only when it does not alter a requirement;
- a production change requires a recorded failed gate;
- a public API, ABI, normative or wire-format change resets the RC number and affected evidence;
- new features are prohibited;
- the RC does not claim final APTA 1.0 publication or an unavailable reference tempo/beatgrid qualification.

## Exit gate

P8 is complete only when the final RC commit has all required workflows green,
zero open S9 blockers, inspected archive/checksum and freeze/legal reports, no
unreviewed contract change and maintainer approval to proceed to P9.
