# Draft: staged native Rust implementation preserving APTA contracts

Local draft only; not submitted and not maintainer-approved.

## Proposal

Implement the complete library in native Rust, starting from the explicitly
chosen `1.1.0` baseline on `rust-rewrite`. Keep the C implementation as a working
reference until the Rust implementation passes the full replacement matrix.
See [migration plan](MIGRATION.md) for scope, sequence and evidence boundaries.

## Compatibility impact

The intended replacement retains the published 1.x C API/ABI, container version
1, frozen 1.0 consumers and optional 1.1 sections. Initial experimental native
Rust modules are additive and do not replace the installed C library. No changes
to specification, package versions, acceptance thresholds or release promises
are proposed here. Breaking changes require a separate major-version decision.

## Validation

Use C as a development oracle, existing fixtures and conformance tests,
malformed-input/resource/ownership tests, supported platform builds and physical
ESP32-P4 measurements. Separate mechanical parity from algorithm improvements;
retain frozen evaluation provenance and unopened holdouts.

## Coordination still needed

Agree upstream on migration/review cadence, Rust toolchain support and eventual
C ABI/package switchover criteria. Approve any deliberate public-contract or
release changes separately. Existing DJ accuracy and physical hardware blockers
remain open; Rust does not by itself resolve them.
