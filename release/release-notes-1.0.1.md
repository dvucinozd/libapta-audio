# APTA 1.0.1

APTA 1.0.1 is a documentation, metadata and release-automation coherence patch
for the stable APTA 1.0 specification and `libapta` reference implementation.

## Corrected

- final status labels across the normative APTA 1.0 component documents;
- stable API/package, governance, contribution and file-format guidance;
- public conformance-suite and ESP-IDF component version metadata;
- version-derived release packaging and publication automation;
- normative manifest hashes, accompanied by APTA 1.0 errata 1.

## Compatibility statement

This release changes no public symbol, public structure layout, shared-library
ABI, canonical `.apta` bytes, parser/writer rule or production DSP behavior.
The specification remains APTA 1.0, the package is 1.0.1, the public API is
1.0.0, the container is version 1 and the shared-library ABI remains `SOVERSION 1`.

The `v1.0.0` tag remains the immutable first stable publication.

## Known limitations

The known limitations from 1.0.0 remain unchanged: no supported native
big-endian release target, hosted ESP-IDF compile/link rather than physical-board
execution, no parent-directory `fsync()` after POSIX atomic replacement, and no
semantic conformance claim for tempo/beatgrid selection accuracy.

The project remains licensed under Apache-2.0.
