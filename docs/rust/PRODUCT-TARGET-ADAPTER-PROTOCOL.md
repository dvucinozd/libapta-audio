# RC-04A-3 target adapter and build-only package contract

Date:2026-10-27. Baseline1dbba07dc8eb9d5b81b19aecfb47bf167f86c219.
Status:REGISTERED_NOT_IMPLEMENTED. [Machine contract](PRODUCT-TARGET-ADAPTER-PROTOCOL.json).
This is the software-preparation slice of RC-04A-3;physical RC-04B remains gated.

## Exact boundary

New standalone `rust/probes/target-adapter` crate with Cargo.toml/Cargo.lock,
`.cargo/config.toml`,src/lib.rs,src/bin/linkcheck.rs,tests/adapter.rs and
examples/telemetry_fixture.rs. Default no_std library depends only on the already
registered sibling resource-core. Optional esp32p4 feature pins cached esp-hal1.2.2
and static_cell2.1.1;no new downloads,model,consumer pin/provider or BSP guard edit.
Host tooling:`tools/apta_target_probe_package.py` and its test module in tools/tests.

## Cooperative adapter

Own a fresh registered probe job,borrow a clock and caller storage through that
job. Clock supplies monotonic u64 ticks and nominal Hz1000..1e9. Constructor
requires Running and a positive limit<=10*Hz. HAL implementation uses pinned
esp_hal::time::Instant::now().duration_since_epoch().as_micros() only after HAL
init,nominal1MHz. Source timer documentation describes wrapping and no sleep
measurement;backwards ticks fail rather than inventing wrap compensation.
Nominal source precision is not physically calibrated or qualified.

step quota0 is inert;stable terminal precedes later flags. Valid running quota
is1..32. An oversized quota refuses before clock/work. Limit each call to the
remaining core phase so clock attribution never crosses copy setup/copy/verify
or projection dot/commit boundaries. Fixed phase sizes derive from registered
core units/progress,not hardware measurements. Time entire call including core
scheduler/cancel overhead;record phase tick totals,not raw MAC/bus speed.
Check clock/deadline before and after work. Backwards clock,elapsed>limit or core
failure terminally fence the adapter,even if core numerics just became Ready.
Expose the owned job/result only when adapter completion also passes timing.
Drop may return uncertified caller buffers;no reset/resume/hidden publication.
No latency benchmark,async requester or device memory claim in host fake-clock tests.

## Producer and interoperability

Safe no_std bounded formatter writes telemetry V1 observation/end frames into
caller byte slices. Validate ASCII tokens,hex digests,chip minimum,sequence and
clock/count/domains before output;overflow exposes no certified length. Unknown
memory/stack/counter metrics serialize as null. No boot chatter or auto-filled
zero/free-memory values. Actual numerical-output digest/verification remains a
separate physical-run obligation;no fabricated hash or numeric PASS.
Host synthetic fixture uses this real producer and a fake clock,then passes its
manifest/log through the existing canonical Python parser. Reject malformed
identity/undersized output,clock reversal,late completion,zero/oversized quota,
cancel and post-terminal mutation. Check no result escapes failed timing fences.

## Five isolated link-check images

Features select exactly one of copy,projection32,projection64,activation,logs,
together with esp32p4. They compile explicit static internal-buffer shapes:
131072,109200,218400,114688,229376B respectively,plus controls/stack/firmware.
No PSRAM allocator,DMA,USB,UI/audio owners or invented physical free-memory values.
Link/map checks distinguish static reserved buffers from free analysis allowances.

The HAL link-check binary initializes the HAL and idles. It retains an uncalled
probe/adapter entry through a black-box function address so the linker must resolve
the selected kernel,math backend and timer against a real target ELF. No automatic
probe execution,serial port,programming layout or arming command is supplied.
An external caller identity is required by that retained entry;no placeholder
device measurement is emitted. These images are BUILD_ONLY_LINKCHECK,not runtime
measurement firmware or a physical execution-ready package. Future operator
arming/producer transport/verification integration and qualified BSP ownership
remain explicit RC-04B prerequisites;do not infer their implementation from linking.

## Build/package tool

Linux/WSL only,explicit new external output/build directories;offline locked Cargo,
pinned compiler1.95.0. Build every variant once,immediately retain ELF,map,compiler
logs,readelf headers/sections/symbols and llvm-objdump disassembly. Set minimum
chip policy302 and `-Tlinkall.x`/frame-pointer/map flags from inspected consumer
configuration. Package identity binds full source SHA,exact source/lockfiles,
environment/tool paths/digests,variant/features and copied ELF hashes. Source
must remain clean and unchanged across the final build;development builds require
an explicit flag and remain labelled separately. No dependency installation.

Keep source symbols for retained entry;verify RISC-V32 ELF and static buffer
reservations/map presence. Retain disassembly for actual instruction review;
mere symbol/file existence is not numerical target execution or WCET proof.
Each build/tool command has a bounded timeout and captured logs. Preserve failures
and refuse overwrite,source drift,missing variant/hash or a package claiming
execution-ready/qualified. No retry/backend/cap rescue inside a build attempt.
Build artifacts are separate from physical-log caps. Package defaults to NOT_RUN,
execution_ready=false,qualification=NOT_QUALIFIED;no flash/capture-live action.

Validate an existing package against its manifest/ELF/maps/metadata. Generate
capture templates with missing unit/revision/recovery/transport explicitly null;
do not convert an incomplete template into an ingest manifest silently. Future
actual capture must independently bind image/readback,unit,clock/placement,
complete trial schedule,numeric oracle and telemetry transport.

Register/push this contract before implementation. Test host lifecycle/formatter
and canonical parser interoperability;then fmt/Clippy and opt-in target/link
checks. Freeze/push implementation before final clean-source package build;
publish a separate package receipt/handoff. Parent RC-04 and all physical/model
rights gates remain incomplete,regardless of build/test success.
