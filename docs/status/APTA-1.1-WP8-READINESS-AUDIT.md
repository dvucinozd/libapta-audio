# APTA 1.1 WP8 readiness audit

- **Status:** in progress; v1.3 diagnostic boot passed, qualifying harness open
- **Audited source revision:** `0fe1c22e44e759db3675a289e859b14a085c31e0`
- **Audit date:** 2026-08-29
- **Acceptance claim:** false
- **Tracked qualifying physical evidence:** absent

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

## Revision-correction diagnostic result

The pre-registered correction was implemented at
`18ade2ed13da23585d9ee10826056c83e3ded9a1` and pushed before the physical
run. A clean ESP-IDF 6.0.2 rebuild produced a 233,056-byte application image
with SHA-256
`e660c5ed0ccaac10bf66717a5497cfe47ce775f36633cacd5b980b9694e61278`, app
version `v1.0.1-225-g18ade2e`, valid checksum/validation hash and ESP32-P4
revision range v1.0-v1.99. The bootloader advertises the same range. The
generated configuration confirms the early-revision selector, v1.0 minimum,
PSRAM, the 32,768-frame overview profile, four detail tiles, 4,096 onset bins
and 16,384 global bins.

A normal `idf.py -B build-wp8-idf60-p4-rev1 -p COM4 flash` completed without
`--force`; esptool identified the target as revision v1.3 and verified every
written segment. The physical boot then confirmed:

- ESP-IDF 6.0.2, app revision `18ade2e`, accepted v1.0-v1.99 and actual v1.3;
- 32 MiB hex PSRAM at 20 MHz and a passing startup memory test;
- all seven diagnostic feature-sweep rows completed with zero heap delta;
- the final 12-family row used 580,784 bytes of eight-second workspace across
  392 process calls, with 3,457 us average, p99 at most 6,000 us and 8,832 us
  maximum;
- free heap was unchanged at 34,154,815 bytes, minimum free heap was
  33,592,143 bytes, largest free block was 33,030,144 bytes and task stack
  high-water mark was 5,764 words.

The initial standalone tempo demonstration also reported one 52,220 us maximum
call despite a 1,091 us average and p99 at most 2,600 us. This value is retained
as diagnostic information and is not reinterpreted as a qualifying deadline
result. Startup additionally reported a physical 16 MiB flash behind the 2 MiB
image header and use of the generic driver for the detected BOYA flash. Neither
warning is a gate in the frozen physical-evidence contract, but both remain
visible inputs to the final board profile review.

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
feature sweep may be reported as qualifying physical evidence. Closing WP8
still requires the connected ESP32-P4 v1.3 board with PSRAM, a real 48 kHz
USB/audio workload, at least 1,800 continuous seconds and every
counter/measurement required by `APTA-1.1-P4-HARDWARE-EVIDENCE.md`.

## Current decision

WP8 remains open. The connected COM4 device is now a proven IDF 6.0.2 v1.3
diagnostic target, and revision compatibility is no longer the blocker. The
immediate physical work is an instrumented real USB/audio input harness that
records every frozen counter and resource field for 1,800 seconds, followed by
the exact integrated-candidate rerun if its firmware or memory profile changes.
WP6 and WP7 remain independently gated by the failed WP5 algorithm-entry
decision.
