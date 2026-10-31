# RC-04B arrival and silicon compatibility checkpoint

Date: **2026-10-31**. LIBAPTA input: `1f9a7558df626acfe4f77a09ad3a9a6843b82da0`.
Verdict: **BOARD_AVAILABLE; NATIVE_PACKAGE_INCOMPATIBLE; LEGACY_REGISTRATION_NEXT**.
[Machine receipt](PRODUCT-BOARD-ARRIVAL-20261031.json),
[live status](PRODUCT-FIRST-STATUS.json), [current handoff](BOARD-ARRIVAL-HANDOFF.md).

## New observation and inherited identity

The owner confirms arrival of the JC1060P470C_I_W_Y on **COM3**. Windows currently
enumerates USB VID_303A/PID_1001/MI_00. An eight-second passive serial observation
retained 9,424 bytes of running Rust SD-owner/heartbeat output without a serial
error. No command bytes, explicit reset, ROM entry, RAM load, flash or eFuse
write occurred in this checkpoint. The log does not attest the current firmware
hash, chip revision or base MAC; those identities were not freshly read today.

Current consumer source is `dvucinozd/Ruzina` at
`926e90506c4698cc68ada045347d21ded84f649a`. Its committed exact-unit record and
independent programmer/eFuse evidence from **2026-10-30** identify unit
`JC1060-20261030-COM3`, base MAC `e8:f6:0a:e7:22:1d`, ESP32-P4 **v1.3 / 103**,
16 MiB flash and factory-reported 32 MiB PSRAM. This is inherited unit evidence,
not a fresh ROM handshake or LIBAPTA resource qualification. Panel/PCB markings
and complete factory restoration remain unqualified in that record.

Four existing full-flash backup files were rehashed today: two independent
16,777,216-byte reads plus both secondary copies. All match SHA-256
`27d5782978aeb3c42f8b49659e793d3ca8f59543f62e419d7b1981de2c492060`.
Manual cold-ROM entry/recovery and scalar-sector layout have prior source-bound
evidence. They are not a new programming plan or proof of complete restoration.

Raw observation, backup recheck and error/status record are retained outside Git:

```text
D:/AI/LIBAPTA/outputs/rc04b-arrival-20261031-com3-01/passive.log
D:/AI/LIBAPTA/outputs/rc04b-arrival-20261031-com3-01/observation.json
```

## Compatibility consequence

The frozen five-image RC-04A-3 package selects policy **302**, and the pinned
esp-hal 1.2.2 source explicitly excludes ESP32-P4 revisions below v3.0. The
received unit's recorded **103** cannot use that backend. Do not flash the
prepared native images, lower their revision policy, patch image headers or
substitute 302 for the observed identity. The frozen package remains valid
build/link evidence for its original target, `execution_ready=false`.

Telemetry V1 also requires chip revision >=302. Passing a fabricated revision
would invalidate identity. A legacy backend therefore needs a separately
registered/versioned telemetry identity contract; do not silently relax V1.

The consumer already contains a separate legacy ESP-IDF 6.1 shell and a narrow
C ABI to shared no_std Rust logic. Its S4/S5/S6 device records establish selected
scalar, memory-task and SD/display behavior only. Their memory counters,
timings and operator observations do not close LIBAPTA PF-01/TP-00–TP-06 gates.
This checkpoint leaves consumer code, its running firmware, provider and pins
unchanged. No trained model or musical corpus was run.

## Next bounded work

Register the legacy resource-probe boundary before implementation: reuse the
existing IDF legacy startup/memory map/FreeRTOS/driver ownership and link only
the fixed HAL-independent LIBAPTA probe core through a reviewed narrow C ABI.
Keep esp-hal startup out of that executable. Freeze source/dependencies,
legacy revision domain, clocks, numerical references, placement, arming,
transport, reusable warm-up/trial schedule and result verification. Define a
separate telemetry version and its refusals for revision 103.

Registration/software readiness does not authorize flashing a build-only image.
An actual physical run still needs its own reviewed exact image/layout/recovery
plan, a fresh unit match, source freeze/push and all runtime prerequisites.
All nine PF-01 allowances, product latency/cancel targets, rights/provenance,
musical validation and product GO remain open. VERSION stays 1.0.1.
