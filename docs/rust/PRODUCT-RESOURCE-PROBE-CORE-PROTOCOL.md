# RC-04A-2 fixed no_std resource-probe core registration

Date:2026-10-27. Baseline7d1d7a3167c695b61858ec59812bc0ff88059c39.
Status:REGISTERED_NOT_IMPLEMENTED. [Machine registration](PRODUCT-RESOURCE-PROBE-CORE-PROTOCOL.json).
This is software preparation under the accepted RC-04 split. No physical case,
timing experiment,trained model or consumer activation is registered here.
Existing physical TP-02/03/04 recipes and numeric gates remain unchanged.

## Boundary and frozen paths

Standalone `rust/probes/resource-core` crate, no default core/runtime integration,
`#![no_std]`, `#![forbid(unsafe_code)]`, no allocation, I/O, timer or HAL imports.
Files:Cargo.toml,Cargo.lock,src/lib.rs,tests/probes.rs under that directory.
No old Cargo/module/protocol needs modification. Package
`libapta-resource-probe-core`0.1.0,edition2021,MSRV1.81 declaration,explicit opt-in
manifest only. Dependency `libm = =0.2.16`,default-features=false,already cached
in the local toolchain; no install/download or alternate backend.
Actual executed compiler is recorded separately; MSRV is not claimed tested.

Public fixtures in `fixtures/resource-probe-core-rc04a2/`:copy.u32le65536B,
projection.i32le9600B,nonlinear.f64le224B,oracles.json and independent verify.py.
Projection2400 rational outputs and copy16384 words are exact; activation/log
references use independent Decimal/Fraction math. Verify references before code.
Candidate library does not include expected-output fixtures; tests alone read them.

## Common ownership, work and cancellation

All buffers are caller-owned and checked before writes. Constructors accept
buffers at least required length; oversize tails remain unchanged. Mutable output
storage remains borrowed until job drop; result views borrow the job, preventing
reuse/mutation while observed. No owned output/callback/retained transient source
block. Fixed input/weight slices are immutable for the full projection lifetime.

Status:Running,Ready,Cancelled,Failed. Ready is numerical completion of a probe,
not a device timing certificate. result() is None unless Ready. Cancel/error
leaves an uncertified prefix; no erase,resume or reset claim. A fresh constructor
is required for another trial. Global offsets/counts are fixed, not caller-configurable.

step(quota,cancel:&AtomicBool) returns units,status,phase,error. quota0 is inert
ZeroBudget even at terminal. Stable terminal precedes later flags/inputs. A pending
flag or flag observed before a charged unit terminally cancels, records interrupted
phase and performs no next-unit write. Clearing flag cannot resume. Check caps
before increment/work; each successful charged unit does exactly the operation
below, no hidden full-buffer scan. A library scalar call is one host quantum,
not an interruptible operation or target WCET guarantee. No timing claim is made.

## Copy: fixed TP-02 recipe

CopyProbe::new(source:&mut[u32],destination:&mut[u32]),both>=16384 words.
Initialize16384 units:source[i]=u32(i).wrapping_mul(2654435761).
Then32 passes, each Prepare16384 units(dest[i]=!source[i]),Copy16384 units
(one word read/write),Verify16384 units(exact copied word vs source, explicit
Mismatch error). Finalize1 unit ->Ready. Total1589249 units. Source/destination
views exposed only on Ready. Safe disjoint mutable borrows forbid aliasing.
Verification/setup are separate phases for future timing; core includes no hash
or hardware memory traffic assertion. Caller may hash finalized bytes afterward.

## Projection: fixed TP-03 recipe

Projection<T> for sealed f32/f64 only. new(input,weights,output) requires
2400/22500/2400 elements. ValidateInput2400 and ValidateWeights22500 units
check exact recipe bits before output writes:
x[r,k]=(((r+3k)%17)-8)/32,w[k,j]=(((5k+7j)%19)-9)/64.
Helpers expose fixed input/weight recipes, never expected outputs.

For each of2400 outputs, Dot150 units in ascending k:product in T,then add in T,
bias0; no mul_add/fast-math. Commit1 unit writes one staged output. Finalize1.
Total387301 units,360000 MACs. Every MAC is its own cancellation boundary,
stricter than the prepared<=32-MAC chunk limit. No dot unrolling into an
uncharged unbounded call. Output must bit-match numerator/2048 in both dtypes;
all terms/sums are exactly representable for these public dyadic fixtures.
Target code-generation/backend verification remains RC-04A-3.

## Nonlinear: fixed TP-04 recipe

NonlinearProbe constructors distinguish f32 activation output and f64 log output;
each output buffer>=28672 values. Kind is Sigmoid,Tanh,Log,LogDiv15.
Input palette loops index%7 for4096 cycles. Activation points[-20,-2,-1,0,1,2,20].
Sigmoid uses libm::expf with stable f32 branches:x>=0 =>1/(1+expf(-x)),
x<0 =>expf(x)/(1+expf(x)). Tanh uses libm::tanhf.
Log input is f32 bits[0,0x80000000,1,0x3d4ccccd,0x3eaaaaab,0x3f000000,0x3f800000],
promoted to f64. Log uses libm::log(p);LogDiv15 uses separate f64 p/15 then log.
Either signed zero maps exactly to-Inf without log invocation. No epsilon/clamp.
Eval1 unit validates/stages one scalar;Commit1 writes it. Finalize1. Total57345
units per kind. Activation tolerance2e-6+2e-5*abs(reference);finite logs4e-13,
zero-Inf exact. Fail on NaN,+Inf or invalid output domain before commit.

## Verification, stop policy and handoff

Debug/release tests:full fixtures under quotas1,31,4096,usize::MAX;exact work,
MAC/pass/output counts and per-schedule output identity. Constructor short buffers
and tails;projection malformed input/weights before writes;cancel in every phase,
including staged partial output and before Finalize;quota0/terminal priority;
internal cap/mismatch pre-write refusal;compile-fail result lifetime tests.
Use existing host compile/format/Clippy tools. No_std RV32 library build may be
checked with installed target; it is not the later HAL adapter or physical run.

Software verification uses independent fixed oracles,not performance scoring.
Stop on numeric/work/ownership mismatch; fix publicly synthetic implementation
before final freeze and disclose repairs. Do not change fixed oracle/tolerance
or existing protocol to make a candidate pass. No measured device/host benchmark
run is implied; physical trial schedule,placement,clock,requester and gates remain
in their prepared protocol and RC-04B. Commit/push registration before code;
commit/push tested implementation receipt before RC-04A-3. Existing19 telemetry
tests remain controls; old numerical components are preserved by hashes.
