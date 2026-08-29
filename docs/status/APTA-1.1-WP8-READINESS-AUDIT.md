# APTA 1.1 WP8 readiness audit

- **Status:** in progress; physical run unavailable
- **Audited source revision:** `966798f8771764801524f875d11eae081be95fcf`
- **Audit date:** 2026-08-29
- **Acceptance claim:** false
- **Tracked physical evidence:** absent

## Environment result

The Windows host has a working ESP-IDF 6.0.2 installation, including the
ESP32-P4 GCC 15.2.0 toolchain and esptool. No COM port, Espressif USB device,
USB serial/JTAG bridge or other candidate board is currently enumerated. The
previously measured board was ESP32-P4 revision 1.3; ESP-IDF 6.0.2 images are
restricted to chip revisions v3.1 through v3.99, so that older board cannot run
the required release image even if reconnected.

An exact local build of revision
`966798f8771764801524f875d11eae081be95fcf` with ESP-IDF 6.0.2 and both
`sdkconfig.defaults` and `sdkconfig.p4.defaults` succeeds. The generated
235,040-byte application image has SHA-256
`09d565b80aa3d9a5089472a9a9bbf36c21194ec59a3939ef727eac91c330596b`,
declares ESP32-P4 v3.1-v3.99 and has PSRAM enabled. This is build evidence only;
it was not flashed or executed and cannot close WP8.

## Fail-closed integration findings

The existing build and example are not yet a faithful implementation of the
pre-registered physical-evidence contract:

1. the P4 CI firmware command does not load `sdkconfig.p4.defaults`, so it does
   not prove the required PSRAM configuration;
2. the P4 defaults leave `CONFIG_APTA_OVERVIEW_FRAMES_PER_COLUMN=1024`, while
   the frozen 30-minute profile requires 32,768;
3. the cooperative example's nominal full sweep omits waveform three-band,
   musical key, meter/downbeat and calibrated quality, qualifying only 8 of the
   12 required release feature families;
4. the deterministic capacity probe includes 10 of 12 required feature
   families, omitting waveform detail and calibrated quality;
5. the example processes eight seconds of generated click PCM and has no real
   USB/audio input or coexistence path. It cannot substantiate a 1,800-second
   physical USB/audio claim.

## Frozen remediation boundary

Before another build or device attempt:

- make the P4 CI build load both defaults files;
- bind the P4 defaults explicitly to the 32,768-frame overview profile;
- make the example's full row request all 12 release feature families;
- make the 30-minute capacity probe request the same 12 families;
- recalculate exact workspace/result-pool minimums after that correction and
  raise validator minima if required, while retaining the existing 1.5 MiB
  workspace, 2 MiB pool, 4,096-column and 9,216-beat ceilings;
- rebuild the corrected exact revision under ESP-IDF 6.0.2 and record the image
  identity and revision bounds.

No synthetic runner, generated JSON, accelerated PCM feed or eight-second
feature sweep may be reported as physical evidence. Closing WP8 still requires
a real ESP32-P4 v3.1-or-newer board with PSRAM, a real 48 kHz USB/audio workload,
at least 1,800 continuous seconds and every counter/measurement required by
`APTA-1.1-P4-HARDWARE-EVIDENCE.md`.

## Current decision

WP8 remains open. Software preparation can continue without a board, but the
physical release gate cannot pass until compatible hardware and its intended
USB/audio input path are available. WP6 and WP7 remain independently gated by
the failed WP5 algorithm-entry decision.
