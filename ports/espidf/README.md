# libapta-audio ESP-IDF component

The `ports/espidf` directory exposes the portable `libapta` core as an ESP-IDF component. The component builds the same ISO C11 core, waveform, tempo, beatgrid and serialization sources used by the native CMake target; ESP-IDF-specific code is confined to the adapter in this directory.

## Supported build range

The reference CI verifies:

- ESP-IDF 5.5.4 on ESP32 with the scalar helper path;
- ESP-IDF 6.0.2 on ESP32 with the scalar helper path;
- ESP-IDF 6.0.2 on ESP32-S3 with the optional ESP-DSP helper enabled.

The component manifest declares ESP-IDF `>=5.5`. A later ESP-IDF version is not assumed compatible until it is added to CI.

## Adding the component to a project

When `libapta-audio` is available beside an application, add its component directory before including the ESP-IDF project CMake file:

```cmake
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "/absolute/or/project-relative/path/to/libapta-audio/ports/espidf")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_apta_application)
```

A consuming component can then declare:

```cmake
idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES espidf)
```

The public adapter header is:

```c
#include <apta/apta.h>
#include <apta/apta_espidf.h>
```

## Context binding

```c
apta_espidf_port_t port;
apta_context_config_t config;
apta_context_t *context = NULL;

apta_espidf_port_init(&port);
apta_context_config_init(&config);
config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;

if (apta_espidf_bind_context_config(&port, &config) != APTA_STATUS_OK) {
    /* invalid port/config */
}
if (apta_context_create(&config, &context) != APTA_STATUS_OK) {
    /* allocation or configuration failure */
}
```

`apta_espidf_bind_context_config()` installs:

- a capability-aware aligned allocator;
- `heap_caps_free()` deallocation;
- a monotonic nanosecond clock derived from `esp_timer_get_time()`;
- an optional logger backed by `esp_log_write()`.

The `apta_espidf_port_t` object must remain alive for as long as the context may invoke its callbacks.

## Memory capability mapping

The default mapping is:

| APTA memory class | ESP-IDF capabilities |
|---|---|
| default | `MALLOC_CAP_8BIT` |
| fast | `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT` |
| large | `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT` |
| persistent | `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT` |
| temporary | `MALLOC_CAP_8BIT` |
| DMA | `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT` |

All masks are writable before binding. When a requested non-default capability allocation fails, the adapter normally retries with `default_caps`. Set:

```c
port.flags |= APTA_ESP_IDF_PORT_FLAG_STRICT_MEMORY_CAPS;
```

to disable fallback.

`APTA_ESP_IDF_PORT_FLAG_PREFER_SPIRAM_LARGE` documents the default policy that `APTA_MEMORY_LARGE` maps to `large_caps`; the application remains responsible for enabling and configuring PSRAM on targets that provide it.

## Logging

Logging is enabled by default through:

```c
APTA_ESP_IDF_PORT_FLAG_LOG_TO_ESP_LOG
```

Set `port.log_tag` before binding to change the ESP log tag. Clear the flag to leave the APTA logger callback unset.

## Optional ESP-DSP helper

The component provides:

```c
apta_espidf_dot_product_f32();
apta_espidf_dsp_backend();
```

By default, the helper uses a portable scalar implementation. Enable:

```text
CONFIG_APTA_ESP_DSP_BACKEND=y
```

to route the helper through `dsps_dotprod_f32()` from the Espressif ESP-DSP component.

This helper is an optional port optimization boundary. The current portable APTA analysis algorithms remain unchanged and do not require ESP-DSP.

## Cooperative execution

The core does not create tasks. An ESP-IDF application should:

1. supply or pull a bounded PCM block;
2. call `apta_session_process()` with an explicit work budget;
3. yield or return to its scheduler;
4. repeat until the desired result is available or the source completes.

See [`../../examples/espidf/cooperative_scheduler`](../../examples/espidf/cooperative_scheduler).

## Evidence boundary

CI cross-compiles and links complete firmware images for the listed ESP-IDF/target combinations and runs host-side allocator, lifetime, bounded-profile, sanitizer and fuzz tests. CI does not flash or execute the example on a physical ESP board. Manual ESP32-P4 timing and memory measurements are recorded in the cooperative-example README and the S4 status document, but hosts must still measure their intended target, stack, heap and workload before making a device resource-class or real-time claim.

## Tunable capacities

The library's geometry and capacity constants are `#ifndef`-guarded, and the
four a host is most likely to need are exposed through this component's
`Kconfig`:

| Option | Default | Effect |
|---|---:|---|
| `CONFIG_APTA_OVERVIEW_FRAMES_PER_COLUMN` | 1024 | Overview horizontal resolution. 1024 is about 43 columns per second at 44.1 kHz. |
| `CONFIG_APTA_MAX_DETAIL_TILES` | 4 | Detail tile cache. About 1.5 s of total coverage at the default detail geometry. |
| `CONFIG_APTA_ONSET_BIN_CAPACITY` | 4096 | Local tempo analysis window, 23.8 s at 44.1 kHz. |
| `CONFIG_APTA_GLOBAL_BIN_CAPACITY` | 16384 | Global grid window, 12.7 minutes at 44.1 kHz. |

`CMakeLists.txt` passes each one through only when it is set, so an
unconfigured build keeps the library defaults.

Anything overridden changes the static workspace a session needs. Do not scale
a published profile constant by hand -- call
`apta_query_workspace_requirements()` with the configuration and use what it
reports. `apta_session_create()` enforces the same figure.

### Stack cost of the global grid

`apta_internal_s6_refresh()` declares a per-window array on the stack, sized
`APTA_INTERNAL_GLOBAL_MAX_WINDOWS`, which is
`GLOBAL_BIN_CAPACITY / GLOBAL_WINDOW_BINS + 1`. At the defaults that is 129
entries of `apta_s6_window_t`, roughly 4 KiB, and it lives for the duration of
a `apta_session_process()` call. Raising `CONFIG_APTA_GLOBAL_BIN_CAPACITY`
raises this proportionally, so a task calling `apta_session_process()` needs
its stack sized accordingly. A static assertion caps the array at 1025 entries
to stop an override from silently overflowing a typical task stack; raising
that cap is a deliberate edit to `src/core/apta_internal.h`.

### Coherence is checked at compile time

Overrides that contradict each other fail the build rather than misbehaving at
runtime. For example an onset ring smaller than the minimum tempo window:

```text
error: static assertion failed: "onset bin capacity must hold the minimum tempo window"
```
