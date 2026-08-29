# APTA 1.1 WP8 readiness audit

- **Status:** in progress; candidate device connected, physical run pending
- **Audited source revision:** `0fe1c22e44e759db3675a289e859b14a085c31e0`
- **Audit date:** 2026-08-29
- **Acceptance claim:** false
- **Tracked physical evidence:** absent

## Environment result

The Windows host has a working ESP-IDF 6.0.2 installation, including the
ESP32-P4 GCC 15.2.0 toolchain and esptool. On 2026-08-29 a candidate Espressif
USB serial/JTAG device enumerated as `USB\VID_303A&PID_1001&MI_00` on `COM4`.
Its chip revision, board model, PSRAM geometry and compatibility with the
required v3.1-v3.99 image have not yet been read, so enumeration alone is not
physical qualification evidence. The previously measured board was ESP32-P4
revision 1.3; ESP-IDF 6.0.2 images cannot be flashed to that older revision.

The first exact local build, at revision
`966798f8771764801524f875d11eae081be95fcf`, exposed the fail-closed
integration findings below. The remediation boundary was frozen before any
source change, then implemented at
`0fe1c22e44e759db3675a289e859b14a085c31e0`.

## Resolved integration findings

The audit found these mismatches with the pre-registered physical-evidence
contract:

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

Revision `0fe1c22e44e759db3675a289e859b14a085c31e0` resolves findings 1
through 4: CI loads both defaults files, the P4 profile fixes overview columns
at 32,768 source frames, and both the cooperative full row and deterministic
capacity probe request all 12 release feature families. Finding 5 is an
intentional diagnostic boundary and remains open until the physical harness and
real USB/audio source are present.

## Remediation verification

The frozen remediation was implemented without changing algorithm thresholds.
Verification on the exact remediation revision produced:

- hardware-evidence validator unit tests: 8/8 pass, including fail-closed
  rejection of the wrong overview profile;
- corrected 12-feature capacity probe: 2,637 overview columns, 3,072 mutable
  beats, two result slots, 9,216 resident beat records, 941,216-byte minimum
  workspace, 1,000,058-byte recommended workspace and 537,104-byte pool;
- no validator threshold increase: all corrected values remain within the
  frozen 1.5 MiB workspace, 2 MiB pool, 4,096-column and 9,216-beat ceilings;
- exact ESP-IDF 6.0.2 build: pass, with `sdkconfig.defaults` and
  `sdkconfig.p4.defaults` both loaded;
- application image: 235,024 bytes, SHA-256
  `1b7a06f4c0c76e9cddd6b7a0ba8d7923917ddf7bc3367a1d479f94e2d0b6113b`;
- image metadata: ESP32-P4, chip revisions v3.1-v3.99, app version
  `v1.0.1-218-g0fe1c22`, ESP-IDF v6.0.2, valid checksum and validation hash;
- exact configuration: PSRAM enabled, 8,192-byte main-task stack, overview
  frames per column 32,768, four detail tiles, 4,096 onset bins and 16,384
  global bins.

The generated image and ignored build directory are reproducible build
artifacts, not tracked acceptance evidence. The checkout was clean after the
build.

No synthetic runner, generated JSON, accelerated PCM feed or eight-second
feature sweep may be reported as physical evidence. Closing WP8 still requires
a real ESP32-P4 v3.1-or-newer board with PSRAM, a real 48 kHz USB/audio workload,
at least 1,800 continuous seconds and every counter/measurement required by
`APTA-1.1-P4-HARDWARE-EVIDENCE.md`.

## Current decision

WP8 remains open but is software-prepared and now has a candidate device on
`COM4`. The next non-destructive hardware step is to identify its chip revision
and PSRAM before any flash. The physical release gate still requires a
compatible v3.1-or-newer board, the exact frozen firmware, its intended real
USB/audio input path and the complete 1,800-second evidence run. WP6 and WP7
remain independently gated by the failed WP5 algorithm-entry decision.
