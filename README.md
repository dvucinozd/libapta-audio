# libapta-audio

[![License: Apache-2.0](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)

`libapta-audio` is the home of the Adaptive Progressive Track Analysis (APTA)
standard and its portable ISO C11 reference implementation.

> **Release status:** APTA specification 1.0, public API 1.0.0, package 1.0.1,
> container version 1 and shared-library `SOVERSION 1`.

APTA provides progressive, bounded and portable audio analysis for waveform,
tempo, local beatgrid, global beatgrid and dynamic-tempo data. The host retains
control of decoding, playback, storage, scheduling and device resources.

## Stable 1.0 scope

- push and pull PCM input;
- bounded cooperative processing and cancellation;
- playback focus and requested-region prioritisation;
- immutable result generations;
- static session workspaces and bounded result slots;
- overview/detail waveform and optional three-band overview data;
- BPM candidates, local and global grid, dynamic tempo and revisions;
- canonical `.apta` container version 1 interchange;
- installable static/shared core packages;
- public conformance profiles and retained multi-platform evidence.

The stable version-1 section set is:

```text
META WOVR WDTL TEMP LGRD GGRD REVN
```

Musical key, downbeat/meter/phrase classification, codec ownership, playback,
USB/filesystem/network ownership, user-interface policy, three-band detail
tiles and bit-exact reference tempo/beatgrid algorithms are outside the APTA
1.0 core.

## Conformance claims

APTA 1.0 defines:

- `WAVEFORM-1.0`;
- `ADAPTIVE-WAVEFORM-1.0`;
- `CORE-ANALYSIS-1.0`;
- optional `+REFERENCE-WAVEFORM-1.0`.

No `REFERENCE-TEMPO-1.0` or `REFERENCE-BEATGRID-1.0` claim is made. POSIX,
Windows and ESP-IDF results are platform integrations of the same `libapta`
implementation, not independent DSP implementations.

## Installing the core package

A packaged installation exposes the stable `apta::core` target:

```cmake
find_package(APTA 1.0 CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE apta::core)
```

On Unix-like systems, `libapta.pc` is also installed for pkg-config consumers.

To vendor the source tree:

```cmake
set(APTA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(APTA_BUILD_DESKTOP_ADAPTERS OFF CACHE BOOL "" FORCE)
set(APTA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)

add_subdirectory(path/to/libapta-audio)
target_link_libraries(your_target PRIVATE apta::core)
```

Applications include:

```c
#include <apta/apta.h>
```

## Building and testing

Default native build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Portable core only:

```bash
cmake -S . -B build-core \
  -DAPTA_BUILD_DESKTOP_ADAPTERS=OFF \
  -DAPTA_BUILD_TOOLS=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-core --parallel
ctest --test-dir build-core --output-on-failure
```

Windows with Visual Studio 2022:

```powershell
cmake -S . -B build-windows `
  -DAPTA_BUILD_DESKTOP_ADAPTERS=ON `
  -DAPTA_BUILD_TOOLS=ON
cmake --build build-windows --config Release --parallel
ctest --test-dir build-windows -C Release --output-on-failure
```

Sanitizer and fuzz build:

```bash
CC=clang CXX=clang++ cmake -S . -B build-sanitized \
  -DAPTA_BUILD_TESTS=ON \
  -DAPTA_ENABLE_SANITIZERS=ON \
  -DAPTA_BUILD_FUZZING=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-sanitized --parallel
ctest --test-dir build-sanitized --output-on-failure
```

## Platform boundary

- POSIX and Windows provide native file, WAV decoder and reference CLI
  integrations.
- ESP-IDF 5.5.4 and 6.0.2 firmware builds are supported for the retained matrix.
- Hosted ESP-IDF CI proves compile/link integration; it does not claim execution
  on physical hardware.
- No supported native big-endian release target was available for 1.0;
  deterministic byte-swap evidence is retained.

## Documentation

- [`specification/APTA-SPEC.md`](specification/APTA-SPEC.md) — stable APTA 1.0
  master specification.
- [`specification/APTA-1.0-NORMATIVE-MANIFEST.md`](specification/APTA-1.0-NORMATIVE-MANIFEST.md)
  — exact normative files and blob hashes.
- [`specification/APTA-1.0-ERRATA-1.md`](specification/APTA-1.0-ERRATA-1.md) —
  editorial corrections published with 1.0.1.
- [`docs/status/APTA-ROADMAP-STATUS.md`](docs/status/APTA-ROADMAP-STATUS.md) —
  release and roadmap status.
- [`docs/api/APTA-API-ABI-1.0.md`](docs/api/APTA-API-ABI-1.0.md) — public API and
  ABI contract.
- [`conformance/`](conformance/) — versioned public conformance suite.
- [`CHANGELOG.md`](CHANGELOG.md) — compatibility boundary and known limitations.
- [`SECURITY.md`](SECURITY.md) — supported versions and private reporting.

## Repository layout

- `specification/` — normative APTA 1.0 documents;
- `include/apta/` — public C API;
- `src/` — portable core and optional desktop implementation;
- `ports/` — platform integrations, including ESP-IDF and Windows;
- `tools/` — reference desktop command-line tools;
- `conformance/` — installed-package public conformance suite;
- `interoperability/` — independent producer/consumer evidence;
- `tests/` — unit, integration, compatibility, package, security and fuzz tests;
- `release/` — frozen RC and final-release verification material.

The project is distributed under the [Apache License 2.0](LICENSE).
