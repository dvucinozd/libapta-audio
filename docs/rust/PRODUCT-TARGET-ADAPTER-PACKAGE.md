# RC-04A-3 clean target link-check package result

Date:2026-10-27. Verdict:**AUDIT_PASS_BUILD_ONLY; EXECUTION_BLOCKED**.
Clean pushed build source:`489e5af0beac1cef53c1276a3aad65d7687255b8`.
[Machine result](PRODUCT-TARGET-ADAPTER-PACKAGE.json),
[implementation](PRODUCT-TARGET-ADAPTER-IMPLEMENTATION.md),
[registered boundary](PRODUCT-TARGET-ADAPTER-PROTOCOL.md).

All five variants built once in fixed tool order:copy,projection32,projection64,
activation,logs. No development flag,source repair,dependency installation,cap
change or retry occurred in this clean build. Source stayed clean at build and
audit;the final documentation commit is separate from the compiled source SHA.

The clean package retains49 files/39411271B,below64MiB:ELF,map,headers,symbols,
disassembly,build stdout/stderr/supervision and capture template per variant,
plus package/source/compiler/dependency metadata. The separate audit JSON binds
all49 file hashes. Package manifest SHA-256:
`9c460c062c54a2622185a2b464d25d2f98963f5b24aa845c4288e6cf56752a95`.
Paths:

```text
D:/AI/LIBAPTA/outputs/rc04a3-package-clean-489e5af-20261027/
D:/AI/LIBAPTA/outputs/rc04a3-package-audit-489e5af-20261027/audit.json
```

## Compiled reservations and instruction observations

| Variant | Caller buffer payload B | Linked BSS B | ELF file B |
|---|---:|---:|---:|
| copy |131072|131260|4066880|
| projection32 |109200|109388|4074128|
| projection64 |218400|218592|4072228|
| activation |114688|114876|4072104|
| logs |229376|229568|4072172|

BSS is a linker reservation including the selected buffers and base-image/control
state. It is not a measured peak,free RAM,stack high-water or approved product
analysis allowance. The other product owners are not active in these images.
ELF file bytes include debug/non-load data and do not establish programmed flash
footprint;program headers/maps are retained,and no flash layout was selected.

Static inspection finds five fmul.s and one fadd.s instruction sites in the
projection32 image,with zero f32 fused multiply-add/subtract sites in that image.
Software double multiply/add symbols are linked. These are whole-image static
observations,not executed instruction counts,isolated per-kernel throughput,
target numerical validation or a timing estimate. Existing fixed-reference host
tests remain the numerical evidence until physical execution is qualified.

## Identity and software checks

The package/audit rechecks14 selected source files,24174 registry source files,
tools and actual Rust/Cargo binaries,all nine adapter implementation components,
authority chains and395 earlier frozen bindings. Every ELF is RISC-V32,contains
the exact image/source/protocol tags and retains the uncalled trial symbol.
No or multiple variant selection refuses compilation with the intended guard.
The package verifier rejects hash/size/file-set/feature/ELF/readiness mismatches.

Seven debug and seven release adapter tests pass. Six WSL package tests and19
telemetry controls pass;Windows package controls are five passes plus one Linux-
specific NOT_RUN. Host/target Clippy and fmt pass. The Rust producer's synthetic
log is accepted by the canonical Python parser with19 unknown metrics and
NOT_QUALIFIED. Those fake ticks are not device measurements.

Build tools are pinned Rust1.95.0,target riscv32imafc-unknown-none-elf,HAL1.2.2,
policy302 and cached locked dependencies. llvm-objdump/readelf tool identities
are bound. The first variant's observed host build wall is32.733s;remaining
variants0.816–0.918s reuse compiled dependencies. Maximum observed compiler/process
RSS is976128KiB. All180s/2GiB per-process/64MiB operational gates pass. These
host build figures are unrelated to P4 execution speed or memory.

## What is ready and what remains open

RC-04A-3 is **complete for software adapter/link/package preparation**.
The HAL boot path initializes and idles;it never automatically runs a probe.
Capture templates deliberately contain null unit/revision/schedule/arming and
are not ingest manifests. The package always has execution_ready=false and
qualification=NOT_QUALIFIED. Nothing was flashed,booted,captured from a port or
measured on a device. No consumer code,provider,pin,trained model or corpus changed.

Physical RC-04B still requires:

- Exact-unit identity/revision/access,factory backup,recovery and programming layout.
- Qualified arming/control/telemetry transport and buffer reuse across complete
  warm-up/trial schedules;the retained entry is single-use link scaffolding.
- Numerical output verification/digests against the registered public oracles.
- Resident BSP/allocator/stack/error metrics and the asynchronous cancel requester.
- Actual placement/timing/coexistence measurements and justified PF01 allowances.

All nine physical resource categories,5s/60s/50ms,model rights/provenance and
product activation remain open. Next dispatch is RC-04B prerequisite resolution,
not a new model or another toy DSP experiment. Missing independent runtime
integration is explicit;having a link-check ELF alone is insufficient to execute
the registered physical protocol or claim its completion.
