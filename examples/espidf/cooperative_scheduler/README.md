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

ESP-IDF 6.0.2 has mutually exclusive P4 build paths for revisions below v3.0
and revisions v3.0 or newer. The P4 defaults select the early-revision path and
a v1.0 minimum for the measured revision v1.3 board, producing an accepted
image range of v1.0 through v1.99. A separate defaults profile is required for
v3.x hardware; one binary cannot support both hardware families.

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

The P4 defaults bind the frozen 30-minute release profile to 32,768 overview
frames per column. The final feature-sweep row requests all 12 release-target
families: overview/detail/three-band waveform, BPM, local/global beatgrid,
dynamic tempo, confidence, grid locking, meter/downbeat, musical key and
calibrated quality.

## Per-feature sweep

After the demonstration the example runs the same seven cumulative feature
sets the host cost probe measures, printing the queried workspace requirement
beside each. The final row is the complete 12-family release mask:

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

This eight-second generated-click sweep remains a diagnostic example, not the
APTA 1.1 physical evidence run. It does not provide a real USB/audio source,
1,800 seconds of continuous input, failure/drop counters or the complete
evidence JSON required by `docs/status/APTA-1.1-P4-HARDWARE-EVIDENCE.md`.

## APTA 1.1 P4 USB/audio evidence harness

The opt-in WP8 profile replaces the generated-click example with a fail-closed
USB Audio Class device. The P4 exposes one 48 kHz mono PCM16 speaker endpoint;
audio sent by the USB host is the analyzer input. There is no generated,
silence or file fallback.

Build the harness from a clean, committed checkout with exact ESP-IDF 6.0.2:

```bash
idf.py -B build-wp8-uac \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.p4.defaults;sdkconfig.wp8.defaults" \
  set-target esp32p4 build
```

The tracked `dependencies.lock` pins `espressif/usb_device_uac` 1.3.1,
`espressif/tinyusb` 0.19.0~3 and their resolved component hashes. Verify the
image reports ESP32-P4 revisions v1.0-v1.99 before flashing normally:

```bash
python -m esptool --chip esp32p4 image-info \
  build-wp8-uac/apta_cooperative_scheduler.bin
idf.py -B build-wp8-uac -p PORT flash monitor
```

`PORT` is the USB Serial/JTAG control connection. Audio must use the board's
separate USB-OTG connector, which must enumerate on the host as
`APTA 1.1 P4 Evidence Input`. Merely seeing the serial port or the firmware
`ready` message is not USB/audio evidence.

The 30-minute clock starts on the first accepted UAC frame. A paused stream,
disconnect, malformed block, bounded-queue overflow, partial APTA push,
allocation failure or process call above 21,334 microseconds permanently fails
the run. A complete run consumes exactly 86,400,000 source frames and emits one
machine-readable `APTA_P4_EVIDENCE` record with stream accounting, all failure
counters, process timing and before/minimum/after heap measurements. Preserve
the complete serial log; the final tracked evidence JSON also binds the exact
committed source revision, firmware SHA-256 and physical board metadata.
