# Full-graph readiness audit and ordered continuation

Date: 2026-10-27. Source: `acdbadcdd5db57e3049e6a0decb32e30c396ae8b`,
branch `rust-rewrite`. [Machine evidence](PRODUCT-FULL-GRAPH-READINESS.json).
Status: **STATIC_AUDIT_COMPLETE; PRODUCT_NOT_READY**. No musical rejection,
new candidate execution,code change,model selection or physical measurement.

## Decision

Continue the native M1 direction with the existing host components and stable
core contracts. The next ready task is **registration of a narrow artificial
probability-to-density/support/ownership bridge (proposed HS-06)**. This closes
a concrete unimplemented join between completed components. Registration is
separate from implementation and does not authorize a trained checkpoint.

The full trained graph/native product is **NO_GO while its gates remain open**:
code/checkpoint rights,training provenance,actual-device resource allowances,
full-size numerical parity and musical acceptance are unresolved. This is a
readiness decision,not a new detector rejection. Do not enlarge the toy kernel,
change models,train,quantize,offload or activate a provider to bypass those gates.
Preserve all HS01-05 evidence,old rejections,VERSION1.0.1 and default behavior.

## 1. What the current code establishes

| Boundary | Implemented/evidence | Missing or intentionally different |
|---|---|---|
| Source PCM ->22050Hz | HS03 bounded FIR,44.1/48kHz,mono/stereo;full synthetic streams | Codec/media owner integration and whole-job EOF/error/cancel |
| PCM ->288 features | HS04 centered Hann/FFT1764/filter/log/difference | No feed into a complete neural graph;small HS03 compositions only |
| Neural arithmetic | HS05 exact scalar affine,both LSTM biases,gates,staged state,softmax and cancellation | Fixed3->2,two layers of2,head3,artificial113-parameter profiles;no Conv/pool or dimension150 graph |
| HMM | HS01 F64 sparse score/checkpoint/replay,full30000-row synthetic evidence | Input is already an immutable `&[[f64;3]]`;no product probability adapter/coordinator |
| Path ->events/grid | HS02 strict correction/global fit,meter,capacity/Q32 and native/wire mathematical fixtures | Its input still borrows full f32 probabilities and path;no trained producer |
| Result/core | Existing `no_std` builder,validation,wire,waveform,ownership infrastructure | No `rust/src/product_analysis` orchestration/required_storage/begin/finish implementation |
| M1 consumer | Current origin/main verified at4a8c660;adapter/source contracts exist | Core dependency remains25d09de;runtime experiments are not adopted,default provider/firmware not activated |
| Device/product | User targets5s first useful waveform/BPM,60s final,50ms cancel;600s maximum | Nine budget categories UNKNOWN;JC1060 last reported unavailable,revision unobserved |

HS01-05 live in the **desktop std runtime**,not the allocator-free `no_std`
core. HMM Workspace uses preallocated Vec owners;HS03/04/05 have fixed component
storage but still use host APIs/math. Allocation-free stepping is useful evidence,
not a target port,whole-image memory placement or P4 ABI/layout qualification.
Host helpers and synthetic binaries are not product owners.

The refreshed consumer sources are read with `git show` at the exact origin SHA,
not from the older local working checkout. The selected five earlier files still
match their frozen hashes. Cargo.toml independently confirms libapta core pin
`25d09de364e9507532c0508c838a8fecf0772d13`,with default-features=false.
No consumer branch,pin,provider or working file was changed. Its adapter README
states that conversion does not automatically publish `AnalysisPublication`.
Host integration,default activation and physical acceptance remain separate.

## 2. Exact full-graph gap

The pinned [main-branch source](https://github.com/mjhydri/BeatNet-Plus/blob/bb90eb0a9065b101a4b4c4cb2b2061950266cb4b/src/BeatNetPlus/model.py)
was inspected as text/AST,never imported. Exact Git blob374218b7bc7e242bd63d2df6ee7babf99175b9b2
and10375 bytes are verified. The trained binary was neither downloaded nor read.

| Stage | Shape | Parameters | Affine MACs/row |
|---|---|---:|---:|
| Conv1d |1->2,k10;288->279 per channel|22|5580|
| ReLU/pool/flatten |2x279 ->2x139 ->278,channel-major|0|0 (comparisons are separate work)|
| Projection |278->150,bias,no extra activation|41850|41700|
| LSTM |4 layers,input/hidden150,unidirectional,bias_ih and bias_hh|724800|720000|
| Head |150->3,bias|453|450|
| Softmax |3 classes|0|0 (scalar nonlinear/reduction work is separate)|
| Total |one branch,one row at a time|767125|767730|

Weights are3068500B f32;hidden+cell state is4800B before pending state or other
scratch. At30000 rows:23031900000 affine MACs. Even assigning the entire60s
deadline to these dots alone requires383865000 MAC/s. The scalar arithmetic
design additionally implies3003 nonlinear calls/row,90090000 total (sigmoid/tanh
and head exp);resampling,FFT,HMM,I/O,UI and elementwise/bias work remain separate.
These are DERIVED operation counts,not a runtime benchmark or P4 feasibility verdict.

The toy's76 affine MACs/row is smaller by a factor10101.71. Its measured0.044–
0.058s step totals must not be multiplied into a claimed full-model time;matrix
sizes,cache/lifetimes,backend and work scheduling differ. Nor may the separately
measured FIR/frontend/HMM cohorts be summed into a product latency claim.

Changing HS05's Profile does not change dimensions:its input/output arrays,
eight gate rows,two-unit loops,layer offsets and static113-parameter layout are
fixed. Feeding the first three frontend values,cropping/padding,or merely changing
constructor constants would not implement the target graph. Full-size kernels
require separately registered dimensions,oracles,bounds and cancellation parity.
Conv orientation,ReLU/signed-zero behavior,pool ties/odd tail and flatten order
must be fixed before that implementation;this audit creates no new kernel rule.

## 3. Probability boundary: the immediate integration task

The product specification names `[p_beat,p_down,p_non]`. HS05 slots0/1/2 are
only artificial outputs;no musical class binding or trained producer was tested.
HS01 graph classes index density order `[nonbeat,beat,downbeat]`. The required
adapter is therefore explicit:

```text
[p_beat,p_down,p_non] f32
  -> [ln(f64(p_non)/15),ln(f64(p_beat)),ln(f64(p_down))] F64
```

Zero maps to negative infinity;no epsilon,clipping or silent renormalization.
Finite values must remain in[0,1],with the receiving contract's probability sum
check. HS05's standalone sum bound2e-6 is looser than HS02/CS02's2^-21
(0.000000476837158203125). Passing the former does not imply the latter.
All90 frozen short toy reference rows happen to satisfy the stricter bound
(maximum error7.450580596923828e-8);this checks public math only,not a full-size
trained output. Test both sides of the stricter boundary explicitly in HS06.
Preserve the CS02 F64 log tolerance4e-13 and all existing decision gates.

The source row counts are compatible by the exact identity
`ceil(ceil(N*22050/fs)/441)=ceil(50*N/fs)`;16 boundary examples at both rates
were checked. Keep original source N/fs,row indices and support origin.
Threshold0.05 trims decoder support,not source duration. Row0 must count;
pass decoded first/exclusive end to HS02 without recentering or extending EOF.
No evidence/no path/invalid rows cannot become a fabricated complete grid.

## 4. Ownership and additional storage when composing current APIs

HS01 Decoder borrows an immutable full slice of F64 log-density triples. That
is **720000B** for30000 rows. HS02 still needs the original f32 probabilities
(360000B) for support and event correction;one does not replace the other.
Existing standalone HMM timings/payload explicitly excluded density ownership.
A lazy per-row log source could avoid that full allocation,but is not the current
API and would need a separate parity/ownership registration. Do not rewrite the
frozen decoder or assume that optimization in a product budget.

The path returned by HS01 is borrowed from Workspace. HS02's PathInput stores
that borrow. With these interfaces,using the path keeps the Workspace owner
alive,including checkpoints/parents. Logical completion does not free it.
Copying60000B to an independently owned path before release,or a split/reclaim
API,needs an explicit lifetime design and tests. No such release API exists now.

One declared composition scenario,using current host types/storage dimensions:

| Owner | Bytes | Assumption |
|---|---:|---|
| winner4 trace Workspace |16575840|includes60000B path;retained while borrowed |
| typed HMM graph vectors including derived classes |235304|length payload;allocator capacities/controls additional |
| postprocessor state map |33660|separate3-u8 map/state;not the derived class vector |
| f32 probabilities |360000|retained through correction |
| F64 densities |720000|materialized for current decoder API |
| event scratch |24576|3072 Events of8B,reserved for whole job in this scenario |
| PF01 existing overlap |898890|old declared overlap scenario,counted once |
| Sum |18848270|DERIVED described owners,not a measured or approved peak |

Without that PF01 overlap,the described owners total17949380B. Retaining model
weights during decoding would add at least3068500B,plus actual neural scratch/
controls. Raw asset residency/duplicates,multiple live graphs,Vec excess capacity,
allocator/alignment/fragmentation,stack/codec/UI/USB/audio/DMA and extra pending/
retained result banks are not proved by this sum. Flash/PSRAM/internal placement
is UNKNOWN. This scenario differs from the frozen CS02 reference scenarios
18107834/21583819B;those records remain unchanged and are not target budgets.
Documented board capacity is not free analysis RAM.

## 5. Rights, provenance and physical gates

A fresh public check confirms main still atbb90eb0a9065b101a4b4c4cb2b2061950266cb4b.
[Repository metadata](https://api.github.com/repos/mjhydri/BeatNet-Plus) still has
license=null;the full36-entry untruncated tree has no named license/notice/model-card
file. [License issue4](https://github.com/mjhydri/BeatNet-Plus/issues/4) is open
with two comments,both non-owner requests. Checkpoint blob identity is unchanged.
Availability issue1 (five comments) and annotation issue2 (two comments) were
also refreshed:owner replies establish availability/planned data,not artifact
terms or a checkpoint-specific training-membership record.
This adds a new observation;it does not modify the frozen2026-10-26 rights review.
No affirmative code/exact-checkpoint grant or checkpoint-specific training record
was found in this bounded refresh. Private grants may exist;this is not a legal
determination that no grant is possible. LICENSE_UNRESOLVED and
INDEPENDENCE_UNRESOLVED remain. No author was contacted and no external message
was sent. The [existing review](PRODUCT-COMPACT-RIGHTS-REVIEW.md) specifies the
exact digest-bound grant and training-membership evidence needed.

PF01's nine UNKNOWN budget categories remain unchanged:internal RAM,PSRAM,
model storage,stack/futures,decoder/frontend/trace,owned pending/retained copies,
codec/cache/USB/UI,cleanup/reclamation and fragmentation/safety reserve. Hardware
availability is the last owner report (unavailable),not a new device observation;
silicon3.2 remains unobserved. Only actual exact-board/image measurements can
close those budgets and the5s/60s/50ms targets. No serial port or revision was guessed.

The first M1 output scope is waveform,BPM,beatgrid and meter;key/calibrated
confidence remain later. Existing no_std waveform/tempo/result/wire infrastructure
should be reused. Stopped-deck media leases,first useful progress,MP3/FLAC decoded
PCM,final source-wide validation,old-result preservation,cancel publication fence
and actual provider/bank handoff are still product integration work. Full APTA
C ABI replacement/release and the first M1 vertical slice have distinct criteria.

## 6. Ordered plan for the next model

1. **Register HS06 only.** Create `PRODUCT-HOST-PROBABILITY-PROTOCOL.md/.json`
   and public mathematical fixtures before new code. Freeze named class order,
   2^-21 input validation,F64 log/zero rules,support origin/EOF,checked capacities,
   immutable probability/density/path owners,units,cancel and stop policy.
   Explicitly account for720000B density storage;use current materialized HMM
   API rather than silently adding lazy/recomputed densities.
2. **After that registration commit/push,implement the bounded adapter.** Use
   a separate opt-in host module;keep HS01-05/default/core/wire unchanged. Cases
   must include all three class impulses,zeros,all-zero invalid,NaN/Inf,out-of-range,
   sums between the two old bounds,row0 support,exclusive EOF,gaps/oversupply,
   empty/no-support,full30000 rows,quotas and cancellation. Fixed caps,identities
   and exact denominators belong in the registration,not guessed during a run.
3. **Close small artificial composition.** Feed the adapter into existing HMM
   scoring/trace and HS02 correction/native validation. Test meter-selection ties,
   shared identical input for both meters,replay immutability,global support offsets,
   unsupported/no-path outcomes and lifetime through final borrowed path. Retain
   the original probability arrays;never rerun the RNN for HMM replay. Freeze/
   push implementation before fixed measured cases;publish a separate result.
4. **In parallel,close external prerequisites.** Obtain digest-bound rights/
   provenance evidence through an explicitly authorized channel;measure exact
   JC1060 memory/placement/coexistence when available. Do not assign free RAM,
   infer revision,contact authors automatically or treat paper terms as a grant.
5. **Then select/register the full-size neural scope.** Include missing Conv/
   ReLU/pool/278->150/four-layer150/head,per-stage mathematical and composition
   oracles,complete resource/lifetime accounting and explicit checkpoint/runtime
   permission. No extra model,training or backend rescue is implicit. A toy pass
   cannot promote the trained graph without those gates.
6. **Follow the existing product plan.** Separately qualify full-source host
   behavior and permitted musical development/independent transfer before the
   large native port;then consumer pin/lifecycle and exact-image P4 qualification.
   WAV/PCM first,MP3/FLAC bridge before claiming full input scope. Keep fresh
   acceptance separate from spent corpora and preserve mandatory unknown outputs.

Done for this audit:coverage/lifetime/numerical gaps identified;counts independently
derived and checked against bound metadata;consumer/public-source refresh verified;
frozen sources/assets unchanged;next scope selected without candidate execution.
No Rust/C/P4 tests or performance runs were added for this documentation-only
checkpoint. Static source/hash/arithmetic/link checks are the applicable validation.

External evidence: `D:/AI/LIBAPTA/outputs/full-graph-readiness-20261027/`.
