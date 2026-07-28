# APTA waveform container conformance manifest 0.1

**Status:** Verified implementation-candidate manifest  
**Verified merge commit:** `e9eef869578656a25a00ed583b8c657e71549f33`  
**Primary verification:** GitHub Actions PR CI run `#147`  
**Serialization verification:** PR CI runs `#143` and `#145`

## Scope

This manifest records self-tested behaviour of the version-1 `.apta` waveform container implementation:

- required `WOVR`; and
- optional `WDTL` when detail tiles are available.

It is not a formal profile or certification claim.

## Writer checks

- canonical little-endian container encoding;
- fixed 96-byte header and 40-byte directory entries;
- exact file-size and eight-byte alignment accounting;
- deterministic `WOVR` layout;
- canonical `WOVR`, then optional `WDTL` directory order;
- CRC32C Castagnoli for header and section payloads;
- sparse span preservation without fabricated columns;
- detail tile identity, state, confidence and packed columns;
- final and partial lifecycle encoding;
- source metadata retained after session destruction;
- caller-provided output buffer and explicit size query;
- overview-only byte compatibility after WDTL support;
- deterministic writer → reader → writer byte identity.

## Reader checks

- bounded complete-buffer parsing;
- supported container and section version validation;
- header and section CRC validation;
- directory, section, span, tile and packed-column bounds;
- section and packed-payload overlap rejection;
- unknown required section rejection;
- safe skipping of unknown optional sections;
- duplicate required `WOVR` rejection;
- duplicate detail tile identity rejection;
- strict reserved-field validation;
- source-frame, logical-column and detail-tile geometry validation;
- continuous `FINAL` overview coverage;
- lifecycle, confidence and column-flag validation;
- file, section, span, column and allocation limits;
- immutable copied result ownership;
- complete cleanup after every injected WOVR/WDTL parser allocation failure.

## Verified runtime tests

The verified build registers 28 runtime tests. The serialization/parser subset includes:

- `apta.serialization.wovr_writer`;
- `apta.serialization.wovr_writer_partial`;
- `apta.serialization.wovr_roundtrip`;
- `apta.serialization.wovr_reader_malformed`;
- `apta.serialization.wovr_reader_hardening`;
- `apta.serialization.wovr_reader_allocation_failure`;
- `apta.serialization.wdtl_roundtrip`;
- `apta.serialization.wdtl_reader_malformed`;
- `apta.serialization.wdtl_reader_allocation_failure`.

The same workflow executes the core, lifecycle, memory, overview/detail processing, publication-retry, scheduler, concurrency and cancellation tests.

## Hardening configuration

The repository provides:

- AddressSanitizer and UndefinedBehaviorSanitizer builds;
- a bounded libFuzzer target for untrusted complete `.apta` input;
- deterministic final, sparse-partial and WDTL seed generation;
- an APTA-specific libFuzzer dictionary;
- a bounded CI fuzz-smoke run;
- Nth-allocation failure sweeps for WOVR and WDTL parsing.

## Profile interpretation

`WOVR` is required by `APTA-WAVEFORM-0.1`.

`WDTL`, partial `.apta` results and `META` are optional Waveform Profile capabilities. WDTL and partial waveform results are implemented. Deterministic `META` support remains deferred.

## Remaining gates for a formal profile claim

- machine-readable fixture manifest and recorded manifest hash;
- exhaustive fixed-header truncation fixtures;
- 32-bit ABI/build execution;
- cross-endian or independently produced container fixtures;
- maintained long-running fuzz campaign and minimized corpus report;
- independent ESP-IDF reader/writer validation;
- final governance approval for an official profile claim.
