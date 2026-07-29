# Project documentation

This directory contains informative design, implementation and verification
documentation. Normative requirements belong in
[`../specification/`](../specification/).

For the current project state, start with
[`status/APTA-ROADMAP-STATUS.md`](status/APTA-ROADMAP-STATUS.md). Documents
that identify a source commit or CI run are milestone evidence snapshots; their
test counts and forward-looking sections describe that recorded baseline.

## Sections

- [`architecture/`](architecture/) — original system boundaries and target
  architecture.
- [`api/`](api/) — public API, ABI, PCM pull, ownership and threading
  contracts.
- [`conformance/`](conformance/README.md) — self-tested readiness reports, manifests and
  the independent fixture record.
- [`file-format/`](file-format/) — `.apta` implementation notes; normative
  format rules are in
  [`../specification/file-format.md`](../specification/file-format.md).
- [`memory/`](memory/README.md) — static workspace and bounded immutable-result design
  records.
- [`ports/`](ports/) — platform integration documentation.
- [`reference/`](reference/) — reference algorithms, tools, containers and
  measured integration profiles.
- [`reviews/`](reviews/) — technical audits and resolved-review records.
- [`scheduler/`](scheduler/README.md) — reference scheduler policy.
- [`status/`](status/README.md) — current roadmap status and stage evidence snapshots.
- [`decisions/`](decisions/) — reserved Architecture Decision Record
  directory.
- [`roadmap/`](roadmap/) — original implementation plans and stage definitions.
