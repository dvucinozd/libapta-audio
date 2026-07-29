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
- reports process-call count, average/max process time and free heap before/after cleanup.

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
process_calls=<count> average_us=<microseconds> max_us=<microseconds>
port_dsp_backend=<0 scalar | 1 ESP-DSP>
free_heap_before=<bytes> free_heap_after=<bytes> delta=<bytes>
```

Exact timing and heap values depend on the target, clock, compiler optimization, ESP-IDF version and system configuration. They are measurements, not stable API output.

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
