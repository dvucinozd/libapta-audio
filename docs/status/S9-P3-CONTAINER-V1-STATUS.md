# Stage S9 P3 — Container version 1 freeze status

**Status:** in progress  
**Branch:** `agent/s9-p3-container-v1-freeze`  
**Base:** P2 merge `816209b86232104e886e6f383e787c7a9e010c1b`  
**Work order:** [`../roadmap/APTA-1.0-WORK-ORDER.md`](../roadmap/APTA-1.0-WORK-ORDER.md)

## Audit boundary

P3 freezes the byte-level `.apta` container version 1 contract. It does not
change waveform, tempo or beatgrid DSP algorithms.

The initial audit covered:

- fixed header and directory validation;
- section flags, versions, alignment, overlap and CRC behaviour;
- `META`, `WOVR`, `WDTL`, `TEMP`, `LGRD`, `GGRD` and `REVN` readers and writers;
- canonical ordering and writer composition;
- strict/permissive reserved fields;
- partial-result and unknown-duration state;
- current independent fixtures and serialization tests.

## Resolved registry decisions

The normative `container-v1-registry.md` establishes:

- exactly one mandatory `WOVR` version-1 section;
- zero or more `WDTL` sections with globally unique tile identities;
- singleton `META`, `TEMP` and `LGRD` sections;
- `LGRD` requiring `TEMP`;
- one optional adjacent `GGRD`/`REVN` pair, with `GGRD` also requiring `TEMP`;
- canonical order `WOVR`, `WDTL`, `META`, `TEMP`, `LGRD`, `GGRD`, `REVN`;
- recognized unsupported versions returning unsupported rather than being
  skipped as unknown optionals;
- safe skipping but no required opaque preservation for unknown optional
  sections;
- a 96-byte canonical header and safe opaque skipping of future extended-header
  bytes;
- exact strict/permissive reserved-field boundaries;
- aggregate partial/finality and unknown-duration rules;
- criteria for section-version, FourCC and container-version evolution.

The one-WOVR decision matches the implemented reader, writer and public result
model. Multi-level overview serialization is deferred to a future section or
container version rather than being claimed without an interoperable API.

## Identified implementation work

Before P3 can close, the implementation and conformance suite must still:

- apply the registry's partial/finality rules consistently to TEMP, LGRD, GGRD
  and REVN;
- align strict/permissive defined-reserved handling across all recognized
  section readers;
- add registry-specific malformed and future-compatibility tests;
- add committed canonical legal-combination fixtures, including one full
  standard-section fixture;
- add versioned fixture-suite manifests and hashes;
- extend independent fixture validation beyond WOVR+META;
- verify canonical writer-reader-writer identity across supported platforms.

## Normative authority guard

`tests/spec/check_normative_manifest.py` verifies every manifest row against the
Git blob SHA-1 of the referenced UTF-8 file. The reference-fixture workflow runs
this check whenever normative specification files change.

## P3 exit gate

P3 remains open until:

- specification and implementation agree for every registry rule;
- canonical and malformed fixture manifests pass on 32-bit, 64-bit and Windows
  builds and under sanitizers;
- full standard-section interchange is independently exercised;
- container version remains `1` with no approved incompatibility report.
