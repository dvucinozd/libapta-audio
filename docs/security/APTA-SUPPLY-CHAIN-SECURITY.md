# APTA supply-chain security

This document describes the controls used to protect the `libapta-audio`
source tree, continuous-integration workflows and future release artifacts.
It complements [`SECURITY.md`](../../SECURITY.md), which defines private
vulnerability reporting and coordinated disclosure.

## Trust boundary

The stable APTA contracts remain defined by the normative specification,
public headers, ABI evidence, canonical container fixtures and release
manifests. Security automation is an additional verification layer; it does
not silently redefine the API, ABI, DSP behaviour or `.apta` wire format.

Repository automation follows these rules:

- third-party GitHub Actions are pinned to full 40-character commit SHAs;
- literal container images are pinned to immutable SHA-256 digests;
- workflow tokens receive the minimum permissions required by each job;
- normal build and test jobs use read-only repository access and disable
  persisted checkout credentials;
- release publication is isolated to the protected `main` push path.

The repository enforces the first two rules with
`security/automation/check_workflow_pins.py`.

## Continuous checks

### CodeQL

The `Security` workflow analyzes the C and C++ build on pull requests, relevant
pushes and a weekly schedule. The analyzed configuration builds the portable
core and public examples without desktop adapters, keeping the scan focused on
the stable library boundary.

### Dependency review

Pull requests are checked for newly introduced dependencies with moderate or
higher known-severity findings treated as failures when the GitHub dependency
graph is enabled. The workflow first probes the dependency-graph API. While the
graph is unavailable, it emits an explicit warning and runs the immutable
workflow-reference verifier as a limited fallback. That fallback is not a
replacement for GitHub dependency review.

The project has no runtime package-manager dependency, but GitHub Actions and
future build dependencies still form part of the supply chain.

### Dependabot

Dependabot checks GitHub Actions monthly. Updates are grouped into at most one
open pull request so that action changes can be reviewed and tested without
flooding the Actions queue.

## Future stable release artifacts

For the next stable release after `v1.0.1`, the release workflow will add:

- `apta-<version>-sbom.spdx.json`, a deterministic SPDX 2.3 file inventory;
- `SHA256SUMS`, covering every release asset except the checksum file itself;
- GitHub artifact attestations for the complete publication bundle.

The SPDX generator is self-tested in the normal CI workflow. Release
attestations bind the produced assets to the repository workflow and source
revision. They do not prove algorithmic correctness; the package, conformance,
ABI and interoperability checks remain authoritative for those claims.

`v1.0.1` predates P12 and is intentionally not retroactively attested or
rewritten. Its immutable tag and previously published assets remain unchanged.

## Verifying a future release

After downloading a release into an empty directory:

```bash
sha256sum --check SHA256SUMS
gh attestation verify ./apta-<version>-linux-*.tar.gz \
  --repo dvucinozd/libapta-audio
```

Repeat the attestation command for any asset whose provenance matters to the
consumer. Inspect the SPDX JSON directly or with an SPDX-compatible tool.

Checksums detect changed bytes. Attestations identify the workflow and
repository that produced an asset. Neither mechanism replaces signature-policy
decisions made by a downstream distributor.

## Repository-owner settings

The following GitHub settings are recommended and must be reviewed in the
repository UI because they are not source-controlled:

- enable the dependency graph so the configured dependency-review gate becomes
  authoritative;
- enable private vulnerability reporting;
- enable Dependabot alerts and security updates;
- enable secret scanning and push protection where available;
- protect `main` with required checks, pull requests and conversation
  resolution;
- prevent force-pushes and deletion of protected branches;
- protect `v*` tags from update or deletion;
- restrict Actions to trusted sources and require full-SHA pinning if the
  repository settings expose that policy.

Source-controlled P12 files do not claim that these account-level controls are
enabled. Their state should be audited separately.
