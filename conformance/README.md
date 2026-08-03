# APTA public conformance suite 1.0

This directory is the black-box APTA 1.0 conformance suite. It is a separate
CMake project and links only an installed implementation's public package:

```cmake
find_package(APTA 1.0 CONFIG REQUIRED)
target_link_libraries(test PRIVATE apta::core)
```

No source under `src/`, no private header and no libapta test helper is required.
A third-party implementation can run the suite by exposing the same public
headers, ABI and CMake target contract.

## Profiles

- `WAVEFORM-1.0` validates public initialization/version rules, context and
  session lifecycle, overview waveform semantics and canonical serialization.
- `ADAPTIVE-WAVEFORM-1.0` extends WAVEFORM with region-request, pull and detail
  tile lifecycle semantics.
- `CORE-ANALYSIS-1.0` extends ADAPTIVE-WAVEFORM with tempo/grid semantic ranges,
  revision lifecycle and canonical container fixture consumption.
- `REFERENCE-WAVEFORM-1.0` is an optional exact-vector qualifier. It is the only
  reference DSP qualifier in the 1.0 suite. No reference-tempo qualifier exists.

Tempo or beatgrid selection accuracy is not semantic conformance. The CORE
tests require legal states, ranges, candidate ordering, revision behavior and
serializable public results, but do not require a selected BPM or grid phase to
match the reference implementation.

## Running against an installed package

```sh
cmake -S conformance -B build-conformance \
  -DCMAKE_PREFIX_PATH=/path/to/apta-prefix \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-conformance --target apta_conformance_report
```

The deterministic report is written to
`build-conformance/apta-conformance-report.json`.

Claims can be overridden for another implementation:

```sh
cmake -S conformance -B build-conformance \
  -DCMAKE_PREFIX_PATH=/path/to/implementation-prefix \
  -DAPTA_CONFORMANCE_PROFILES="WAVEFORM-1.0;ADAPTIVE-WAVEFORM-1.0" \
  -DAPTA_CONFORMANCE_QUALIFIERS="" \
  -DAPTA_CONFORMANCE_IMPLEMENTATION_NAME=my-implementation \
  -DAPTA_CONFORMANCE_SOURCE_REVISION=<revision>
```

Skipping any mandatory test invalidates the associated profile claim. Optional
tests and explicit platform exemptions remain visible in the report without
silently weakening mandatory profile sets.

## Versioned artifacts

- `suite-manifest.json` defines profiles, qualifiers and mandatory/optional
  cases.
- `fixtures/manifest.json` records fixture producers, generators, sizes and
  SHA-256 hashes.
- `report-schema.json` defines the portable JSON report surface.
- `run_suite.py` executes only built public test binaries and emits stable,
  sorted JSON without timestamps or elapsed-time noise.

The reference implementation runs this same project against a staged
installation on Linux, ILP32 and Windows. Internal white-box regression,
performance and fuzz/security evidence stays in the normal implementation CI
and is classified separately by `tests/classification/rules.json`.
