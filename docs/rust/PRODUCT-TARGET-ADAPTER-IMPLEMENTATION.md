# RC-04A-3 adapter and link-check package preparation

Date:2026-10-27. Registration7d5151bc6d85f5431c9589457df707acdcab4ff4.
[Contract](PRODUCT-TARGET-ADAPTER-PROTOCOL.md),
[implementation receipt](PRODUCT-TARGET-ADAPTER-IMPLEMENTATION.json).

## Implemented boundary

The independent no_std target-adapter crate owns a fresh resource-core job and
borrows a clock. It schedules1..32 core units without crossing a registered
phase boundary. Phase tick totals include core scheduling/cancel/validation
overhead;they are not raw memory bandwidth or isolated MAC throughput.
Total elapsed includes gaps between calls. A late or backwards clock fences
the result,including when the underlying core has just numerically completed.
Quota0 and oversized running quotas do no work;terminal states stay stable.
Pending cancellation stops the job without reading another clock or resuming.
This is not an asynchronous physical cancel-latency measurement.

The HAL clock uses pinned esp-hal1.2.2 Instant microseconds after HAL init,
nominal1MHz. Its source warns about pre-init reliability,sleep and wrap;the
adapter fails on backwards timestamps and does not invent rollover compensation.
Clock calibration,real CPU/PSRAM settings and timing qualification remain open.
Existing libm/resource-core math is reused without source changes.

The bounded no_std producer emits observation/end records accepted by telemetry
V1. Tokens,digests,minimum chip,counts and intervals are checked before successful
length publication. Unknown allocator/stack/USB/audio metrics remain null. Buffer
failure exposes no certified length;its bytes may contain an uncertified prefix.
No device-memory values or numerical-output digest are fabricated. Canonical
Python ingestion remains the authority for capture order/completeness/unknowns.

## Five real target ELFs, guarded idle boot

The opt-in link-check binary initializes HAL and idles. A retained,uncalled entry
forces the selected resource job,adapter,timer and backend to link. Separate
features reserve copy/projection32/projection64/activation/log caller storage,
so mutually exclusive buffers are never summed into one supposed product peak.
No selected variant or multiple variants fail compilation with the explicit
selection guard. No Guition product guard or consumer checkout was changed.

The retained entry requires device-log kind,observed chip matching efuse,and the
embedded image/source/protocol identity before touching its static buffers.
It is single-use compile/link scaffolding,not a complete physical trial runner.
There is no external arming/serial command path and no automatic probe execution.
StaticCell initialization/stack behavior,buffer reuse across warm-up/trials,
numerical digest/oracle checking and telemetry transport require later runtime
integration and qualification. Link-check ELF files are not physical measurement
firmware or an execution-ready package. They supply no programming layout.

The package tool builds all five variants with locked offline Cargo,policy302,
frame pointers and linkall.x. It binds source components,Cargo metadata/lock,
actual compiler/tool paths and registry source-file hashes,rechecking after each
build. A stable external map path permits dependency-cache reuse;each map/ELF
is copied into its own immutable variant folder immediately. ELF identity,
RISC-V32 sections,retained entry and minimum static BSS are checked. readelf
headers/symbols and llvm-objdump disassembly are retained for actual code review.

Clean source is required by default;development builds are explicitly tagged.
Every build has180s wall/2GiB per-process RSS and64MiB retained-package caps;
inspection commands have60s timeout. These are host operational bounds,not P4
allowances. No installation,flash,serial or live-capture action exists. Output
and build directories must be new,external and disjoint;failures are preserved.

Package verification rejects modified/missing/extra/symlink artifacts,foreign
or malformed ELF,target/feature/static-buffer mismatches,unsafe readiness and
missing unit/revision placeholders. Capture templates bind image/source/protocol
but retain unit/revision/arming/schedule as null. They are intentionally not
ingest manifests until those fields and the runtime protocol are resolved.

## Software checks and evidence scope

Seven adapter lifecycle/formatter tests pass debug/release:full copy/projection/
nonlinear runs with fake clocks;phase budgets;fresh jobs;clock/budget refusal;
pending cancel;late-ready result fencing;capacity/token/interval formatting.
Fake ticks are explicitly synthetic and never presented as elapsed device time.

Six package tests pass in WSL,including overwrite and symlink refusal. On Windows
the platform-specific test is NOT_RUN;the other five pass. Existing19 telemetry
controls pass. The real Rust producer's synthetic fixture passes the canonical
Python parser with one observation,19 null metrics and NOT_QUALIFIED.
No target producer/collector is claimed physically exercised by that test.

Host fmt/Clippy and target release Clippy pass. Both missing/multiple feature
negative builds include the intended selection-guard diagnostic. Cached dependencies
resolve offline. Rust/Cargo1.95.0 is the actual toolchain. Default LIBAPTA core,
runtime,HS01–HS06 sources/oracles and VERSION1.0.1 remain unchanged by hashes.

Early development evidence is retained separately:
`D:/AI/LIBAPTA/outputs/rc04a3-package-development-20261027-01/`,
`rc04a3-link-preflight-20261027-01/`,`rc04a3-checks-20261027-01/` and
`rc04a3-host-interop-20261027-01/`. The initial copy preflight exposed a function-
address cast warning,which was fixed before freeze. Fresh-job enforcement and
embedded identity guards were tightened before final checks. Development package
checks preceded those refinements;the final clean package uses the frozen final
source. No device/measured-candidate retry or numerical/cap/backend change occurred.

## Frozen build handoff

Freeze/push implementation first,then invoke from WSL with NEW external paths:

```text
python3 tools/apta_target_probe_package.py build --output NEW_EXTERNAL_PACKAGE --target-dir NEW_EXTERNAL_BUILD --cargo /home/daniel/.cargo/bin/cargo --rustc /home/daniel/.cargo/bin/rustc
python3 tools/apta_target_probe_package.py verify --package NEW_EXTERNAL_PACKAGE
```

Final clean-package result is published separately in
`docs/rust/PRODUCT-TARGET-ADAPTER-PACKAGE.md` after build (planned result path).
At the implementation freeze that result is NOT_RUN;development evidence does
not become a clean build by relabeling its source SHA.

RC-04A-3 completion means adapter/link/package preparation only. Physical RC-04B
still requires exact-unit access/factory backup/recovery/programming layout,
qualified memory/board/product owners,operator arming and transport,reusable
trial scheduler,numerical-output verification/digests,live allocator/stack/error
metrics and an asynchronous requester. All nine PF01 allowances,5s/60s/50ms,
model rights/provenance and product GO remain open. A BUILD_ONLY_COMPLETE result
keeps execution_ready=false and qualification=NOT_QUALIFIED.
