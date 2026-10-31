# RC-04B-L1: legacy ESP-IDF probe registration

Date: **2026-10-31**. Status: **REGISTERED_NOT_IMPLEMENTED; NOT_EXECUTION_READY**.
LIBAPTA input: `790480fca57936eccea0ac2134f814fbf9f94985`.
[Machine registration](PRODUCT-LEGACY-IDF-PROBE-PROTOCOL.json),
[arrival evidence](PRODUCT-BOARD-ARRIVAL-20261031.md),
[physical parent protocol](PRODUCT-TARGET-PROBE-PROTOCOL.md).

## 1. Hypothesis, scope and veto

An IDF-owned legacy P4 runtime can link the existing fixed, HAL-independent Rust
probe arithmetic through a narrow C ABI without relaxing the native v3.x policy.
This is backend preparation, not a new DSP candidate. Fixed inputs/counts,
numerical references/tolerances and cancellation/publication limits remain those
of the parent protocol and resource-core registration. No model/audio is needed.

The next implementation, **RC-04B-L2**, delivers a standalone Rust ABI/producer,
offline V2 parser and one disarmed legacy IDF build/link package. The package
retains an uncalled runner address to require actual linkage. Boot initializes
the reviewed IDF platform, reports readiness and idles; no probe runs at boot.
There is no flash/capture/arming command in this slice. A compiled synchronous
runner is not a qualified full physical scheduler. Runtime arming, full repeated
trials, independent numerical verification and exact-image physical admission
need a separate execution registration after this implementation is frozen.

Veto: any change to frozen core/oracles, native minimum302, V1 acceptance domain,
default product behavior, consumer pin/provider or release VERSION. Do not patch
image headers, suppress ABI mismatches or combine IDF and esp-hal startup.
Missing capability is BLOCKED/UNKNOWN, not PASS. No target, model or serial
execution occurs while registering or building this preparation.

## 2. Platform and ownership

Use the reviewed consumer `926e90506c4698cc68ada045347d21ded84f649a` as the source
reference for legacy startup/configuration and GCC runtime ordering. Keep its
checkout untouched. The separate LIBAPTA project is under
`ports/espidf/legacy-resource-probe/`; it is not the installed C component.
Reuse the architecture and small configuration/link scaffolding with source
attribution; do not copy vendor BSP/panel tables or unrelated product owners.

- IDF **6.1.0**, source `fff9895c82d744c7237be8847347bdd1b07c6643`, owns startup,
  ROM map, FreeRTOS, drivers, clock, heap and termination.
- GCC **esp-15.2.0_20251204**, `-march=rv32imafc -mabi=ilp32f`; resolve and hash
  its actual libgcc before the Rust archive, as in the reviewed consumer.
- Rust/Cargo **1.95.0**, `riscv32imafc-unknown-none-elf`, locked offline build.
  Dependencies: existing resource-core, default-feature-free target-adapter and
  their pinned libm **0.2.16**; no esp-hal/esp-rtos runtime or new crate downloads.
- Select legacy revision profile **100..199**, CPU **360 MHz**, flash **16 MiB**,
  DIO/40 MHz, USB Serial/JTAG, FreeRTOS **1000 Hz**. Runtime unit checking later
  must match the actual **103** and recorded MAC; policy is not a detector.
- Initial package uses **internal CPU-accessible buffers only**, PSRAM disabled,
  no DMA/audio/USB-host/display/SD/C6 owners. USB Serial/JTAG is console transport,
  not a running USB-host workload. Main task stack is12288B. Allocate the exact
  selected payload using `heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)`;
  refuse allocation failure, with no PSRAM fallback or smaller case. PSRAM/
  resident/product cases remain deferred.
- C owns all buffers/allocations, callbacks and task lifetime. Rust performs no
  allocation and retains no caller pointer after the synchronous ABI call.
  No allocator crosses C/Rust; no packed structs, Rust slices/enums/bools/usize
  or unwinding cross the ABI. Target panic calls IDF abort.

The nominal timer is `esp_timer_get_time()`, nonnegative signed64 microseconds,
1,000,000 Hz. Reject negative/backwards readings; no wrap repair or calibration
claim. This is an explicit legacy-backend replacement for the original prepared
embassy timer choice; that historical document is unchanged. Timer resolution,
physical calibration and asynchronous requester evidence remain future gates.

## 3. Frozen synchronous ABI

Header symbol prefix `apta_lp_v1_`; ABI version **1** is distinct from telemetry
schema2. Export exactly `apta_lp_v1_run` and the pure query
`uint32_t apta_lp_v1_report_bytes(void)`. The query returns168 and never
initializes hardware or runs arithmetic. The header defines ABI version1.

```c
typedef uint64_t (*apta_lp_v1_clock_us)(void *user);
typedef int32_t (*apta_lp_v1_cancel)(void *user);
int32_t apta_lp_v1_run(
    uint32_t abi_version, uint32_t selector,
    void *a, uint32_t a_bytes, void *b, uint32_t b_bytes,
    void *c, uint32_t c_bytes, uint32_t quota, uint64_t limit_us,
    apta_lp_v1_clock_us clock_us, apta_lp_v1_cancel cancelled,
    void *user, struct apta_lp_v1_report *report, uint32_t report_bytes);
```

Report field order is four u32 (`abi_version,struct_bytes,status,error`), six u64
(`core_units,macs,outputs,ticks_start,ticks_end,elapsed_ticks`), twelve u64
`phase_ticks`, then two u32 (`final_phase,flags`). Expected size **168 bytes**;
verify C and Rust offsets/size/alignment on host and RV32 rather than assuming
host parity. Flags bit0=timing valid, bit1=complete output valid; other bits zero.
Status:0 failed,1 complete,2 cancelled. Error:0 none,1 ABI/report-size,2 geometry,
3 selector/quota/limit,4 clock/callback,5 timeout,6 core failure. Return0 complete,
1 cancelled, negative error number otherwise. Failure never returns a valid
complete-output certificate, even if partial caller buffers have changed.

| Selector | Buffers and exact payload bytes | Meaning |
|---|---|---|
| 0 | a65536,b65536,c absent | TP-02, two u32 buffers |
| 1 | a9600,b90000,c9600 | TP-03 f32 input/weights/output |
| 2 | a19200,b180000,c19200 | TP-03 f64 input/weights/output |
| 3 | a114688,b/c absent | TP-04 f32 sigmoid output |
| 4 | a114688,b/c absent | TP-04 f32 tanh output |
| 5 | a229376,b/c absent | TP-04 f64 ln(p) output |
| 6 | a229376,b/c absent | TP-04 f64 ln(p/15) output |

Absent means null/zero. Require exact lengths, natural element alignment and
checked address-range arithmetic; reject overlapping buffers/report before
creating Rust references. Non-null foreign pointers still require valid, live,
exclusive C allocations: range checks cannot authenticate arbitrary pointers.
Callbacks/user storage must remain valid and must not alias or access borrowed
arrays/report, reenter the runner, block, unwind or mutate Rust state. A clock
callback returns UINT64_MAX to signal its error; other readings must fit i64.
Cancel callback returns only0/1; any other value is callback failure.

Quota1..32, limit1..10,000,000us. Invalid/zero quota refuses before work; preserve
the lower-level zero-step semantics. C prepares projection inputs/weights from
the frozen public recipes before calling; their preparation is outside this
kernel interval and must be measured separately before any product-time claim.
Copy/nonlinear setup follows the existing core. Fresh borrowed jobs only.
Use the existing `Trial` timing/phase fences with a callback clock and a local
Rust AtomicBool populated from the cancel callback before every bounded step.
No transmutation between C atomics and Rust AtomicBool. Stable completion never
resumes. A pending cancel before timer initialization has flags0 and no invented
measured interval. A late/backwards clock invalidates output. Phase slots0..10
match the existing adapter; slot11 stays zero. FFI setup/control overhead is not
an isolated MAC measurement. This callback bridge alone does not prove an
independent in-flight TP-05 requester or whole-product50ms cancellation.

## 4. Telemetry V2: separate identity and strict offline integrity

Dedicated prefix **`M1_LEGACY_RESOURCE_PROBE_V2 `**, JSON schema integer **2**.
Keep V1 source/tests/assets unchanged. V2 does not rewrite identities or route
revision103 through a302 manifest. Limits, mandatory observation/end fields,
memory classes,19 nullable metrics, status names and terminal/order rules are
the V1 envelope rules, with only the explicit V2 changes below.

Identity adds `backend`, `consumer_revision`, `idf_revision`, `app_sha256` to the
V1 nine fields. Backend is literal `esp-idf-6.1-p4-legacy`; chip revision is an
integer **100..199**, with an exact type/value match to the expected manifest.
Consumer/source/IDF revisions are40 lowercase hex; IDF must equal the pin above.
App/ELF/protocol digests are64 lowercase hex. Board stays JC1060P470C_I_W_Y.
Synthetic/device_log remain explicit. An execution manifest must use freshly
confirmed103 for this unit; parser support for a domain is not device admission.
Manifest clock is exactly1,000,000Hz and counter_bits64. Tick values fit i64,
are monotonic and use subtraction, not modular repair.

The three tick fields may be **all null** only for FAILED/CANCELLED/NOT_SUPPORTED
records with no certified timing. Mixed-null timing or MEASURED with null timing
is INVALID; three nulls add three unknowns. Known values must satisfy exact
end-start=elapsed and the planned cap. Do not serialize sentinel/error ticks as
real measurements. Rust V2 formatter uses a separate identity/observation type;
it must not call the V1 revision validator with a fabricated value.

Reuse immutable V1 primitive functions where applicable (`strict_json`, scalar/
key/digest checks, memory accounting), without monkeypatching constants, schema
or identity. Implement the V2 manifest/identity/timing/framing loop separately;
do not call V1 analyze/ingest via an identity-converting shim. Preserve bounded
reads, duplicate/unknown/bool/float/overflow rejection, dedicated-log noise refusal,
exclusive outputs and raw evidence hashing. V1 and V2 cross-inputs must fail.
Limits:1MiB log,128KiB manifest,8192B line,1024 observations,2MiB retained ingest.

All parser results retain `qualification=NOT_QUALIFIED`,
`physical_measurement_verified=false`, `programmed_image_verified=false`,
`experiment_verdict=NOT_EVALUATED`. App hash is host/package metadata, not a
self-hash embedded in its own image or proof of programmed bytes. No automatic
numeric PASS from a digest shape. Numeric digest is null until actual output
bytes and the independent oracle check supply it. No fake zero memory/counters.

## 5. Build/package and next implementation checklist

Only the exact new source paths in the machine registration may be created.
Existing source/oracle/protocol bytes are read-only. The default workspace and
installed C component stay unchanged; no consumer checkout changes are allowed.

1. Implement the standalone crate/header and C ABI tests. Host-only `host-std`
   feature allows test executables; forbid it on the bare target. Target static
   archive uses abort panic, frame pointers and strict ilp32f-compatible linking.
2. Implement V2 formatter/parser and explicit synthetic fixture with public fake
   unit/image identifiers. Test both directions of V1/V2 refusal,103 acceptance,
  302 rejection, null timing/metrics (19 unknown metrics,22 with null timing),
   order/terminal/caps and malformed domains.
3. Implement the separate IDF project, internal-buffer caller and retained
   uncalled runner. IDF startup only; no existing SD/display workload is adopted.
   Build one image containing all seven selectors, with sequential caller buffer
   ownership. Maximum per-selector payload229376B is not a product allowance.
4. Build/package tooling verifies clean source, IDF/submodule/compiler pins,
   resolved dependency files and actual binary/tool hashes. Use new external
   package/build dirs, locked offline dependencies, no install/network fallback.
   Preserve archive,ELF,app,bootloader,partition binary,flasher_args,sdkconfig,
   map,headers,symbols,disassembly,commands,stdout/stderr and source manifest.
   Shipping flasher_args as evidence does not authorize running them.
5. Bind app header P4 chip18/profile100..199, source/protocol tags, retained ABI
   symbol, exact archive and libgcc selection. Refuse mixed startup, ABI mismatch
   suppression, wrong revision/profile, hidden product owners, dirty/pin drift,
   artifact tampering/symlinks/missing/extra files and execution-ready claims.
   No-FMA/exact projection semantics remain mandatory; inspect generated code.
6. Run focused host debug/release tests, C/Rust ABI layout/caller tests, fmt and
   Clippy-Dwarnings, target static-archive/link checks, existing core/adapter and
   V1 controls. Required negatives: null/misaligned/overlap/overflow/short buffers,
   bad callbacks,quota0/33,deadline0/>10s,pending/mid-run cancel,clock reversal,
   late completion,post-terminal fencing. Real V2 producer->parser interop must
   retain unknowns/NOT_QUALIFIED. Report unavailable C-oracle tests as NOT_RUN.
7. Host supervision: build900s wall,4GiB summed live-descendant RSS,128MiB retained
   package; inspection60s/command. Refuse execution if process-tree supervision
   is unavailable. Measure each sequential OS stage where it executes: WSL
   launcher RSS alone cannot qualify its Linux compiler children. Failed/partial
   builds are preserved; never auto-relax caps.
8. Commit/push implementation and verify full remote SHA, then one clean-source
   package and separate result receipt/push. Freeze actual commands,tool/env
   identities and operational results before a future physical execution plan.

Future CLI shape, **not implemented or executable at registration**:

```text
python tools/apta_legacy_probe_package.py build --idf IDF61 --consumer-source M1_PINNED_SOURCE --output NEW_EXTERNAL_PACKAGE --build-dir NEW_EXTERNAL_BUILD
python tools/apta_legacy_probe_package.py verify --package RETAINED_PACKAGE
python tools/apta_legacy_probe_telemetry.py --manifest EXPECTED_V2 --log DEDICATED_V2 --output NEW_EXTERNAL_INGEST
```

Build result must say **BUILD_ONLY_LINKCHECK**, `execution_ready=false` and
NOT_QUALIFIED. No live port/flash/capture option. TP-00/01/05/06, PSRAM placement,
resident allocator/stack/error instrumentation, repeated physical schedule and
operator arming remain future execution gates; this registration does not mark
them implemented. No product reserves or5s/60s/50ms acceptance from link success.
