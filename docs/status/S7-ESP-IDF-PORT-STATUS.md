# Stage S7 — ESP-IDF port status

**Stage status:** Complete self-tested and cross-build-verified implementation candidate  
**Architecture source:** `docs/architecture/APTA-ARCHITECTURE-DRAFT.md`, Stage S7  
**Implementation merge:** `5f949197c2600a9c83a9187ec3625c841ab7f076`  
**Native verification:** GitHub Actions CI run `#291`  
**ESP-IDF verification:** GitHub Actions ESP-IDF workflow run `#13`  
**Registered native runtime tests:** 69  
**Physical-board runtime claim:** not issued

## 1. Stage scope

The architecture roadmap defines Stage S7 as:

- ESP allocator;
- ESP clock;
- optional ESP-DSP backend;
- cooperative scheduler example;
- embedded memory profiles.

All five functional items are present in the repository and have build, host-runtime or bounded-profile evidence. The complete example firmware is cross-compiled and linked for multiple ESP-IDF/target configurations.

Stage completion here does not claim that CI executed the firmware on a physical board.

## 2. Component structure

The port is located at:

```text
ports/espidf/
```

It contains:

- an ESP-IDF component `CMakeLists.txt`;
- component manifest and Kconfig option;
- public `apta_espidf.h` adapter API;
- allocator, clock, logger and optional DSP bindings.

The component builds the same portable C11 sources used by the native `apta::core` target. It does not fork or reduce the waveform, tempo, beatgrid or serialization implementation.

ESP-IDF code does not enter the portable core source directories. The dependency direction remains:

```text
application
    ↓
ESP-IDF adapter
    ↓
portable libapta core
```

## 3. ESP allocator

`apta_espidf_bind_context_config()` installs an aligned allocator using the ESP-IDF capability heap.

The default mapping is:

| APTA memory flag | ESP-IDF capabilities |
|---|---|
| default | `MALLOC_CAP_8BIT` |
| fast | `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT` |
| large | `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT` |
| persistent | `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT` |
| temporary | `MALLOC_CAP_8BIT` |
| DMA | `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT` |

The application may replace every capability mask before binding.

A failed specialized allocation normally retries with `default_caps`. `APTA_ESP_IDF_PORT_FLAG_STRICT_MEMORY_CAPS` disables this fallback. Deallocation uses `heap_caps_free()`.

The native port regression verifies:

- requested-capability selection;
- aligned allocation;
- specialized-to-default fallback;
- strict failure behavior;
- context allocation and cleanup;
- invalid/reserved configuration rejection.

## 4. ESP monotonic clock and logging

The port derives the APTA monotonic nanosecond clock from:

```text
esp_timer_get_time() × 1000
```

Non-positive timer values map to zero and multiplication is overflow guarded.

When `APTA_ESP_IDF_PORT_FLAG_LOG_TO_ESP_LOG` is set, the APTA logger maps public log levels to `esp_log_write()` and prefixes messages with the APTA diagnostic code. The application controls the log tag through `apta_espidf_port_t.log_tag`.

## 5. Optional ESP-DSP boundary

The Kconfig option:

```text
CONFIG_APTA_ESP_DSP_BACKEND
```

selects the implementation of `apta_espidf_dot_product_f32()`:

- disabled — portable scalar loop;
- enabled — Espressif `dsps_dotprod_f32()`.

`apta_espidf_dsp_backend()` reports the selected helper.

This is an optional port optimization boundary. The current reference waveform, S4 and S6 algorithms remain the same portable implementation and do not require ESP-DSP.

## 6. Cooperative scheduler example

The project:

```text
examples/espidf/cooperative_scheduler/
```

builds a complete ESP-IDF application that:

1. binds the ESP-IDF port to an APTA context;
2. generates deterministic 48 kHz mono click-track PCM;
3. pushes 1024-frame blocks;
4. processes with a bounded work budget;
5. yields between calls with `taskYIELD()`;
6. drains to end of input;
7. reads final tempo and local-grid output;
8. reports process-call count, average/max process time and heap before/after cleanup.

The example preserves application ownership of tasking, playback, USB, filesystem and decoder scheduling.

## 7. Embedded memory profiles

Workflow run `#13` verifies three bounded profiles:

| Profile | Workspace | Result-pool allocation | Workspace + pool | Post-create allocator calls |
|---|---:|---:|---:|---:|
| `WAVEFORM_8S` | 131,072 B | 55,664 B | 186,736 B | 0 |
| `PERFORMANCE_LOCAL_6S` | 262,144 B | 46,736 B | 308,880 B | 0 |
| `GLOBAL_DYNAMIC_10_9S` | 1,572,864 B | 399,408 B | 1,972,272 B | 0 |

Each context/session creation performs exactly two allocator callback calls: one context allocation and one complete result-pool allocation. Mutable analysis state uses the caller workspace.

See [`../reference/APTA-ESP-IDF-MEMORY-PROFILES-0.1.md`](../reference/APTA-ESP-IDF-MEMORY-PROFILES-0.1.md).

## 8. ESP-IDF build matrix

Workflow run `#13` successfully cross-compiles and links:

| ESP-IDF | Target | Helper backend | Firmware artifact |
|---|---|---|---|
| 5.5.4 | ESP32 | scalar | verified |
| 6.0.2 | ESP32 | scalar | verified |
| 6.0.2 | ESP32-S3 | ESP-DSP | verified |

The `size-components` archive report records the APTA component archive as:

| Configuration | `libespidf.a` contribution |
|---|---:|
| IDF 5.5.4 / ESP32 / scalar | 36,786 B |
| IDF 6.0.2 / ESP32 / scalar | 36,311 B |
| IDF 6.0.2 / ESP32-S3 / ESP-DSP | 36,342 B |

These are linked archive contributions for the reference example and build configuration. They are not a universal flash-size guarantee.

## 9. Native verification

CI run `#291` completes successfully with:

- GCC and Clang builds;
- C11 and C++11 public-header checks;
- all 69 registered runtime tests;
- the `apta.port.espidf` callback regression;
- AddressSanitizer;
- UndefinedBehaviorSanitizer;
- canonical parser seed generation;
- bounded libFuzzer smoke.

The host test uses narrow ESP-IDF API stubs only to exercise adapter control flow. The cross-build matrix remains authoritative for compatibility with actual ESP-IDF headers and link targets.

## 10. Evidence boundary

Verified:

- full component configuration;
- complete portable core cross-compilation;
- firmware linking for three IDF/target/backend combinations;
- firmware artifact production;
- capability allocator behavior under host regression;
- monotonic clock and logger binding;
- scalar helper execution;
- ESP-DSP helper compilation and linking;
- bounded workload completion without post-create allocator calls.

Not yet verified:

- execution on a physical ESP board;
- actual task stack high-water marks;
- on-target process-call latency distribution;
- on-target heap fragmentation and largest-block behavior;
- watchdog/playback coexistence;
- physical USB/filesystem/decoder integration;
- on-device `.apta` interchange with an independent producer/consumer.

These remaining items prevent a measured ESP resource-class or real-time certification claim, but they do not leave an architecture Stage S7 implementation item absent.

## 11. Conformance position

The project may accurately state:

> `libapta` 0.1.0 contains a self-tested and ESP-IDF-cross-build-verified Stage S7 implementation candidate: capability-aware allocation, monotonic clock and logging bindings, an optional ESP-DSP helper boundary, a cooperative scheduler example and bounded embedded integration profiles.

The project does not yet claim APTA 1.0 stability, certified Core Profile conformance or physical-device runtime certification.

## 12. Next architecture stage

The next roadmap stage is:

```text
Stage S8 — Second independent platform
```

S8 requires implementation and validation on at least one additional platform, such as Zephyr, Windows, STM32 bare metal, a Linux DJ player or a mobile application.
