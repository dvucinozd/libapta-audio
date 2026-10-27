# RC-03 preparation: bounded exact-board resource probe

Date: 2026-10-27. Source `1e029785500e8a0ae3d674b63c56bede77cf3779`.
Status: **PREPARED_WITH_EXPLICIT_BLOCKERS; NOT_EXECUTION_READY; NOT_RUN**.
[Machine protocol](PRODUCT-TARGET-PROBE-PROTOCOL.json),
[route/owner decision](PRODUCT-ROUTE-DECISION-20261027.md).

## 1. Hypothesis and execution gate

An opt-in, fixed artificial target probe can establish exact-unit identity,
bounded CPU scratch/math timings and, once product owners/allocator telemetry
exist, residual memory under real resident owners. It executes no trained model
or musical corpus and never qualifies the full5s/60s/50ms product by itself.

Pinned consumer source:4a8c660183910b0950ae7ddabce9e8b395a2f10d.
Board:JC1060P470C_I_W_Y only for physical conclusions. Rust1.95.0;
`riscv32imafc-unknown-none-elf`; HAL minimum chip revision302. Actual board revision,
port, transport, recovery and image layout must be read/verified, not inferred.

Prerequisites before any physical execution:

1. Exact unit is available; capture identity/factory log and verified image backup;
   establish ROM/recovery and reviewed programming procedure from the first-boot
   runbook. No flash command/address/COM port is supplied by this preparation.
2. Review current consumer SHA and preserve local unrelated work. Any new probe
   source is implemented in an appropriate isolated checkout and frozen/pushed
   before measurement. A source change requires a new exact-image identity.
3. Resolve platform capability blockers: Guition product owner bindings; one
   PSRAM/allocator owner; internal/PSRAM memory classification; used/free/min-free/
   largest-free reporting; task-stack high-water and error/underrun counters;
   reviewed monotonic timer; observation collector and bounded cancellation.
4. Freeze actual feature/image/resident set, target-math implementation/dependency
   and reference assets/hashes, telemetry schema and control transport. Unknowns
   listed in JSON must be resolved by a new execution-ready registration commit
   before measurement. This document itself does not claim that freeze occurred.

At the pinned source the resident workload is compile-guarded, the existing64KiB
PSRAM boot probe registers no allocator, and existing serial boot snapshots are
not live memory/stack telemetry. These are BLOCKED_CAPABILITY, not zero usage.
Do not remove the product guards without the required BSP work or combine
`guition-psram-test` with another owner of its exclusive scratch mapping.

## 2. Existing commands and exact implementation boundary

Existing BUILD_ONLY path, from consumer root on the pinned/new reviewed checkout,
with new external absolute directories:

```text
python3 firmware-rust/tools/guition_first_boot.py build --probe memory --output NEW_EXTERNAL_PACKAGE --target-dir NEW_EXTERNAL_BUILD
```

This tool builds separate jc1060 and jc8012 packages; do not transfer jc8012
evidence. The jc1060 package may later use existing `ingest` or explicit-port
`capture` per the runbook. These commands do not program the board, provide a
layout, activate product owners or measure residual product memory. Do not run
them in the stale local consumer checkout in this RC-03 preparation.

Proposed implementation files, not existing functionality:

- `firmware-rust/apps/m1-firmware/src/platform/resource_probe.rs`: fixed math/
  copy cases, timer/cancel wrapper and bounded telemetry; platform unsafe code
  must document exclusive regions/capability invariants.
- `firmware-rust/crates/ruzina-board-profile/src/resource_probe.rs` and
  `tests/resource_probe.rs`: HAL-independent case IDs/formulas/report parser and
  oracle checks. Use existing module conventions; final exact paths are frozen
  in the implementation registration before edits.
- Existing platform memory/diagnostics modules: minimal opt-in wiring only after
  memory ownership is resolved. Proposed feature `guition-resource-probe`, no
  default activation. Separate resident capability work is not a one-line guard
  removal and must keep APTA provider/core pin unchanged.
- Consumer `firmware-rust/tools/guition_resource_probe.py`: extend/reuse existing
  first-boot identity/log ingestion helpers, not another flashing tool. Strict
  parsing, exclusive output, image binding and resource refusals required.

Planned build template after capability/feature implementation; NOT executable
against the current source:

```text
cargo +1.95.0 build --locked --offline --release --target riscv32imafc-unknown-none-elf --manifest-path firmware-rust/apps/m1-firmware/Cargo.toml --target-dir NEW_EXTERNAL_BUILD --features board-jc1060,board-diagnostics,guition-resource-probe
```

Run it from the app directory so its `.cargo/config.toml` linker/min-revision
configuration is applied. Resident-image features require a separate exact list
after qualified BSP wiring; they are currently null, so that build is blocked.
Bind map/ELF and actual flags/clock configuration, not merely command text.

## 3. Fixed prepared cases and numerical references

Execute cases in the order below after all gates close. Each numeric/copy case
uses one warm-up plus three measured trials, with all trials retained, no adaptive
loop count or fastest-only selection. No automatic rerun after a failing attempt.
Numeric reference and all expected output bytes must be generated/independently
verified and hash-bound before candidate implementation and execution freeze.

| ID | Work and fixed input | Reference / placement | Scope and cap |
|---|---|---|---|
| TP-00 | Existing boot identity and read-only RDID/64KiB scratch probe | Existing board-profile memory oracle and strict first-boot ingest; sole mapping owner | Once per observed boot scenario; capture<=300s/1MiB; CPU_SCRATCH_PASSED only |
| TP-01 |30s actual resident stopped-deck baseline; sample once per100ms | Counts plus per-class allocator/map evidence; no synthetic allocation substitute |300 scheduled samples, missed/null samples explicit; host wall35s; no model allocation |
| TP-02 |65536B copy buffer pair,32 sequential full copies per trial; source word `(i*2654435761) mod2^32`, destination complemented before each copy | Exact little-endian word comparison after each copy, SHA-256 final buffer; internal and PSRAM placements separately | Two65536B buffers in approved same class;2MiB copied/trial; warm-up+3;10s/trial |
| TP-03 | f32 then f64 artificial projection:16 input rows,150 input/output dimensions; dot ascending k, product then add, bias0 | `x[r,k]=(((r+3k)%17)-8)/32`;`w[k,j]=(((5k+7j)%19)-9)/64`; exact rational sums denominator2048; SHA exact output bytes |360000MAC/trial/type; warm-up+3 per type;10s/trial; loops chunk<=32MAC per cancellation check |
| TP-04 | sigmoid/tanh at[-20,-2,-1,0,1,2,20] repeated4096; f64 density logs on registered seven-value palette repeated4096 | HS05 activation bound `2e-6+2e-5*abs(reference)`; HS06 logs4e-13, exact zero->-Inf; immutable independent references |28672 input points/function; warm-up+3;10s/trial; explicit target math backend required |
| TP-05 | One bounded cancellation trial in each numeric/copy phase after its first completed chunk; separate scheduled flag while subsequent work is active | Flag observed, no resumed work/new certificate; all timing sources bound | One trial/phase; cancellation fence target50ms; close/reclaim separately reported; no indefinite await |
| TP-06 | Five fixed cycles stopped-deck -> observed playback request -> idle, retaining old result/banks | Actual product owner state, underrun/error counters and per-class free/largest-block before/after |<=30s/cycle; baseline owners must really run; no synthetic heartbeat substitute |

TP-02 is CPU-cached copy traffic, not a DMA/cache coherency test or DRAM bus
bandwidth guarantee; record read/write/check costs separately. Buffer pair
requires131072B in the tested class; if allowance is missing/insufficient, mark
UNSUPPORTED_CAPACITY and stop that case rather than shrinking it. Do not claim
the remaining protocol fully passed. TP-03 weights90000B plus input9600B/output9600B
for f32, double for f64, and allocation/stack controls additional. Preflight
checked capacities before writes, exact arithmetic/no-FMA semantics and no
constant folding/elision of repeated trials; consume/hash every result outside
the timed kernel. F64 target may be software; inspect/link-bind the actual path.

TP-04 log palette is binary32 `[+0,-0,min-positive-subnormal,0.05,1/3,0.5,1]`.
For each promoted value evaluate `ln(p)` and `ln(p/15)` separately, zero by exact
-Inf branch. This is a scalar math test, not a sum-to-one probability triple.
Use f32-cast sigmoid/tanh outputs and stable reference formulas; freeze target
library/version/flags before running. No inference weights are included.

TP-05 has an actual in-flight request, unlike HS06's pending-only host probe.
The requester/timer/interrupt latency and job-side observation clocks must be
specified/tested before execution; these capabilities remain UNRESOLVED. If the
request races terminal completion, preserve that trial as INCOMPLETE, do not
extend work or replay to obtain a preferred latency. A component stop still
does not establish the whole-product publication fence or cleanup deadline.

## 4. Limits, measurements and refusal behavior

All inputs, loop counts and trial counts above are fixed preparation choices,
not approved product allocations. Allocate only from explicitly permitted regions
after baseline observation; record alignment/address/length/class, heap capacity,
free/minimum/largest block before/during/after, stack/future sizes and current
resident-owner mask. Verify scratch recovery/checksum and no stale DMA/owner alias.
An unsupported metric remains null and prevents PF01 closure.

Use a reviewed `embassy_time::Instant` monotonic source with raw ticks and actual
tick frequency plus firmware-measured duration. Existing `as_millis()` boot sample
does not establish sub-ms precision. Require proven tick resolution<=1ms for
50ms cancellation observations; record calibration/overhead without subtracting
unknown cost. Bind CPU and PSRAM clocks/modes, core/task affinity and scheduling.
These configuration values remain unresolved until observed/frozen.

Per trial device wall cap10s; independent host capture cap **60s per numeric
variant** (each memory class and each floating dtype separately). TP-01 host
35s, TP-06 host180s, TP-00 capture300s. Separate case captures, each<=1MiB;
aggregate retained attempt log/telemetry/audit cap8MiB, excluding build products.
Preserve raw logs and partial failures. Collector must refuse truncated/duplicate/
unknown required fields, wrong unit/image/protocol, missing trials, clock mismatch,
buffer mismatch and unbounded data. Display every trial; report min/median/max
only alongside all observations, no extrapolated full-model product PASS.

Stop on integrity fault, numerical mismatch, panic/reset, timeout, storage/stack
overflow or observed audio/USB fault. Preserve evidence. No changing clock,
placement, case size, tolerances, backend or repetition count within an attempt.
No background probe while it compromises playback; the product cancellation path
must be active and its request recorded. Recovery/reset procedures must be
prequalified, not generated ad hoc to keep a test running.

Return separate statuses: INPUT/IDENTITY_PASS, NUMERIC_PASS, MICROBENCH_MEASURED,
RESIDENT_RESOURCE_MEASURED, INVALID/INCOMPLETE or BLOCKED_CAPABILITY. Even a full
probe pass does not establish musical accuracy, sustained full-neural/HMM timing,
DMA qualification, final native publication or stable release readiness.

## 5. Implementation and freeze handoff

Before code, resolve each JSON blocker and record exact build/feature/flash/
capture commands and actual telemetry APIs. Implement the smallest platform
probe, host parser/oracle tests and target compile/link/map; tests must include
negative malformed logs/resource refusal and missing-metric handling. Commit/push
implementation before physical measurements. Bind source, oracle inputs, target
math, compiler/Cargo/linker/HAL, ELF/map/binary, actual unit, protocol and collector.
Then execute the registered attempt once and publish `PRODUCT-P4-BUDGET-RESULT`
with all nine categories still UNKNOWN where proof is absent. Free bytes become
approved analysis reserve only after resident/lifetime/fragmentation and safety
margin review; no default percentage is invented here.

At this checkpoint: no probe source, target build, programming, port opening,
device measurements, new backend or product activation was performed. The
protocol preparation and explicit prerequisite list are the deliverable.
