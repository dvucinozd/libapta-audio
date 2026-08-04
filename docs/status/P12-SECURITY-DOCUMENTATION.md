# P12 — Security automation and release provenance

**Status:** source controls complete; dependency-graph activation pending.

## Objective

Harden repository automation and future stable release provenance without
changing any APTA 1.0 specification, API, ABI, container or DSP contract.

## Delivered

- full-SHA pinning for external GitHub Actions;
- immutable digests for retained ESP-IDF build containers;
- an automated workflow-reference pin verifier;
- CodeQL analysis for the C/C++ library and public examples;
- pull-request dependency review with an explicit unavailable-feature preflight;
- monthly OpenSSF Scorecard analysis;
- grouped monthly Dependabot updates for GitHub Actions;
- deterministic SPDX 2.3 release SBOM generation and self-test;
- consolidated release checksums and GitHub artifact attestations for future
  stable publication;
- supply-chain documentation and explicit repository-owner settings checklist.

## Release boundary

P12 does not alter package `1.0.1`, public API `1.0.0`, `SOVERSION 1`,
container version 1, canonical `.apta` bytes or production analysis behaviour.
The existing `v1.0.1` release remains immutable and is not retroactively
attested. Provenance additions apply to the next stable release produced after
P12.

## Validation

- all source-controlled external action references pass the immutable-reference
  verifier;
- workflow and Dependabot YAML parse successfully;
- the SPDX generator produces byte-identical output for identical inputs and
  changes its package verification code when an input changes;
- the normal CI, security, Scorecard and relevant platform workflows remain the
  merge authority.

The repository dependency graph is currently disabled, so the full dependency
review gate cannot run yet. The source-controlled implementation is complete,
but P12 becomes operationally complete only after a repository owner enables
that setting and a subsequent pull request proves the real dependency-review
step. Other GitHub account and repository settings listed in the supply-chain
guide also require owner-side review.
