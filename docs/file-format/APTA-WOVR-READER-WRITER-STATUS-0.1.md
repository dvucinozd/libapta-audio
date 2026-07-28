# APTA WOVR reader/writer status 0.1

**Status:** Implementation candidate  
**Container version:** 1  
**Supported waveform section:** `WOVR` version 1  
**Conformance claim:** Not yet claimed

## Implemented writer behaviour

The current canonical writer provides:

- a 96-byte container header;
- one 40-byte section-directory entry;
- one required `WOVR` version-1 section;
- little-endian integer encoding;
- exact total file size;
- source sample rate, channel count, channel layout and total source frames;
- sparse overview spans without fabricated gap columns;
- packed 10-byte waveform columns;
- lifecycle state encoding through normative `WOVR.flags`;
- `PARTIAL_RESULT` and `SOURCE_DURATION_UNKNOWN` container flags;
- CRC32C Castagnoli for the fixed header and `WOVR` payload;
- buffer size query and caller-provided output storage;
- deterministic reserialization of a parsed canonical file;
- serialization after the originating analysis session has been destroyed.

The writer performs no temporary heap allocation.

## Implemented reader behaviour

The bounded version-1 reader validates before constructing an immutable result:

- minimum header length and magic;
- supported container and specification versions;
- exact complete-buffer file size;
- header CRC32C;
- directory offset, alignment, count and bounds;
- configured file, section, span, waveform-column and allocation limits;
- section range overlap with the header, directory and other sections;
- section CRC32C;
- unsupported compression and encryption flags;
- unknown required section rejection;
- duplicate `WOVR` rejection;
- `WOVR` header, span-directory and packed-column bounds;
- sorted non-overlapping source spans;
- logical column bounds and source-frame geometry;
- shorter known final column handling;
- valid lifecycle and container-flag combinations;
- valid waveform column ranges and reserved column flag bits;
- strict zero requirements for defined reserved fields;
- source fingerprint kind `NONE` and its required zero bytes.

Successful parsing returns an immutable reference-counted `apta_result_t`. The result owns copied spans and waveform columns and does not retain pointers into the input buffer.

## Parse limits

`apta_parse_options_t` exposes bounded defaults and caller overrides for:

- maximum file bytes;
- maximum section count;
- maximum overview span count;
- maximum logical or packed waveform column count;
- maximum aggregate result allocation.

A zero limit field selects the library default rather than disabling the limit.

## Runtime tests

The current serialization tests include:

- canonical final `WOVR` golden-layout validation;
- sparse partial/unknown-duration writer validation;
- canonical writer → reader → writer byte-identical round trip;
- parser file-size and allocation limits;
- truncated input;
- invalid magic;
- invalid header CRC;
- invalid directory alignment;
- invalid section CRC;
- unknown required section;
- strict and non-strict reserved-directory behaviour;
- invalid span geometry;
- invalid reserved waveform-column flags.

## Deliberate limitations

The implementation does not yet provide:

- streaming or incremental parsing;
- memory-mapped zero-copy result views;
- `META` parsing or writing;
- `WDTL` parsing or writing;
- compressed or encrypted sections;
- preservation of unknown optional sections during rewrite;
- non-zero source fingerprint kinds;
- multiple overview levels;
- tempo or beatgrid sections;
- a fuzzing corpus integrated with a sanitizer matrix;
- a formal Waveform Profile conformance report.

## Completion gates

This package can be treated as a verified implementation candidate when:

- the newest GitHub Actions run compiles the reader on the configured C and C++ header probes;
- all 17 runtime tests pass;
- compiler warnings remain clean;
- sanitizer and fuzz targets are added for untrusted parser input;
- final multi-span coverage and duplicate packed-data edge cases are represented in the malformed corpus;
- the status document records the verified commit SHA.
