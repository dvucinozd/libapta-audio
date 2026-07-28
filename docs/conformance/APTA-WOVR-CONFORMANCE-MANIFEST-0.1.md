# APTA WOVR conformance manifest 0.1

**Status:** Verified implementation-candidate manifest  
**Verified commit:** `6a0db75208774ab2511b463370391bc458108aaf`  
**Verification evidence:** GitHub Actions workflow `fix(ci): restore last verified overview and parser build #134` completed successfully.

## Scope

This manifest records the currently verified behaviour of the version-1 `.apta` container implementation for the required `WOVR` section. It is not yet a formal Waveform Profile conformance claim.

## Required writer checks

- canonical little-endian container encoding;
- fixed 96-byte header and 40-byte directory entry;
- exact file-size accounting;
- deterministic `WOVR` section layout;
- CRC32C Castagnoli for the header and section payload;
- sparse span preservation without fabricated columns;
- final and partial lifecycle flag encoding;
- source metadata retained after session destruction;
- caller-provided output buffer and explicit size query;
- deterministic writer → reader → writer byte identity.

## Required reader checks

- bounded complete-buffer parsing;
- supported version validation;
- header and section CRC validation;
- directory, section, span and packed-column bounds;
- section overlap rejection;
- unknown required section rejection;
- duplicate `WOVR` rejection;
- strict reserved-field validation;
- source-frame and logical-column geometry validation;
- continuous `FINAL` coverage;
- non-overlapping packed-column intervals;
- allocation, file, section, span and column limits;
- complete cleanup after every injected allocation failure;
- immutable copied result ownership.

## Verified runtime tests

The verified build registers 19 runtime tests. The serialization/parser subset includes:

- `apta.serialization.wovr_writer`;
- `apta.serialization.wovr_writer_partial`;
- `apta.serialization.wovr_roundtrip`;
- `apta.serialization.wovr_reader_malformed`;
- `apta.serialization.wovr_reader_hardening`;
- `apta.serialization.wovr_reader_allocation_failure`.

The same workflow also executes the core, lifecycle, memory, waveform overview, publication-retry, concurrency and cancellation tests.

## Hardening configuration

The repository provides:

- AddressSanitizer and UndefinedBehaviorSanitizer opt-in builds;
- a bounded libFuzzer target for untrusted `.apta` input;
- deterministic final and sparse-partial seed generation;
- an APTA-specific libFuzzer dictionary;
- a bounded CI fuzz-smoke run.

## Remaining gates for a formal profile claim

- independent ESP-IDF reader validation;
- maintained long-running fuzz corpus and campaign report;
- normative `META` and `WDTL` support required by the final Waveform Profile;
- cross-platform golden vectors, including a 32-bit target;
- final profile identifier and governance approval.
