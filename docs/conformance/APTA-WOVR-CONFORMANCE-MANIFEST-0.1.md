# APTA waveform container conformance manifest 0.1

**Status:** Verified implementation-candidate manifest  
**Verified merge commit:** `2bdbf32f429a0aa24c30937e0f078e744f0653dd`  
**Primary verification:** GitHub Actions PR CI run `#153`  
**Serialization verification:** PR CI runs `#143`, `#145`, `#151` and `#153`

## Scope

This manifest records self-tested behaviour of the version-1 `.apta` waveform container implementation:

- required `WOVR`;
- optional `WDTL` when detail tiles are available; and
- optional deterministic-CBOR `META`.

It is not a formal profile or certification claim.

## Writer checks

- canonical little-endian container encoding;
- fixed 96-byte header and 40-byte directory entries;
- exact file-size and eight-byte alignment accounting;
- deterministic `WOVR` layout;
- canonical `WOVR`, optional `WDTL`, optional `META` directory order;
- CRC32C Castagnoli for header and section payloads;
- sparse span preservation without fabricated columns;
- detail tile identity, state, confidence and packed columns;
- bounded owned metadata with deterministic CBOR encoding;
- explicit preservation of present empty metadata values;
- final and partial lifecycle encoding;
- source and application metadata retained after session destruction;
- caller-provided output buffer and explicit size query;
- waveform-only byte compatibility after WDTL and META support;
- deterministic writer → reader → writer byte identity.

## Reader checks

- bounded complete-buffer parsing;
- supported container and section version validation;
- header and section CRC validation;
- directory, section, span, tile and packed-column bounds;
- section and packed-payload overlap rejection;
- unknown required section rejection;
- safe skipping of unknown optional sections;
- duplicate required `WOVR` and duplicate `META` rejection;
- duplicate detail tile identity rejection;
- strict reserved-field validation;
- source-frame, logical-column and detail-tile geometry validation;
- continuous `FINAL` overview coverage;
- lifecycle, confidence and column-flag validation;
- canonical recognized META key/type/order/length validation;
- recognized metadata UTF-8 and fixed-size limit validation;
- file, section, span, column and allocation limits;
- immutable copied result ownership;
- complete cleanup after injected WOVR/WDTL/META allocation failures;
- rejection of every byte-prefix truncation of canonical WOVR, WDTL and META fixtures;
- rejection of valid canonical files with one trailing byte.

## Verified runtime tests

The verified build registers 34 runtime tests. The serialization/parser subset includes:

- `apta.serialization.wovr_writer`;
- `apta.serialization.wovr_writer_partial`;
- `apta.serialization.wovr_roundtrip`;
- `apta.serialization.wovr_reader_malformed`;
- `apta.serialization.wovr_reader_hardening`;
- `apta.serialization.wovr_reader_allocation_failure`;
- `apta.serialization.wdtl_roundtrip`;
- `apta.serialization.wdtl_reader_malformed`;
- `apta.serialization.wdtl_reader_allocation_failure`;
- `apta.serialization.meta_roundtrip`;
- `apta.serialization.meta_wdtl_roundtrip`;
- `apta.serialization.meta_reader_malformed`;
- `apta.serialization.meta_allocation_failure`;
- `apta.serialization.container_truncation`.

The same workflow executes core, metadata lifecycle, memory, overview/detail processing, publication-retry, scheduler, concurrency and cancellation tests.

## Hardening configuration

The repository provides:

- AddressSanitizer and UndefinedBehaviorSanitizer builds;
- a bounded libFuzzer target for untrusted complete `.apta` input;
- deterministic final, sparse-partial, WDTL and META seed generation;
- an APTA/CBOR-specific libFuzzer dictionary;
- a bounded CI fuzz-smoke run;
- Nth-allocation failure sweeps for WOVR, WDTL and META parsing;
- exhaustive canonical prefix truncation for all currently emitted section combinations.

## Profile interpretation

`WOVR` is required by `APTA-WAVEFORM-0.1`.

`WDTL`, partial `.apta` results and `META` are optional Waveform Profile capabilities. All three are implemented and covered by canonical writer/reader tests.

## Remaining gates for a formal profile claim

- machine-readable fixture manifest and recorded manifest hash;
- 32-bit ABI/build execution;
- cross-endian or independently produced container fixtures;
- maintained long-running fuzz campaign and minimized corpus report;
- independent ESP-IDF reader/writer validation;
- final governance approval for an official profile claim.
