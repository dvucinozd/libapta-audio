# libapta-audio governance

`libapta-audio` is a maintainer-led open-source project. This model keeps
specification, compatibility, security and release decisions explicit for the
stable APTA 1.0 standard and the maintained `libapta` 1.x reference
implementation.

## Roles

### Users

Users build products, integrations, experiments and independent
implementations with APTA. They shape priorities through issues, use cases,
test reports and compatibility feedback.

### Contributors

Contributors submit code, tests, documentation, specifications, reviews or
other project work. A merged contribution does not automatically confer
maintainer status.

### Maintainers

Maintainers triage issues, review changes, protect compatibility and security,
merge pull requests, manage releases and interpret project policy. The current
project maintainer is:

- Daniel Vučinović (GitHub: [@dvucinozd](https://github.com/dvucinozd))

Additional maintainers may be invited based on sustained, constructive
contributions, sound technical judgment, reliable reviews and respect for the
project's security and compatibility boundaries. Maintainer additions and
departures are recorded in this document through a normal pull request.

## Decision process

Routine fixes and backward-compatible improvements use issue and pull-request
discussion. The maintainer seeks practical consensus and records the decision
in the merged change.

Changes with broad or lasting impact require an issue or APTA RFC before
implementation. This includes:

- normative specification changes;
- public API or ABI breaks;
- incompatible `.apta` format changes;
- new required dependencies or platform assumptions;
- release, licensing, governance or conformance-policy changes.

Such a proposal must state its motivation, alternatives, compatibility,
resource cost, security impact and migration plan. Normative changes require
matching specification text and appropriate conformance evidence.

When consensus cannot be reached, the maintainer makes and records the final
decision. The rationale should address the strongest technical objections.
Urgent security fixes may be prepared privately and documented after
coordinated disclosure.

## Review and merge

- Authors do not approve their own changes when another maintainer is
  available.
- Required automated checks must pass unless a documented infrastructure
  failure is unrelated to the change and the maintainer explicitly records the
  exception.
- Security-sensitive changes follow [`SECURITY.md`](SECURITY.md) and may be
  reviewed outside the public pull-request flow until disclosure.
- Force pushes to protected release history and merging known-broken changes
  are avoided.

The project may use draft pull requests for early review. Merge strategy is
chosen to preserve a readable, auditable history.

## Releases and compatibility

The specification version, C API version and `.apta` container/section versions
are related but distinct. A release must identify all applicable versions and
the validation evidence behind its claims.

APTA 1.0, the public API 1.x contract, shared-library `SOVERSION 1` and
container version 1 are stable. Patch releases may correct defects, metadata and
editorial errors without changing those contracts. Compatible additions require
an explicit minor-release proposal and evidence. Incompatible API, ABI or wire
changes require a new major version and migration guidance.

Passing the repository's tests means that an implementation passed those
specific checks. It does not by itself grant certification, trademark rights
or a universal APTA conformance claim.

## Conduct and conflicts

Project participation must remain respectful, technical and inclusive.
Harassment, personal attacks and deliberate disruption are not acceptable.

Anyone reviewing a change should disclose a material conflict of interest.
When practical, another maintainer or an independent reviewer should handle
the decision. If no alternative reviewer exists, the conflict and resulting
decision must be documented.

## Amendments and continuity

Governance changes use the same public proposal and review process as other
lasting project-policy changes. If the project grows beyond a single
maintainer, this document should be revised to define voting, quorum,
succession and an appeal path before those mechanisms are needed.

If the current maintainer becomes unavailable, repository ownership and
release authority must be transferred explicitly; contributors must not infer
authority from inactivity alone.
