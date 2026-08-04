# libapta-audio

[![CI](https://github.com/dvucinozd/libapta-audio/actions/workflows/ci.yml/badge.svg)](https://github.com/dvucinozd/libapta-audio/actions/workflows/ci.yml)
[![ESP-IDF](https://github.com/dvucinozd/libapta-audio/actions/workflows/espidf.yml/badge.svg)](https://github.com/dvucinozd/libapta-audio/actions/workflows/espidf.yml)
[![Latest release](https://img.shields.io/github/v/release/dvucinozd/libapta-audio)](https://github.com/dvucinozd/libapta-audio/releases/latest)
[![C11](https://img.shields.io/badge/language-ISO%20C11-blue)](https://en.cppreference.com/w/c/11)
[![License: Apache-2.0](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)

`libapta-audio` is the home of the Adaptive Progressive Track Analysis (APTA)
standard and its portable ISO C11 reference implementation.

> **Release status:** APTA specification 1.0, public API 1.0.0, package 1.0.1,
> container version 1 and shared-library `SOVERSION 1`.

APTA provides progressive, bounded and portable audio analysis for waveform,
tempo, local beatgrid, global beatgrid and dynamic-tempo data. The host retains
control of decoding, playback, storage, scheduling and device resources.

## Why APTA?

Traditional track-analysis pipelines often assume that a complete file can be
decoded, analysed and stored before a user needs the result. That model is a poor
fit for embedded devices, live browsing, removable media and applications that
must keep playback responsive.

APTA separates analysis from codec and I/O ownership. A host can feed PCM or
expose a pull source, assign a bounded amount of work, prioritize a playback
region and consume immutable result generations as they become available. The
same public contracts apply on desktop and embedded targets, and the canonical
`.apta` container lets independent producers and consumers exchange results.

## Five-minute quick start

Build the portable core and the included examples:

```bash
cmake -S . -B build-quickstart \
  -DAPTA_BUILD_EXAMPLES=ON \
  -DAPTA_BUILD_TESTS=ON \
  -DAPTA_BUILD_DESKTOP_ADAPTERS=OFF \
  -DAPTA_BUILD_TOOLS=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-quickstart --parallel
ctest --test-dir build-quickstart -L examples --output-on-failure
```

The smallest push-mode flow is:

```c
#include <stdint.h>
#include <apta/apta.h>

int main(void)
{
    int16_t pcm[4] = {0, 12000, -12000, 0};
    apta_context_config_t cc;
    apta_session_config_t sc;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    apta_context_t *context = 0;
    apta_session_t *session = 0;
    uint32_t accepted = 0;
    apta_status_t status;

    apta_context_config_init(&cc);
    cc.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    if (apta_context_create(&cc, &context) != APTA_STATUS_OK) return 1;

    apta_session_config_init(&sc);
    sc.input_mode = APTA_INPUT_MODE_PUSH;
    sc.source_sample_rate = 48000;
    sc.channel_count = 1;
    sc.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    sc.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    sc.total_frames = 4;
    sc.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    if (apta_session_create(context, &sc, &session) != APTA_STATUS_OK) return 1;

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.frame_count = 4;
    if (apta_session_push_pcm(session, &block, &accepted) != APTA_STATUS_OK) return 1;
    if (apta_session_signal_end_of_input(session, 4) != APTA_STATUS_OK) return 1;

    apta_work_budget_init(&budget);
    budget.maximum_steps = 16;
    do {
        status = apta_session_process(session, &budget, 0);
    } while (status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);

    apta_session_destroy(session);
    apta_context_destroy(context);
    return status == APTA_STATUS_END_OF_INPUT ? 0 : 1;
}
```

See [`examples/pcm_push`](examples/pcm_push/) for result inspection and
[`examples/pcm_pull`](examples/pcm_pull/) for callback-driven decoding.

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
Complete external consumer projects are under
[`examples/installed_cmake_consumer`](examples/installed_cmake_consumer/) and
[`examples/pkgconfig_consumer`](examples/pkgconfig_consumer/).

To vendor the source tree:

```cmake
set(APTA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(APTA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
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

## Supported integration matrix

| Environment | Status | Notes |
|---|---|---|
| Linux / POSIX | Supported | Core, native file/WAV adapters, tools, packages and conformance evidence |
| Windows / MSVC | Supported | Core, native file adapter, tools, shared/static packages and ABI tests |
| ESP-IDF 5.5.4 / ESP32 | Supported integration | Scalar component and firmware-build evidence |
| ESP-IDF 6.0.2 / ESP32 | Supported integration | Scalar component and firmware-build evidence |
| ESP-IDF 6.0.2 / ESP32-S3 | Supported integration | ESP-DSP-enabled retained build configuration |
| ILP32 | Verified | 32-bit public layout and core test evidence |
| macOS | Community-tested only | No retained native release matrix yet |
| Big-endian native target | Not currently supported | Deterministic byte-swap evidence exists, but no native release target was available |

Hosted ESP-IDF CI proves compile/link integration; it does not claim execution
on physical hardware.

## Version model

APTA intentionally separates several version domains:

| Domain | Current value | Compatibility meaning |
|---|---:|---|
| Specification | 1.0 | Normative APTA behaviour and profiles |
| Public API | 1.0.0 | C source and semantic API contract |
| Package | 1.0.1 | Distributed library and component release |
| Container | 1 | Canonical `.apta` wire format |
| Shared-library SOVERSION | 1 | Binary compatibility family |

A package patch does not automatically change the public API, container or
specification version.

## Examples and tools

- [`examples/README.md`](examples/README.md) — push, pull, installed-package,
  pkg-config and ESP-IDF examples;
- [`tools/README.md`](tools/README.md) — reference command-line tools and build
  options;
- [`ports/espidf/README.md`](ports/espidf/README.md) — ESP-IDF component
  integration and configuration.

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

## Getting help and contributing

- Read [`SUPPORT.md`](SUPPORT.md) before opening a usage or integration request.
- Report reproducible defects through the GitHub issue forms.
- Follow [`SECURITY.md`](SECURITY.md) for private vulnerability reporting.
- Read [`CONTRIBUTING.md`](CONTRIBUTING.md), [`GOVERNANCE.md`](GOVERNANCE.md)
  and [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md) before a substantial change.
- Cite the project using [`CITATION.cff`](CITATION.cff).

## Repository layout

- `specification/` — normative APTA 1.0 documents;
- `include/apta/` — public C API;
- `src/` — portable core and optional desktop implementation;
- `ports/` — platform integrations, including ESP-IDF and Windows;
- `examples/` — complete push, pull and external-consumer examples;
- `tools/` — reference desktop command-line tools;
- `conformance/` — installed-package public conformance suite;
- `interoperability/` — independent producer/consumer evidence;
- `tests/` — unit, integration, compatibility, package, security and fuzz tests;
- `release/` — frozen release verification and publication material.

The project is distributed under the [Apache License 2.0](LICENSE).
