# APTA 1.0 errata 1

**Status:** Approved editorial errata  
**Applies to:** APTA 1.0  
**Published with:** `libapta` 1.0.1

## Scope

This errata records documentation and release-identity corrections discovered
after the immutable `v1.0.0` publication. It does not change a normative
requirement, public symbol, structure layout, calling contract, container byte,
section interpretation, parser/writer rule or production analysis behavior.

## Corrections

1. Normative component documents are labelled **Final APTA 1.0** rather than
   **APTA 1.0 Release Candidate Draft**.
2. The master specification distinguishes package line `1.0.x` from public API
   `1.0.0`, while the specification remains 1.0 and the container remains
   version 1.
3. Current API, packaging, governance, contribution and file-format guides no
   longer describe the project as a pre-1.0 draft.
4. Public conformance-suite metadata and the ESP-IDF component manifest use the
   maintained `1.0.1` release identity.
5. Historical `0.x`, release-candidate and milestone evidence remains unchanged
   and is explicitly historical.

## Compatibility statement

`v1.0.0` remains the immutable first stable release. The `v1.0.1` maintenance
release republishes the corrected documentation and metadata while preserving:

- APTA specification semantics 1.0;
- public API 1.0.0, ABI `SOVERSION 1` and compatible package line 1.0.x;
- `.apta` container version 1 and all canonical fixture bytes;
- conformance profiles and qualifiers;
- production DSP behavior.
