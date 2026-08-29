# APTA 1.1 WP8 readiness audit

- **Status:** in progress; v1.3 compatibility correction pre-registered
- **Audited source revision:** `0fe1c22e44e759db3675a289e859b14a085c31e0`
- **Audit date:** 2026-08-29
- **Acceptance claim:** false
- **Tracked physical evidence:** absent

## Environment result

The Windows host has a working ESP-IDF 6.0.2 installation, including the
ESP32-P4 GCC 15.2.0 toolchain and esptool. On 2026-08-29 an Espressif USB
serial/JTAG device enumerated as `USB\VID_303A&PID_1001&MI_00` on `COM4`.
The ESP-IDF 6.0.2 environment's esptool identified it without flashing as an
ESP32-P4 revision v1.3, dual-core plus LP core, 400 MHz, 40 MHz crystal and USB
Serial/JTAG mode. Device-unique identifiers are intentionally not tracked.
The earlier 6.0.2 build accepts only v3.1-v3.99 because the project left the
new IDF 6 P4 revision selector at its default. Local ESP-IDF 6.0.2 Kconfig and
target sources confirm supported, mutually exclusive build paths for revisions
below v3.0 and revisions v3.0 or newer. The connected v1.3 board can therefore
run a normally validated IDF 6.0.2 image once the project selects the early-P4
path; no forced flash is required.

## Pre-registered revision correction

Before changing project configuration or flashing, freeze this correction:

- add `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` to `sdkconfig.p4.defaults`;
- select `CONFIG_ESP32P4_REV_MIN_100=y`, the narrowest available Kconfig
  minimum not newer than the connected v1.3 chip;
- let Kconfig derive `CONFIG_ESP32P4_REV_MIN_FULL=100` and both maximum values
  as 199; do not copy hidden derived symbols into the defaults file;
- build from a fresh dedicated directory with exact ESP-IDF 6.0.2 and both
  project defaults files;
- require `esptool image-info` and the generated `sdkconfig` to agree on
  ESP32-P4 revisions v1.0-v1.99 before a normal, non-forced flash to COM4;
- stop without flashing if the clean build fails, metadata remains on the 3.x
  path, normal esptool compatibility rejects the image or COM4 no longer
  identifies as the already-read ESP32-P4 v1.3 device.

The first boot is diagnostic only. It may establish that the exact project
build starts and that its bounded feature sweep runs on real P4 silicon. It
cannot close WP8 because the example still uses eight seconds of generated
click PCM instead of the required real USB/audio path and 1,800-second run.

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

WP8 remains open but software-prepared. The connected COM4 device is a usable
v1.3 hardware target after the pre-registered IDF 6 early-revision correction.
The immediate physical step is the clean metadata-verified build, normal flash
and diagnostic boot. The release gate still requires the exact frozen
firmware, its intended real USB/audio input path and the complete 1,800-second
evidence run. WP6 and WP7 remain independently gated by the failed WP5
algorithm-entry decision.
