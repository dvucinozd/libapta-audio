# libapta-audio

`libapta-audio` is the home of the Adaptive Progressive Track Analysis (APTA) standard and its portable ISO C11 reference implementation.

APTA provides progressive, bounded and portable audio analysis for waveform, tempo and local beatgrid data. The reference implementation supports push and pull PCM, immutable result generations, static-workspace operation, the versioned `.apta` container and POSIX reference desktop tools.

> Project status: functional implementation candidate through roadmap Stage S5. The specification, public API and ABI remain draft 0.1 and are not yet APTA 1.0 stable.

## Implemented stages

- S0 — Foundation
- S1 — Portable core API
- S2 — Waveform Profile
- S3 — `.apta` container
- S4 — Tempo and local grid
- S5 — Reference desktop tools

Current roadmap status:

[`docs/status/APTA-ROADMAP-STATUS.md`](docs/status/APTA-ROADMAP-STATUS.md)

## Repository layout

- [`specification/`](specification/) — normative APTA specification documents.
- [`docs/`](docs/) — architecture, API, format, porting, review and roadmap documentation.
- [`include/apta/`](include/apta/) — public C API headers.
- [`src/`](src/) — portable core plus optional desktop adapter implementation.
- [`backends/`](backends/) — replaceable DSP backends.
- [`ports/`](ports/) — platform integration layers.
- [`tools/`](tools/) — `apta-analyze`, `apta-inspect` and `apta-validate`.
- [`tests/`](tests/) — unit, integration, conformance, fuzz and generated-fixture tests.
- [`examples/`](examples/) — usage and platform examples.
- [`packaging/`](packaging/) — build-system and package-manager integration.

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

## CMake targets

Current reference targets include:

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

## Testing

The current suite contains unit, generated-audio integration, malformed-input, allocation-failure, concurrency, sanitizer, truncation and fuzz-smoke coverage.

```bash
ctest --test-dir build --output-on-failure
```

The Stage S5 workflow generates its click-track WAV at runtime. No third-party audio recording is required or committed.

## License and copyright

See [`LICENSE`](LICENSE) and the SPDX identifier in each source file.

Copyright (c) Daniel Vučinović.
