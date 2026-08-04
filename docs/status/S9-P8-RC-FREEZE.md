# Stage S9 P8 — APTA 1.0 release-candidate freeze

**Status:** complete. PR #68 was merged into `main` on 2026-08-04.

## Accepted candidate

```text
package:       1.0.0-rc.1
public API:    1.0.0
specification: 1.0 release candidate
container:     version 1
shared ABI:    SOVERSION 1
accepted head: 81e2584df7f466cd53cb21d1eb4623f5d94e1035
```

P8 froze the release identity and every release-facing contract established by
P1 through P7. It added no production feature, public API, ABI, normative
requirement or container byte change.

## Frozen evidence

`release/rc-freeze-v1.json` records 22 Git-blob identities covering release
identity, normative authority, ABI, fixtures, public conformance,
interoperability, security evidence, blocker closure and legal review.

All ten S9 blockers are closed. The Apache-2.0 package review found no bundled
third-party source tree and no project-specific NOTICE payload for the core
release package.

## Accepted validation

The exact accepted RC head passed:

- CI #426 — Linux static/shared, Windows static/shared, ILP32 and the P7
  ASan/UBSan nine-target campaign;
- Reference fixture #84 — normative hashes, canonical/malformed regeneration
  and independent byte reproduction;
- ESP-IDF #103 — ESP-IDF 5.5.4/ESP32, 6.0.2/ESP32 and
  6.0.2/ESP32-S3/ESP-DSP firmware-build evidence;
- APTA 1.0 release candidate #5 — freeze/legal review and reproducible
  Linux/Windows static/shared/source packages.

The retained P7 campaign executed nine targets 8,192 times each, 73,728
executions in total, with zero crashes, hangs, sanitizer findings or retained
crash artifacts.

## Boundary carried into P9

P9 may change only final version labels, package metadata, publication
automation and release documentation. Any public API, ABI, normative
requirement, canonical fixture, parser/writer or DSP behavior change invalidates
the accepted RC and requires a new candidate cycle.
