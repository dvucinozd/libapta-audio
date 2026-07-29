# libapta-audio

`libapta-audio` is the home of the Adaptive Progressive Track Analysis (APTA) standard and its portable ISO C11 reference implementation.

APTA provides progressive, bounded and portable audio analysis for waveform, tempo, local beatgrid, global beatgrid and dynamic-tempo data. The reference implementation supports push and pull PCM, immutable result generations, static-workspace operation, bounded result slots, the versioned `.apta` container, POSIX reference desktop tools and an ESP-IDF platform component.

> Project status: functional implementation candidate through roadmap Stage S7. The specification, public API and ABI remain draft 0.1 and are not yet APTA 1.0 stable.

## Functional implementation stages

- S0 — Foundation
- S1 — Portable core API
- S2 — Waveform Profile
- S3 — `.apta` container
- S4 — Tempo and local grid
- S5 — Reference desktop tools
- S6 — Global grid and dynamic tempo
- S7 — ESP-IDF port

Current roadmap status:

[`docs/status/APTA-ROADMAP-STATUS.md`](docs/status/APTA-ROADMAP-STATUS.md)

Stage S7 status and evidence:

- [`docs/status/S7-ESP-IDF-PORT-STATUS.md`](docs/status/S7-ESP-IDF-PORT-STATUS.md)
- [`ports/espidf/README.md`](ports/espidf/README.md)
- [`docs/reference/APTA-ESP-IDF-MEMORY-PROFILES-0.1.md`](docs/reference/APTA-ESP-IDF-MEMORY-PROFILES-0.1.md)
- [`docs/conformance/APTA-S7-READINESS-0.1.md`](docs/conformance/APTA-S7-READINESS-0.1.md)

Stage S6 status and evidence:

- [`docs/status/S6-GLOBAL-GRID-DYNAMIC-TEMPO-STATUS.md`](docs/status/S6-GLOBAL-GRID-DYNAMIC-TEMPO-STATUS.md)
- [`docs/reference/APTA-S6-CONTAINER-0.1.md`](docs/reference/APTA-S6-CONTAINER-0.1.md)
- [`docs/conformance/APTA-S6-READINESS-0.1.md`](docs/conformance/APTA-S6-READINESS-0.1.md)

## Repository layout

- [`specification/`](specification/) — normative APTA specification documents.
- [`docs/`](docs/) — architecture, API, format, porting, review and roadmap documentation.
- [`include/apta/`](include/apta/) — public C API headers.
- [`src/`](src/) — portable core plus optional desktop adapter implementation.
- [`backends/`](backends/) — reserved scaffolding for future replaceable DSP
  backend packages; the current reference algorithms are built from `src/`.
- [`ports/`](ports/) — platform integration layers, including ESP-IDF.
- [`tools/`](tools/) — `apta-analyze`, `apta-inspect` and `apta-validate`.
- [`tests/`](tests/) — unit, integration, conformance, fuzz and generated-fixture tests.
- [`examples/`](examples/) — usage and platform examples.
- [`packaging/`](packaging/) — reserved packaging scaffolding; install and
  package-manager rules are not implemented yet.

## Documentation

- [`specification/README.md`](specification/README.md) — normative Working
  Draft 0.1 document set.
- [`docs/README.md`](docs/README.md) — API contracts, implementation evidence,
  architecture, platform guidance and the current roadmap status.
- [`docs/status/APTA-ROADMAP-STATUS.md`](docs/status/APTA-ROADMAP-STATUS.md) —
  current implementation and validation boundary.

Documents that name a source commit or CI run are evidence snapshots for that
milestone. They are intentionally not rewritten to imply that the historical
run validated later features.

## Current architecture draft

The original project architecture document is preserved at:

[`docs/architecture/APTA-ARCHITECTURE-DRAFT.md`](docs/architecture/APTA-ARCHITECTURE-DRAFT.md)

It remains an architecture draft and input to the future normative specification. Current implementation status and command behavior are recorded in the status/reference documents.

## Building

A default POSIX build compiles the core library, desktop adapters, reference tools and tests:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### CMake options

- `APTA_BUILD_TESTS` — build runtime tests; default `ON`.
- `APTA_ENABLE_SANITIZERS` — enable AddressSanitizer and UndefinedBehaviorSanitizer with GCC or Clang; default `OFF`.
- `APTA_BUILD_FUZZING` — build libFuzzer targets; default `OFF`.
- `APTA_BUILD_DESKTOP_ADAPTERS` — build `apta::port_posix` and `apta::decoder_wav`; default `ON`.
- `APTA_BUILD_TOOLS` — build the three reference CLI tools; default `ON`.
- `APTA_WARNINGS_AS_ERRORS` — treat `apta::core` compiler warnings as errors;
  default `OFF`.

The tool build requires the desktop adapters.

Example sanitized build:

```bash
CC=clang CXX=clang++ cmake -S . -B build-sanitized \
  -DAPTA_BUILD_TESTS=ON \
  -DAPTA_ENABLE_SANITIZERS=ON \
  -DAPTA_BUILD_FUZZING=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-sanitized --parallel
ctest --test-dir build-sanitized --output-on-failure
```

## ESP-IDF quick start

The reference ESP-IDF component supports the verified build range from ESP-IDF 5.5.4 through 6.0.2.

A complete cooperative example is located at:

```text
examples/espidf/cooperative_scheduler
```

Build the scalar ESP32 configuration:

```bash
cd examples/espidf/cooperative_scheduler
idf.py set-target esp32
idf.py build
```

Build the ESP32-S3 configuration with the optional ESP-DSP helper:

```bash
idf.py -B build-esp32s3 \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.dsp.defaults" \
  set-target esp32s3 build
```

Detailed component integration:

[`ports/espidf/README.md`](ports/espidf/README.md)

The CI matrix cross-compiles linked firmware for ESP-IDF 5.5.4/ESP32, ESP-IDF 6.0.2/ESP32 and ESP-IDF 6.0.2/ESP32-S3 with ESP-DSP. It does not flash or execute firmware on physical hardware.

## Desktop quick start

The built-in reference decoder accepts RIFF/WAVE PCM16, packed PCM24, PCM32 and IEEE float32, mono or stereo. Other codecs require another decoder backend or application-side decode-to-PCM integration.

Analyze a WAV file:

```bash
build/tools/apta-analyze track.wav --output track.apta
```

Generate waveform-only output:

```bash
build/tools/apta-analyze track.wav --output track.apta --profile waveform
```

Inspect the result:

```bash
build/tools/apta-inspect track.apta
build/tools/apta-inspect track.apta --json
build/tools/apta-inspect track.apta --section LGRD
```

Validate it:

```bash
build/tools/apta-validate track.apta --strict
```

Detailed desktop-tool contract:

[`docs/reference/APTA-DESKTOP-TOOLS-0.1.md`](docs/reference/APTA-DESKTOP-TOOLS-0.1.md)

The current S5 command-line feature list and inspector cover waveform, detail,
tempo and local-grid data. Stage S6 global-grid and revision data are available
through the library API and container reader/writer, but `apta-analyze` and
`apta-inspect --section` do not yet expose `GGRD` or `REVN`.

## CMake targets

Current native reference targets include:

```text
apta::headers
apta::core
apta::port_posix
apta::decoder_wav
```

The command-line executables are:

```text
apta-analyze
apta-inspect
apta-validate
```

The ESP-IDF integration is an IDF component under `ports/espidf`, not a native CMake alias target.

## Testing

The default POSIX build registers 69 CTest tests: 59 portable core tests, four
POSIX adapter/decoder tests and six generated CLI fixture tests. A core-only
build with desktop adapters and tools disabled registers 59 tests. The suite
contains unit, generated-audio integration, malformed-input,
allocation-failure, concurrency, exhaustive truncation and parser-hardening
coverage; sanitizer and fuzz-smoke execution are separate CI steps.

```bash
ctest --test-dir build --output-on-failure
```

Stage S7 additionally provides:

- the `apta.port.espidf` host-stub adapter regression;
- three bounded embedded memory profiles;
- ESP-IDF 5.5.4 and 6.0.2 firmware cross-builds;
- scalar ESP32 and ESP-DSP ESP32-S3 link coverage.

The reference workflows generate click-track WAV and `.apta` parser seeds at runtime. No third-party audio recording is required or committed.

## License and copyright

The repository currently has a licensing inconsistency that must be resolved
before a release: [`LICENSE`](LICENSE) contains MIT terms, while source files
carry `SPDX-License-Identifier: Apache-2.0`. Neither this README nor the
architecture draft resolves that conflict.

Copyright (c) Daniel Vučinović.
