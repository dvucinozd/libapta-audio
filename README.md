# libapta-audio

[![License: Apache-2.0](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)

`libapta-audio` is the home of the Adaptive Progressive Track Analysis (APTA) standard and its portable ISO C11 reference implementation.

APTA provides progressive, bounded and portable audio analysis for waveform, tempo, local beatgrid, global beatgrid and dynamic-tempo data. The reference implementation supports push and pull PCM, immutable result generations, static-workspace operation, bounded result slots, the versioned `.apta` container, POSIX reference desktop tools and an ESP-IDF platform component.

> Project status: functional implementation candidate through roadmap Stage S7. The specification remains Working Draft 0.1; the current public API is 0.3.0, and no stable API or ABI is claimed before APTA 1.0.

## Why APTA

Many audio-analysis libraries assume that the entire decoded track, a
filesystem, worker threads and an effectively unbounded heap are available.
APTA instead separates portable analysis from platform integration:

- useful immutable results can be published before whole-track analysis
  finishes;
- the host controls work through explicit frame and step budgets;
- playback focus and requested regions can reprioritize analysis;
- PCM can be pushed by the application or pulled through a callback;
- the core does not own codecs, filesystems, threads or scheduling;
- static workspace and bounded result slots support constrained devices;
- canonical `.apta` files move validated results between implementations.

The same public model can therefore serve desktop preprocessing, servers and
embedded DJ-player firmware without making the embedded integration depend on
the POSIX reference tools.

## Feature overview

| Area | Current reference support | Important boundary |
|---|---|---|
| Overview and detail waveform | Progressive peak/RMS results, focus-driven detail, three-band overview columns and WOVR/WDTL interchange | Detail tiles do not publish three-band values, and Core 0.1 does not mandate a bit-exact three-band filterbank |
| Tempo and local beatgrid | BPM candidates, confidence, locking and TEMP/LGRD interchange | Independent 188-track validation reopens the confidence-75 zero-metrical-error gate; Core 0.1 does not mandate a bit-exact tempo algorithm |
| Global grid and dynamic tempo | Segments, explicit beats, pending revisions, GGRD/REVN interchange and CLI selection/inspection | The S6 model remains draft and has no independent interoperability claim |
| Memory control | Custom allocator, memory budget, static session workspace and two-slot bounded result pools | Resource-class certification is not yet claimed |
| Desktop input | Reference WAV decoder for PCM16, packed PCM24, PCM32 and float32; mono/stereo | Other codecs require an application or third-party decoder backend |
| Embedded integration | ESP-IDF component, cooperative example, three bounded memory profiles and ESP32-P4 measurements | CI remains cross-build-only; repeatable target-specific stack, heap and latency evidence is still required for a resource-class claim |
| Portability | ISO C11 core, ESP-IDF component, and a native Windows file/decoder/CLI integration with UTF-8 paths | The Windows integration remains an implementation candidate until its MSVC CI gate is green |

## Functional implementation stages

- S0 — Foundation
- S1 — Portable core API
- S2 — Waveform Profile
- S3 — `.apta` container
- S4 — Tempo and local grid
- S5 — Reference desktop tools
- S6 — Global grid and dynamic tempo
- S7 — ESP-IDF port
- S8 — Second independent platform (Windows)

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
- [`tools/`](tools/) — `apta-analyze`, `apta-inspect`, `apta-validate`, the
  tempo harness and the read-only Rekordbox corpus importer.
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

Project participation and operations:

- [`CONTRIBUTING.md`](CONTRIBUTING.md) — development workflow, tests and pull
  request expectations.
- [`GOVERNANCE.md`](GOVERNANCE.md) — roles, decisions, review and release
  authority.
- [`SECURITY.md`](SECURITY.md) — supported versions and private vulnerability
  reporting.

## Current architecture draft

The original project architecture document is preserved at:

[`docs/architecture/APTA-ARCHITECTURE-DRAFT.md`](docs/architecture/APTA-ARCHITECTURE-DRAFT.md)

It remains an architecture draft and input to the future normative specification. Current implementation status and command behavior are recorded in the status/reference documents.

## Integrating the library

Package-manager and install rules are not implemented yet. For now, vendor the
repository or add it as a Git submodule, then include it from the parent CMake
project:

```cmake
set(APTA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(APTA_BUILD_DESKTOP_ADAPTERS OFF CACHE BOOL "" FORCE)
set(APTA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)

add_subdirectory(path/to/libapta-audio)
target_link_libraries(your_target PRIVATE apta::core)
```

Applications include the umbrella public header:

```c
#include <apta/apta.h>
```

The core API lifecycle is:

1. initialize `apta_context_config_t` and create a context;
2. initialize `apta_session_config_t` and create a push or pull session;
3. provide PCM and focus/region requests;
4. call `apta_session_process()` with bounded work budgets;
5. acquire immutable result generations and release each acquired result;
6. destroy the session, then the context after all retained results are
   released.

Public structures must be initialized with their matching `apta_*_init()`
function. Full ownership, threading, versioning and pull-source contracts are
in [`docs/api/`](docs/api/); a compact executable lifecycle example is
[`tests/unit/core_smoke.c`](tests/unit/core_smoke.c).

## Building

A default POSIX build compiles the core library, desktop adapters, reference tools and tests:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

For the portable core without POSIX file/decoder dependencies or CLI tools:

```bash
cmake -S . -B build-core \
  -DAPTA_BUILD_DESKTOP_ADAPTERS=OFF \
  -DAPTA_BUILD_TOOLS=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-core --parallel
ctest --test-dir build-core --output-on-failure
```

On Windows with Visual Studio 2022, the default build includes the Win32 file
adapter, WAV decoder and reference CLI tools:

```powershell
cmake -S . -B build-windows `
  -DAPTA_BUILD_DESKTOP_ADAPTERS=ON `
  -DAPTA_BUILD_TOOLS=ON
cmake --build build-windows --config Release --parallel
ctest --test-dir build-windows -C Release --output-on-failure
```

The build checks that the selected MSVC toolchain supports C11 atomics and
enables the required compiler option for the core and white-box tests. Windows
paths accepted by the desktop adapter are UTF-8 and are converted to UTF-16
before Win32 file operations.

### CMake options

- `APTA_BUILD_TESTS` — build runtime tests; default `ON`.
- `APTA_ENABLE_SANITIZERS` — enable AddressSanitizer and UndefinedBehaviorSanitizer with GCC or Clang; default `OFF`.
- `APTA_BUILD_FUZZING` — build libFuzzer targets; default `OFF`.
- `APTA_BUILD_DESKTOP_ADAPTERS` — build `apta::port_native` and
  `apta::decoder_wav`; the native port is POSIX or Windows; default `ON`.
- `APTA_BUILD_TOOLS` — build the three reference CLI tools; default `ON`.
- `APTA_WARNINGS_AS_ERRORS` — treat core, native adapter, decoder and tool
  compiler warnings as errors;
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
build/tools/apta-inspect track.apta --section GGRD
```

Validate it:

```bash
build/tools/apta-validate track.apta --strict
```

Detailed desktop-tool contract:

[`docs/reference/APTA-DESKTOP-TOOLS-0.1.md`](docs/reference/APTA-DESKTOP-TOOLS-0.1.md)

The command-line feature list exposes the supported waveform, tempo, local-grid,
global-grid, dynamic-tempo and locking features. `--features all` is derived
from the context capability mask, while `apta-inspect` supports both `GGRD` and
`REVN` in human-readable and JSON output.

## The `.apta` container

The container is little-endian, CRC32C-protected and directory based. Readers
validate all offsets, sizes, overlaps, versions, reserved fields and configured
resource limits before exposing recognized data.

| FourCC | Data |
|---|---|
| `WOVR` | One overview waveform level |
| `WDTL` | Optional detail waveform tiles |
| `META` | Optional deterministic structured metadata |
| `TEMP` | Optional selected tempo and ordered candidates |
| `LGRD` | Optional local constant-period beatgrid |
| `GGRD` | Optional global/dynamic grid segments or explicit beats |
| `REVN` | Revision identity and pending/applied revision state paired with `GGRD` |

The normative Core 0.1 format is
[`specification/file-format.md`](specification/file-format.md). Stage S6
`GGRD`/`REVN` implementation details are in
[`docs/reference/APTA-S6-CONTAINER-0.1.md`](docs/reference/APTA-S6-CONTAINER-0.1.md).
Unknown optional extensions are skipped; unsupported required extensions fail
explicitly.

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

The current default POSIX build registers 85 CTest tests. The Windows build
registers 84 tests (the POSIX compatibility-adapter test is omitted), including
native file, WAV pull-analysis, independently produced fixture and generated
CLI interchange coverage. A core-only build with desktop adapters and tools
disabled registers 72 tests.
The suite contains unit, generated-audio integration, malformed-input,
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

## Stability and validation boundaries

- APTA 0.1 is a working draft; source and format compatibility may change
  before 1.0.
- “Stage complete” means the listed reference functionality and its stated
  self-test evidence exist. It is not formal certification.
- CI cross-builds the ESP-IDF examples but does not flash or execute them on
  physical hardware.
- The Windows adapter is a Stage S8 implementation candidate. A green MSVC
  adapter/tool/runtime CI run is required before the roadmap marks S8 complete.
- Long-running fuzzing, cross-endian interoperability, a second independent
  implementation and repeatable performance measurements across intended
  devices remain release gates.

The current per-stage evidence and exact limitations are maintained in
[`docs/status/APTA-ROADMAP-STATUS.md`](docs/status/APTA-ROADMAP-STATUS.md).

## Contributing and security

Contributions are welcome. Start with
[`CONTRIBUTING.md`](CONTRIBUTING.md) for build profiles, test expectations,
specification changes and pull requests. Project decisions and maintainer
responsibilities are described in [`GOVERNANCE.md`](GOVERNANCE.md).

Do not report suspected vulnerabilities in public. Use the private contact and
coordinated-disclosure process in [`SECURITY.md`](SECURITY.md).

## License and copyright

The code, tests, examples, specification and documentation are available under
the [Apache License 2.0](LICENSE). This permissive license allows commercial
and non-commercial use, modification and redistribution subject to its notice
and license conditions, and includes an explicit patent grant from
contributors.

Copyright (c) Daniel Vučinović.
