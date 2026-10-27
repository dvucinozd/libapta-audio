# HS-05: small artificial linear / LSTM / softmax host experiment

Date: 2026-10-27. Baseline `5a29a77de78c3e98542594a68e2bd199096da858`,
branch `rust-rewrite`. Status: **REGISTERED_NOT_IMPLEMENTED**.
[Machine registration](PRODUCT-HOST-NEURAL-PROTOCOL.json),
[public mathematical cases](fixtures/product-host-neural-hs05/cases.json),
[scalar oracle](fixtures/product-host-neural-hs05/scalar_reference.py),
[independent checks](fixtures/product-host-neural-hs05/verify_registration.py).

## 1. Hypothesis and boundary

A small opt-in safe Rust state machine can preserve ordered scalar f32 affine
and recurrent arithmetic, gate/bias/state semantics and stable softmax while
remaining bounded, cancellable and invariant to input/output/work fragmentation.
Baseline HS04 host frontend is complete; the approved next handoff explicitly
calls for separate synthetic primitive registration before any new kernel code.

This is a **toy numerical experiment**, not the trained BeatNet+ graph. Its input
width3,hidden2,two layers and three output slots are mathematical dimensions.
There is no Conv/pool,frontend288 projection or automatic substitution for the
278->150/four-layer150 product graph. Linear has no extra ReLU/activation.
All four coefficient profiles are public arithmetic recipes authored here;
none comes from a checkpoint,corpus,training or neural framework. Reference
calculation evaluates these artificial mathematical formulas only. No trained
weights are loaded/inferred and no model package is installed/imported.

The host work per complete toy row is76 affine MACs and172 bounded step units. A30000-row
case has2280000 MACs and5160000 step units,not the full model's approximately
23-billion-MAC workload. Passing this toy cannot establish full-model capacity,
accuracy,rights,training independence,whole-pipeline memory or P4 timing.
CS01 rights/provenance and PF01 exact-device budgets remain open; JC1060 hardware
is unavailable,silicon revision unobserved. Product PF04 remains NOT_RUN,H1 is
deferred. No music/labels/holdouts,codec,consumer/provider activation or release.
VERSION1.0.1,core/default/wire/consumer and HS01/02/03/04 stay unchanged.

Future feature: `experimental-compact-neural=[]`; no dependency is added and
no other experimental feature is enabled implicitly. The six planned source
paths in JSON are the only implementation paths. This checkpoint adds only
registration,public mathematical reference data/verifier and mutable handoff.
Registration commit/push precedes candidate code. Tests/lints and a separate
implementation receipt/commit/push precede clean measurement; result is another
audited commit/push. Do not implement or measure a candidate in this checkpoint.

## 2. Fixed parameters, layout and exact arithmetic

Profiles: `zero`,`asymmetric`,`gate-tagged`,`rounding`,each113 f32 values/452B,
four files/1808B total. The case book fixes row-major tensor shapes,offsets and
all raw bytes. `zero` is all+0. `asymmetric` uses
`f32((((n*7+5)%19)-9)/32)` in flat parameter order. `rounding` uses
`f32((((n*11+7)%23)-11)/10)`. `gate-tagged` starts with asymmetric values and
replaces the linear with a2x3 identity projection,both layers' gate tensors and
biases with the explicit formulas in scalar_reference.py,and a distinct head.
The raw parameter assets own exact identities; future Rust borrows these frozen
bytes and does not regenerate coefficients. Every parameter is finite with
absolute value<=2. No constructor accepts arbitrary weights/model/file/labels.

Parameter order: linear.weight[2,3],linear.bias[2],then for each layer0/1
weight_ih[8,2],weight_hh[8,2],bias_ih[8],bias_hh[8],then head.weight[3,2],
head.bias[3]. Offsets are0,6,8,24,40,48,56,72,88,96,104,110;end113.
Gate rows are `[i0,i1,f0,f1,g0,g1,o0,o1]`. State snapshot order is
`[h0[2],h1[2],c0[2],c1[2]]` (8 f32,32B). Every begin/new session starts all+0.
Layer1 consumes the **new** h0 for this row; each layer's recurrent dot and cell
update use its own entire **previous committed** h/c. No in-place overwrite of
one unit may affect the other unit's old state.

Let Q(v) be round-to-nearest,ties-to-even binary32. No FMA,reassociation,fast-math
or F64 dot reduction. For every dense output row,start a=+0,then for j ascending
do `p=Q(w[j]*x[j]);a=Q(a+p)`,and finally `y=Q(a+bias)`. This applies separately
to input and recurrent LSTM affine paths. Both biases are applied after their
own dot; combine `z=Q(ih+hh)`. Starting with bias or fusing both dots is different.

Sigmoid: promote the f32 argument to F64. For x>=0,use F64 e=exp(-x),
F64 `1/(1+e)`;for x<0,use F64 e=exp(x),F64 `e/(1+e)`;round once to f32.
Tanh: F64 tanh(promoted f32),round once to f32. Each F64 operation has normal
separate IEEE semantics. Gates i/f/o use sigmoid,g uses tanh. For unit j:

```text
new_c[j] = Q(Q(f[j]*old_c[j]) + Q(i[j]*g[j]))
new_h[j] = Q(o[j]*Q(tanh_F64(new_c[j])))
```

Head is the same ordered affine rule on new h1. Stable softmax: choose maximum
in class-index order,then `d[j]=Q(logit[j]-max)`,`e[j]=Q(exp_F64(d[j]))`.
Sum s starts+0 and uses `s=Q(s+e[j])` in ascending j. Output `p[j]=Q(e[j]/s)`.
No epsilon,clipping,renormalization,temperature or class permutation. Slots0/1/2
are synthetic output indices,not detected musical events or calibrated confidence.
Argmax is the first largest **rounded probability**,including ties. Equal logits
must give exactly Q(1/3) in all three slots. Valid probabilities are finite in
[0,1],with F64 sum within2e-6 of1.

Session input is finite3-f32 rows in[-4,4]. Bound proof using parameter<=2:
linear<=26;layer0 preactivation<=112 and subsequent layer<=12;|h|<=1,
|c|<=committed_rows (<=30000). Head logits<=6,subtract-max in[-12,0],
softmax s in[1,3]. These endpoints are exactly representable; monotone IEEE
rounding preserves these conservative bounds. Separate primitive probes can
use larger frozen values (including2^24 and activation/logit128). This does
not widen Session's input domain. Invalid/nonfinite caller rows are never
silently clamped or replaced.

## 3. Numerical oracles and gates

CS02 ordered f32 arithmetic and activation tolerance are unchanged. Linear and
the ih/hh/z intermediates for **fixed standalone probe inputs** must match
their exact frozen bits. Activation probes compare to F64 mathematical values
within `2e-6+2e-5*abs(reference)`,including original[-20,-2,-1,0,1,2,20]
points plus wider/near-zero/signed-zero checks. Cell state,probabilities and
composed sequence traces use that same absolute/relative bound per scalar
against the frozen normative scalar reference. This separately registers a
composed toy gate; it does not relax any existing primitive/FFT/HMM gate.
Primitive intermediate fields after nonlinear feedback use numeric comparison,
not a demand for cross-libm bit identity. Stable argmax and all geometry/state/
reset/cancel/status/work decisions are exact. Candidate bytes must be identical
across its own schedules. Reference digests identify the oracle; matching its
digest is not required where the numeric gate applies.

The scalar oracle has no work state machine,resource controller,model imports
or streaming buffers. The separate verifier imports neither it nor the generator
nor a candidate. It checks exact raw identities,coefficient cycles,closed-form
f32 nonassociativity/bias cases and independent100-digit Decimal calculations
for every primitive and all90 short sequence rows. Independently coded ordered
scalar calculations also verify every row/digest/anchor of all three30000-row
reference streams. These are mathematical registration checks,not a candidate
correctness/performance PASS. Future measurement imports the bound scalar oracle
only in its independent checking worker; the Rust candidate never sees expected
outputs,correctness flags,labels or reference-derived state.

Primitive denominator34:5 affine,16 activation pairs,8 softmax and5 seeded cell
cases. Cell records contain h[2],c[2],ih[8],hh[8],z[8],gates[8] (36 scalars).
Softmax records contain p[3],d[3],e[3],s[1] (10 scalars). Linear/activation records
each contain2 scalars. Seeded old h/c belong only to standalone cell probes;
Session always starts/reset to zero. All expected-bit arrays and layouts are
explicit in cases.json. Primitive `ProbeSession(index)` is fixed-input only,
has <=36-f32 result storage,<=4096B whole value and<=256 units/probe,with
quota/cancel/no-partial-result fences. It reuses the same scalar kernel helpers.
Its typed API is `new(index:usize) -> Result<ProbeSession,Error>`,
`step(quota,cancel) -> ProbeReport{units,status,error}` and
`result() -> Option<ProbeResult{len:u8,values:[f32;36]}>`. Result is None until
Done;unused slots are+0. Publication of the fixed result is one final bounded
unit. Query progress reports units and affine MACs. Invalid index fails creation;
quota0,pending cancel and terminal fences follow Session rules. No lookup of
expected outputs or whole-probe shortcut is allowed. All fixture probes together
perform190 affine MACs (five linear*6 plus five cells*32).

Twelve short sequences/90 rows cover zero/empty/nonzero inputs,asymmetric carry,
impulse,reset at midpoint/every row/last row,gate ordering,second-layer current
input and nondyadic rounding. Every short case runs4 input capacities1/2/7/64,
two output capacities1/7 and five quotas1/3/17/256/usize::MAX:40 schedules/case,
480 combinations/profile. Each primitive uses the same five quotas. The case
book binds28 finite raw assets/6848B,including empty arrays; no private data.

## 4. Future bounded API, state visibility and work

```text
Profile = Zero | Asymmetric | GateTagged | Rounding
Session::new(profile, expected_rows:u64) -> Result<Session,Error>
InputBlock { first_row:u64, rows:&[[f32;3]] }
step(quota,input,output:&mut[[f32;3]],cancel:&AtomicBool) -> StepReport
reset_state(cancel:&AtomicBool) -> ResetReport
finish_input(); cancel(); status(); phase(); progress(); state_snapshot()
StepReport { consumed_rows,produced_rows,first_output_row,units,flow,status,error }
ResetReport { applied:bool,units,status,error }
Progress { accepted_rows,committed_rows,macs,step_units,reset_units,total_units,
           reset_count,cancellation_phase }
Status = Running | Done | NoEvidence | Cancelled | Failed
Flow = Budget | NeedInput | NeedOutput | Terminal
```

Support0..30000 expected rows,0 diagnostic NoEvidence;larger is UnsupportedProfile.
No heap,I/O,callbacks or retained caller references in Session/ProbeSession.
Phases:AcceptRow,Linear,GateInput,GateBiasIh,GateHidden,GateBiasHh,GateMerge,
GateActivate,Cell,Hidden,NextLayer,Head,SoftmaxMax,SoftmaxSubtract,SoftmaxExp,
SoftmaxSum,SoftmaxDivide,Commit,Terminal. Counters remain global across resets.
No input/source timing or full-song geometry is inferred from toy row counts.

One unit is one ordered scalar MAC,bias/merge/max/subtract/sum/divide operation,
one scalar nonlinear call plus conversion,one cell update(two rounded products
and add),one hidden update(tanh/conversion/rounded product),one fixed layer
advance/copy of two values,one validate/copy of a complete3-value input,or one
atomic commit of8 state and3 output values (44B). Bounded initialization/index
changes fold into the associated unit; no hidden whole-row loop or allocation.
First terms start+0;there is no separate array-clear work to conceal. F64 library
calls are fixed scalar work and must have their actual host quantum recorded;
this is not an embedded worst-case guarantee.

Per row:Accept1,Linear8,two LSTM layers69 each,Head9,Softmax15,Commit1 =172.
Each LSTM layer:input MAC16,bias_ih8,hidden MAC16,bias_hh8,merge8,activate8,
cell2,hidden2,advance1. Affine MAC count is6+2*(16+16)+6=76;cell/hidden products
are charged separately in their units,not omitted from work. A successful reset is one
separate fixed32B zero-state unit; record step/reset/total units separately.
Max total units per measured case6000000. If the next charged unit would exceed
this counter,fail ArithmeticOverflow before that unit's writes. Partial state/gates/head/probabilities
stay private until Commit; snapshot always returns the last fully committed
state. Commit copies state and output as one bounded action. A request during
this fixed action is observed before the next unit; an emitted row remains
provisional even then. No product publication or accepted analysis slot exists.

Quota0 returns ZeroBudget with no change,then terminal fencing ignores buffers,
then pending cancellation precedes offset/count validation. Require exact
first_row=accepted_rows and offered count<=expected_rows-accepted_rows. No output
space means no acceptance or arithmetic. Input row acceptance validates all
three values before any consumption. One row may be staged beyond committed
count; all remaining input belongs to caller. NeedInput/NeedOutput can follow
earlier progress in the same call. Partial arithmetic survives starvation/quota
without reset/recompute. finish_input before all expected rows are accepted
fails TruncatedSource;at the exact count permits drain. Done requires accepted
and committed counts both equal expected. Earlier outputs remain provisional.

reset_state first fences terminal state (TerminalReset),then notices pending
cancellation (cancel + TerminalReset),then requires AcceptRow and accepted==
committed,otherwise BusyReset nonterminal and inert. At a legal boundary it
zeros only committed h/c,charges1 reset unit and preserves row counters. Caller
must cap offered input at the next registered reset boundary and drain staged
work before reset,even with a large quota/output buffer. Reset twice at one
boundary is two explicit resets if deliberately requested; tests cover it.
Clearing cancel or resetting cannot restart a terminal source; use a new Session.

Errors:UnsupportedProfile,SourceOffset,SourceLength,InvalidFeatures,
TruncatedSource,ArithmeticOverflow,NumericalFailure,ZeroBudget,BusyReset,
TerminalReset. Except the last three,errors terminally fail the active session;
constructor errors create none. Preserve prior progress and committed state.
NaN/Inf/>4/<-4 in a row after a prefix reports that prefix and fails before
consuming the bad row. Cancel records phase and keeps last committed state,
emits no partial row/result and never resumes;Done stays stable. No numerical
substitution,skip-row,probability repair or fabricated final certificate.

Fixed array payload64 f32/256B:input3,linear2,layer_input2,gate_ih8,gate_hh8,
preactivation8,gates8,committed h/c8,staged h/c8,logits3,exponents3,probabilities3.
Scalar/control/alignment are additional;whole Session<=4096B. Borrow immutable
profile bytes,never copy all parameters into Session. Static profile bank1808B
is separate;other public test assets and executable are host fixture overhead.
Long driver uses<=64 input rows768B,one output row12B and one state snapshot32B;
51B transport packing is separate. No whole-sequence input/state/probability
tensor is retained. Record size/alignment,actual capacities and lifetimes.

## 5. Future tests and exact five-case run

Tests first cover all probes/schedules/both references where applicable,all
sequence schedules and bit identity,old/current layer state and both biases,
fresh begin/reset equivalence,carry-vs-reset differences,canonical uniform output,
stable argmax and last-state snapshots. Explicitly pause/cancel inside each MAC
path,second layer,cell/head/softmax and before Commit;no state/output leakage.
Test quota0,empty buffers,source gaps/repeats,oversupply,short EOF,mid-row reset
rejection,legal repeated resets,pending cancel priority,invalid columns0/1/2
after a prefix,nonfinite failures,terminal fences,scratch reuse and output canaries.
Reject arbitrary CLI model/file/input/descriptor arguments before writing data.
New debug/release tests,fmt,focused Clippy(-D warnings),release CLI,Python compile
and checker-malformation tests are required. Controls:HS04's seven integration
plus internal test,HS03 integration tests,product grid and default context;known
C-oracle ignores remain NOT_RUN. No unrelated C/P4/full prior measurement matrix.

Order is **exactly five** measured cases:

1. `fixtures`:all34 primitive probes,then12 short sequences in JSON order.
2. `long-zero`:zero profile,30000 periodic17 rows,no reset.
3. `long-carry`:asymmetric profile,same rows,no reset.
4. `long-reset`:asymmetric profile,same rows,one reset before row15000.
5. `cancel`:long-carry source,separate requester10ms after repeated bounded
   processing starts;require<=50ms request-to-step-return,Cancelled,no final
   certificate,no resume. Completion before request is INVALID,not a reason
   to extend/repeat the workload. Record provisional prefixes/phase/scope end.

All three full cases execute every row and recurrent/softmax operation;no
zero-profile shortcut,repeated-row memoization or precomputed candidate states.
Independent Python worker computes references incrementally from the bound
public weights/formulas,compares each probability and all8 committed state
values,checks exact argmax,then discards the row. Full oracle digests/anchors
are frozen in cases.json;selected anchors are additional identities,not a
replacement for every-row checking. Input hash is u64le expected_rows then all
accepted3-f32 rows;trace hash is11-f32 rows (p3,state8). Reset schedule is bound
separately and must match counters. Cancel hashes/validates the accepted input
and committed output prefix only;staged/uncommitted state is never certified.

## 6. Transport, host caps and evidence

Fixed-case CLI only. Header8 bytes `HS05BIN1`. Little-endian records:

- Tag1:u16 primitive index0..33,u16 scalar count,then its expected-order f32
  result scalars (max36;linear2,activation2,softmax10,cell36).
- Tag2:u16 short-sequence index0..11 (full/cancel0),u32 row,then11 f32 values
  in trace order. Total51B. State snapshot is acquired after the complete row.
- Tag3:u32 JSON length<=16384,strict UTF-8 JSON trailer,immediate EOF.

Fixtures send probes then sequences/rows in order,including a NoEvidence metadata
observation for empty input. Full cases send30000 rows. Cancel sends only its
provisional prefix and trailer. Max transient bytes are8+149*primitive_count+
51*expected_row_count+5+16384 (conservative primitive-record bound). This is
1546397B for a full case and26053B for fixtures. No omitted rows are allowed.
The1MiB retained-output cap excludes transient pipe bytes;read one bounded record
at a time,no read-all/capture-output/tensor spool. Preserve exact header/trailer,
incremental digest/count,last complete record,one bounded failure witness and
raw stderr. Reject nonfinite,wrong tags/indices/order/count,duplicates,extra bytes,
truncation,identity/numeric/semantic mismatch;kill/wait child and keep partial
receipts on any failure. Record actual Session.step,pipe wait/write and checker
times separately. This is API isolation/supervision,not a filesystem sandbox.

Fixed host caps per case:CPU30s,wall45s,RSS64MiB per process,AS128MiB;
aggregate attempt retained artifacts1MiB. The workload is only5.16-million units
and690000 fixed scalar nonlinear calls/full case;registered reference math is
bounded and standalone Python baseline is around20MiB in the existing HS04
environment. These are host operational limits,not product budgets. Inherit
hard AS/CPU limits and also require worker SELF + waited CHILDREN CPU<=30s.
Waited-child HWM includes fork/exec;do not report it as isolated Rust memory.
Use unchanged neutral supervision/binding helpers. Before measurement refuse
wall0.05s vs1s sleep,RSS16MiB vs64MiB allocating child,output4096B vs65536B and
AS128MiB vs256MiB allocation. Confirm allocating-child refusal,group cleanup,
final supervisor status and aggregate artifact byte cap. No limit tuning.

Use new exclusive external output/build/binary. Bind clean implementation SHA,
six source components,protocol/case book/all28 assets/reference/verifier,frozen
authority files,actual compiler/Cargo binaries and feature,executable/linked
libraries,Python source/cache/interpreter/shared libs before any measured case.
Recheck bindings and loaded runtimes after every case,all worker runtime entries
must have been prebound,and clean source at finish. Record source/binary/toolchain/
input/output hashes,work/state/reset counts,cancellation,all resource receipts,
raw logs,partial failures and full artifact manifest.

PASS is **HS05_SYNTHETIC_HOST_PASS_ONLY** only if the complete registered set
passes all numeric/decision/state/scheduling/resource/identity/control gates.
Semantic disagreement is SYNTHETIC_FAIL;integrity/resource/incomplete is INVALID/
INCOMPLETE. Stop and preserve failures. No tolerance/parameter/backend/cap rescue,
subset or overwrite. A disclosed implementation repair requires a new clean
checkpoint before another attempt;no trained-model or real-data retry follows.

## 7. Ordered handoff for implementation

1. Add feature/module/bin gates only;load exact profile bytes,checked row geometry,
   fixed storage,counters and terminal/reset/EOF API. Keep other sources frozen.
2. Implement resumable scalar affine and ProbeSession. Pass all exact linear/
   bias-order probes,activation/softmax/cell oracles and quota/cancel fences.
3. Implement two-layer state staging/atomic Commit and all172-unit rows;pass
   12 short cases under480 schedules/profile,both biases and reset boundaries.
4. Complete error/backpressure/canary/reuse/cancellation/CLI-refusal tests and
   unchanged controls. Measure actual storage;no new dependencies/default change.
5. Add fixed binary CLI and independent incremental checker/neutral supervision;
   malformed-transport and cap refusal preflight,fmt/Clippy/build/Python checks;
   write implementation receipt,commit/push before long candidate measurement.
6. Execute all five fixed cases once from clean exact source,audit every artifact,
   write separate result/handoff and commit/push before any later stage.

Candidate code/tests/measurements,trained checkpoint parity,full-size graph,
musical/transfer/acceptance and physical P4 qualification remain NOT_RUN here.
