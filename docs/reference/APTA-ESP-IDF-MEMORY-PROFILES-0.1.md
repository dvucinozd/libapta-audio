# APTA ESP-IDF bounded memory profiles 0.1

**Status:** Reference integration profiles  
**Implementation merge:** `5f949197c2600a9c83a9187ec3625c841ab7f076`  
**Profile verification:** ESP-IDF workflow run `#13`  
**Resource-class certification:** not issued

## 1. Purpose

These profiles provide repeatable bounded configurations for ESP-IDF integration work. They are not universal minimum-memory claims and they are not a replacement for measurements on the final target.

Each profile uses:

- known source duration;
- caller-owned static session workspace;
- `APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS`;
- the two-slot immutable result pool;
- push-mode PCM;
- no context allocator calls after successful session creation.

The profile test verifies final feature publication, result lifetime and complete cleanup.

## 2. Verified profiles

| Profile | Features | Source | Queried workspace | Profile workspace | Queried result-pool allocation | Workspace + pool | Alignment | Allocator calls |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| `WAVEFORM_8S` | overview waveform | 8.0 s / 384,000 frames | 68,832 B | 131,072 B | 55,664 B | 186,736 B | 16 B | 2 |
| `PERFORMANCE_LOCAL_6S` | overview, BPM, local grid, confidence | 6.0 s / 288,000 frames | 183,648 B | 262,144 B | 46,736 B | 308,880 B | 16 B | 2 |
| `GLOBAL_DYNAMIC_10_9S` | overview, BPM, global grid, dynamic tempo, confidence | 10.92 s / 524,288 frames | 807,664 B | 1,572,864 B | 399,408 B | 1,972,272 B | 16 B | 2 |

The `Queried workspace` column is `minimum_bytes` from
`apta_query_workspace_requirements()` for each profile's configuration. It
replaces the hand-picked constant as the figure a host should plan against;
the `Profile workspace` column is retained because it is what the profile test
actually supplies, and it shows the headroom each published profile carries.
Every profile allocates more than the query asks for, so none of them was
passing by luck.

The two allocator calls are:

1. the APTA context object;
2. the complete immutable result pool.

PCM queue nodes, accepted ranges, waveform accumulators, tempo/grid mutable state and session metadata use the caller workspace in these profiles. Immutable result generations use the preallocated pool.

## 3. What the numbers include

The queried-workspace column is the `minimum_bytes` value returned by `apta_query_workspace_requirements()`. A buffer at least that large, and aligned to `required_alignment`, completes the analysis the configuration describes; `apta_session_create()` rejects anything smaller with `APTA_ERROR_OUT_OF_MEMORY`. Hosts should call the query rather than copying a constant from this table, because the figure scales with `total_frames`.

The profile-workspace column is the exact byte budget supplied to `apta_session_config_t.static_workspace` by the profile test.

The result-pool column is the `minimum_bytes` value returned by `apta_query_memory_requirements()` for the bounded-result-slot configuration and used as the context allocation limit in the test.

### 3.1 How the workspace figure scales

The workspace is not dominated by fixed capacities. The overview accumulator array holds one entry per column across the whole track and grows by doubling, and for anything longer than a few seconds it is the largest single contributor. Queried `minimum_bytes` for full features at 44.1 kHz:

| Duration | Queried workspace |
|---|---:|
| 30 s | 914,352 B |
| 5 min | 1,832,048 B |
| 12 min | 2,880,688 B |

The growth is sublinear only because the doubling sequence steps in powers of two; within a step the figure is flat, and across steps it roughly tracks duration.

The figure also accounts for the doubling transient. A growable array is replaced before the old one is released, and the freed fragments can never serve the next request, which is strictly larger than everything released so far. The whole doubling sequence is therefore charged, not just the final capacity. Verified by bisection: for a 5-minute full-feature configuration the reported 1,832,048 B completes and the true boundary lies within about 800 bytes of it.

The `Workspace + pool` column is useful for integration planning, but it is not total application RAM.

## 4. What the numbers exclude

The table does not include:

- the separate context-object allocation;
- application PCM/decode buffers;
- USB, filesystem or codec state;
- FreeRTOS task stack;
- ESP-IDF component and system heap overhead;
- logging buffers;
- temporary linker/runtime overhead;
- PSRAM allocator fragmentation;
- other application tasks and peripherals.

It also does not establish a worst-case process-call latency.

## 5. Profile selection

### `WAVEFORM_8S`

Use for a small bounded waveform-only integration or initial component smoke test. It does not include detail tiles, tempo or beatgrid output.

### `PERFORMANCE_LOCAL_6S`

Use for latency-oriented local analysis near playback focus. It exercises BPM, confidence and one local beatgrid segment while preserving the bounded two-slot result model.

### `GLOBAL_DYNAMIC_10_9S`

Use to validate the Stage S6 global/dynamic data path on an ESP integration. The large workspace is intentional because the reference S6 engine carries a bounded long-range onset timeline and explicit-beat staging capacity.

This profile is not presented as suitable for every ESP32-class device. Targets without sufficient internal RAM or PSRAM should disable global/dynamic features or use a smaller future resource-class profile.

## 6. Reproducing the profiles

The source is:

```text
tests/unit/espidf_memory_profiles.c
```

The ESP-IDF workflow builds the production native library without tests or desktop adapters, compiles the profile executable against that library and runs all three configurations.

Equivalent commands are:

```bash
cmake -S . -B build-profiles \
  -DAPTA_BUILD_TESTS=OFF \
  -DAPTA_BUILD_DESKTOP_ADAPTERS=OFF \
  -DAPTA_BUILD_TOOLS=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-profiles --parallel

cc -std=c11 -Iinclude \
  tests/unit/espidf_memory_profiles.c \
  build-profiles/libapta.a -lm \
  -o build-profiles/apta-espidf-memory-profiles

./build-profiles/apta-espidf-memory-profiles
```

## 7. On-target validation

Before production use, run the cooperative example on the intended board and record at least:

- ESP-IDF version and target;
- compiler optimization;
- enabled APTA features;
- source duration and PCM block size;
- caller workspace and queried pool bytes;
- free heap before context creation and after cleanup;
- largest free block and capability-specific free heap;
- task stack high-water mark;
- average and maximum `apta_session_process()` time;
- watchdog and playback interaction.

A future named resource class such as `APTA-R0-STATIC-128K` requires this target-specific evidence and is not claimed by this document.
