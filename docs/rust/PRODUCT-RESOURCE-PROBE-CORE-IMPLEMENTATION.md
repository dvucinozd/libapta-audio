# RC-04A-2 fixed resource-probe core implementation

Date:2026-10-27. Registration source9ece658b238cb221fb387ca5748c8c94aa647528.
[Frozen protocol](PRODUCT-RESOURCE-PROBE-CORE-PROTOCOL.md),
[implementation receipt](PRODUCT-RESOURCE-PROBE-CORE-IMPLEMENTATION.json).

The isolated `rust/probes/resource-core` crate implements the fixed TP-02/03/04
artificial arithmetic in safe no_std Rust. It is an explicit standalone manifest,
outside the default core/runtime workspace; no existing feature/module/default
was changed. It has no HAL,allocation,timer,I/O,model or telemetry producer.
Caller-owned buffers are checked before writes; final views borrow the job.
Cancelled/failed prefixes remain uncertified and expose no result or resume.

Copy performs source initialization then32 Prepare/Copy/Verify passes, with
one word operation per charged unit,1589249 total. Setup/verification phases are
distinct so a future adapter can time copy separately. Projection accepts only
the exact fixed input/weight bits,validated before output writes. Both f32/f64
produce2400 bit-exact dyadic outputs in360000 ordered MACs,387301 charged units.
Nonlinear Sigmoid/Tanh/Log/LogDiv15 each emit28672 values with separate Evaluate/
Commit phases,57345 units. Copy uses two65536B caller buffers; projection payload
is109200B for f32 or218400B for f64; nonlinear output is114688B f32 or229376B f64.
These are payloads,not whole-device peak or approved memory placements.

Every MAC/word/scalar boundary checks cancellation and the work cap before the
next unit. quota0 is inert;stable terminal state wins over later flags. Staged
projection/nonlinear values are not committed on cancellation. The one libm
scalar call within an Evaluate unit cannot be interrupted by this API; its target
WCET is unmeasured. No physical50ms claim follows from host cancellation tests.

The backend is pinned cached libm0.2.16 with default features disabled and a
committed lockfile. Sigmoid uses stable f32 expf branches,tanh uses tanhf;logs
promote input to f64,divide before log where required,and branch exact signed
zero to-Inf. Tests retain activation2e-6+2e-5*abs(reference),finite log4e-13 and
exact zero rules. Candidate code reads input recipes only;expected outputs are
test fixtures verified independently using Decimal/Fraction and modular recurrences.

Best-effort `core::hint::black_box` input barriers discourage folding fixed
palettes/repeated copies or proving verification redundant. They are not volatile
memory operations,hardware bus/cache evidence or a guarantee about final firmware
optimization. Target adapter assembly/link inspection and actual traffic/timing
validation remain RC-04A-3/04B. No benchmark clock is present in this core.

## Verification scope

- Nine integration tests and two internal tests pass in debug and release.
  Each full copy,both projection types and all four nonlinear kinds run quotas
  1,31,4096,usize::MAX:28 full workload/schedule combinations per build. Outputs,
  exact counters/work,tail canaries and schedule byte identity pass.
- Constructor capacity checks,input/weight corruption,typed staging,cancel in
  all common phases,quota0/terminal priority and no-result/no-resume pass.
  Internal injected work caps refuse before pending writes;copy verification
  mismatch fails explicitly. No cap/oracle/tolerance change was needed.
- Two compile-fail lifetime examples pass per profile:view cannot outlive the
  job or coexist with mutable job operations. Host tests allocate buffers,while
  the library remains allocation-free and forbids unsafe code.
- Independent public verifier checks16384 copy words,2400 exact projection
  outputs/both serialized hashes and28 nonlinear reference scalars. It does not
  execute/import the candidate.
- Formatting and all-target focused Clippy(-D warnings) pass. Installed
  riscv32imafc-unknown-none-elf release library build passes with no_std.
  This is a library artifact,not a linked HAL firmware image,boot or device run.
- Existing19 telemetry host controls pass in WSL;prior Windows results remain
  recorded with the unchanged parser. Old HS sources/assets/395 bindings and
  VERSION1.0.1 are preserved by hash checks;their unrelated matrices were not rerun.

Actual compiler/Cargo is1.95.0. The declared1.81 minimum was not executed.
Commands,source/backend/artifact hashes and raw check logs are bound in the
machine receipt and external validation directory. No performance score or
musical/physical qualification is assigned to test completion times.

Final evidence:`D:/AI/LIBAPTA/outputs/rc04a2-core-validation-20261027-02/`.
Its18 files/28923B include final check logs and backend/source manifests after
normalizing a trailing blank line in Cargo.toml. Earlier development evidence
remains in validation-01 (19 files/29750B),including an explicit post-check
metadata incident. That collector initially looked for a host rlib
in release/ after cargo test;the actual artifact is in release/deps/. Existing
successful logs were preserved and the artifact path corrected without rerunning
those checks or changing candidate/backend/oracles. Final source-bound checks
were then captured separately after whitespace normalization. Archive/source identity checking
confirms the cached libm sources match the pinned package checksum.

Development before freeze added typed cancellation/cap coverage,positive-log
finite-domain checking and optimization barriers. These are disclosed synthetic
implementation refinements,not a measured-run repair/retry. Frozen reference,
registration,prior results and numeric gates remain byte-identical.

## Handoff

RC-04A-2 is complete for the fixed pure core. Next is RC-04A-3: prepare a guarded
opt-in target adapter and compile/link/map/capture package,using this explicit
crate rather than changing the accepted analysis algorithm. Bind actual timer,
memory ownership,backend/code generation and producer format;reuse telemetry V1.
Inspect consumer worktrees before edits. Do not remove Guition product guards,
change the libapta pin/provider or invent a programming address/port.

Physical RC-04B,all nine PF01 allowances,exact-unit/recovery/resident BSP,
allocator/largest-block/stack telemetry,actual5s/60s/50ms and code/checkpoint
rights/provenance remain open. No full trained graph,codec bridge,consumer
adoption or new musical corpus has been implemented or executed here.
