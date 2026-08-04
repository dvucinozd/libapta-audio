# APTA 1.0.0

APTA 1.0.0 is the first stable release of the Adaptive Progressive Track
Analysis specification and the `libapta` portable reference implementation.

## Stable release identity

- specification: APTA 1.0;
- package and public API: 1.0.0;
- container: version 1;
- shared ABI: SOVERSION 1;
- license: Apache-2.0.

## Included

- progressive waveform, tempo, local/global beatgrid and dynamic-tempo models;
- push/pull PCM, bounded cooperative processing and immutable results;
- canonical META/WOVR/WDTL/TEMP/LGRD/GGRD/REVN interchange;
- installable static/shared core packages with CMake and pkg-config metadata;
- versioned public conformance and independent producer/consumer evidence;
- Linux, Windows, ILP32 and ESP-IDF integration evidence;
- fixed ASan/UBSan and nine-target libFuzzer release campaign.

## Compatibility statement

The final release differs from accepted candidate `1.0.0-rc.1` only in
version-bound metadata, final specification labels, publication documentation
and release automation. No public API, ABI, canonical wire bytes, parser/writer
behavior or production DSP contract changed.

## Qualification boundary

The release supports `WAVEFORM-1.0`, `ADAPTIVE-WAVEFORM-1.0`,
`CORE-ANALYSIS-1.0` and optional `+REFERENCE-WAVEFORM-1.0`. It does not define
reference tempo or reference beatgrid qualifiers.

## Known limitations

- no native big-endian release target was available;
- ESP-IDF evidence is firmware compile/link evidence, not physical-device
  execution;
- POSIX atomic replacement omits parent-directory `fsync()` after rename;
- tempo and beatgrid selection accuracy is outside semantic conformance.

See `CHANGELOG.md`, `SECURITY.md` and the final normative manifest for the
complete release boundary.
