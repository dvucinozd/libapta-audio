# ESP-IDF cooperative scheduler example

This project builds the complete `libapta` ESP-IDF component and demonstrates application-managed cooperative analysis.

The example:

- creates an ESP-IDF-backed APTA context;
- generates an eight-second, mono, 48 kHz, 125 BPM click track;
- pushes PCM in 1024-frame blocks;
- limits each `apta_session_process()` call with an explicit work budget;
- calls `taskYIELD()` between cooperative processing steps;
- drains the session to `APTA_STATUS_END_OF_INPUT`;
- reads the final tempo and local beatgrid;
- reports process-call count, average/p99-upper-bound/max process time, heap
  cleanup, largest free block and task stack high-water evidence.

## Build with ESP-IDF 5.5.4 or later

From this directory:

```bash
idf.py set-target esp32
idf.py build
```

Flash and monitor on a connected board:

```bash
idf.py -p PORT flash monitor
```

## ESP32-S3 with the optional ESP-DSP helper

Use both default configuration files:

```bash
idf.py -B build-esp32s3 \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.dsp.defaults" \
  set-target esp32s3 build
```

Then flash the selected build directory:

```bash
idf.py -B build-esp32s3 -p PORT flash monitor
```

The ESP-DSP option changes `apta_espidf_dot_product_f32()` only. The current portable APTA analysis pipeline does not depend on the helper.

## Expected output categories

A successful board run logs:

```text
tempo=<millibpm> confidence=<0..100> grid_segments=<count>
process_calls=<count> average_us=<microseconds> p99_us<=<microseconds> max_us=<microseconds>
port_dsp_backend=<0 scalar | 1 ESP-DSP>
free_heap_before=<bytes> free_heap_after=<bytes> delta=<bytes>
minimum_free_heap=<bytes> largest_free_block=<bytes> stack_high_water_words=<words>
```

The p99 value is the upper edge of a 100-us histogram bucket. The FreeRTOS
high-water value is the minimum number of unused `StackType_t` words observed,
not a byte count. Exact timing, heap and stack values depend on the target,
clock, compiler optimization, ESP-IDF version and system configuration. They
are measurements, not stable API output.

## Integration model

The application retains control over:

- task creation and affinity;
- PCM source scheduling;
- USB/filesystem/decoder ownership;
- work budget and yield cadence;
- result publication cadence;
- watchdog and playback priorities.

The library owns analysis state and immutable results only.

## CI boundary

The repository CI cross-compiles this project for:

- ESP-IDF 5.5.4 / ESP32 / scalar helper;
- ESP-IDF 6.0.2 / ESP32 / scalar helper;
- ESP-IDF 6.0.2 / ESP32-S3 / ESP-DSP helper.

CI verifies the linked firmware artifacts but does not execute them on physical hardware. Run the project on the intended board before adopting process-time, stack or heap limits for production.

## ESP32-P4

ESP-IDF 6.0.2 builds P4 firmware that requires chip revision v3.1 or newer. A
v1.x board needs 5.5; the measured revision 1.3 board reports an accepted image
range of v0.1 through v1.99. Flashing a 6.0.2 image to it would fail the image
revision check.

With the APTA 1.0 API and the Phase 7 cooperative refresh state, the measured
global-beatgrid configuration queries 603,504 bytes of workspace. It completes
on the measured P4 both with internal memory only and with PSRAM enabled. The
PSRAM-enabled placement was faster for this working set; neither result is a
universal resource-class claim.

Build with the measured PSRAM configuration:

```bash
idf.py -B build-esp32p4 \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.p4.defaults" \
  set-target esp32p4 build
```

## Per-feature sweep

After the demonstration the example runs the same seven feature sets the host
cost probe measures, printing the queried workspace requirement beside each:

```text
--- per-feature cost, 8 s @ 48000 Hz, 1024-frame blocks ---
overview                 workspace=  68688 calls= 376 average_us=  1916 p99_us<= 3100 ...
+BPM                     workspace= 150704 calls= 385 average_us=  2288 p99_us<= 6300 ...
+global grid             workspace= 603504 calls= 392 average_us=  2670 p99_us<= 6000 ...
```

Sections 29 and 30 of `docs/status/S4-TEMPO-LOCAL-GRID-STATUS.md` read those
numbers against the host table and retain the earlier 807,296-byte result for
comparison. Section 31 records the later incremental-evidence cache measurement.
`docs/status/PHASE7-P4-BOUNDED-REFRESH-STATUS.md` records the cooperative
refresh measurement. The full feature set's current p99 upper bound is 8,200
microseconds and its worst measured process call is 12,355 microseconds,
leaving 42.1% of one 1024-frame block period at 48 kHz.
