# HS-04: bounded centered Hann / FFT1764 / log-band frontend

Date: 2026-10-26. Baseline `99f6ba6a5e57c5dc60eb536c699ec5d30623fbe5`,
branch `rust-rewrite`. [Machine registration](PRODUCT-HOST-FRONTEND-PROTOCOL.json),
[public cases/asset manifest](fixtures/product-host-frontend-hs04/cases.json),
[direct reference](fixtures/product-host-frontend-hs04/frontend_reference.py),
[independent registration checks](fixtures/product-host-frontend-hs04/verify_registration.py).
Status: **REGISTERED_NOT_IMPLEMENTED**. This checkpoint freezes public mathematical
oracles and a continuation plan; no candidate FFT/frontend implementation or run.

## 1. Scope, hypothesis and immutable boundaries

A bounded host frontend can preserve the specified centered frame geometry,
unscaled1764-point DFT,144-band log spectrum and144 positive differences without
retaining full-source PCM/features, while remaining invariant to streaming/work
fragmentation. The owner accepted the next separate frontend registration after
HS-03. This is an opt-in synthetic addendum, not full product PF-04 or model use.
No neural weights/inference/training, music/labels/real logits, formal holdouts,
compressed codecs, consumer/device/provider activation or release is included.
Neural rights/training independence and PF-01 actual-board budgets remain open;
JC1060P470C_I_W_Y is unavailable, silicon revision unobserved. VERSION1.0.1 and
core/default/wire/consumer/HS-01/02/03 files stay unchanged. H1 stays deferred.

Future runtime feature `experimental-compact-frontend` depends only on
`experimental-compact-resampler` for small composition checks. No dependency is
added. Six planned code paths are listed in JSON. Register/push now; future tests/
lints and implementation/push precede clean fixed measurement, then a separate
audited result/push checkpoint. This registration does not implement those stages.

## 2. Source evidence and amplitude domain

CS-01 pins BeatNet+ log_spect.py and madmom framing/STFT/spectrogram sources. Static
inspection confirms default origin0, end normal, no circular shift, symmetric
Hann, complex64 STFT storage, magnitude spectrum, filtered spectrum and
log10(1+x), then positive difference to the preceding row. No upstream package
was imported or executed. Five primary snapshots and their hashes are bound in
JSON. One old local log_spect.py snapshot had a single extra final LF: its
original2408-byte prefix matches the frozen blob/hash; it was preserved, and an
exact pinned GitHub blob was separately obtained and verified for this review.
The old manifest/specification was not rewritten and no weights were opened.

Normalized source PCM has |x|<=1, but HS-03 short outputs reached approximately
1.135 in magnitude. The strict frontend input envelope is therefore **|x|<=2**,
finite f32, with no gain/clipping/renormalization. The registration computes an
independent Decimal bound from every frozen normalized FIR phase: max L1 is
less than1.949. Let u=2^-53, gamma130=130*u/(1-130*u); bound the separate65-term
F64 products/sum and final f32 rounding by
`L1max*(1+gamma130)*(1+2^-24)+2^-149 < 2`. Stereo mean remains in[-1,1]. This
conservatively covers HS-03 outputs; it is a numeric bound, not measured device
headroom. Input magnitude>2 or nonfinite values are InvalidPcm before consumption.

The CS-02 primitive tolerances are **unchanged**: FFT complex error
`<=1e-4+2e-5*abs(reference)` and magnitude/filter/log/difference error
`<=2e-5+2e-5*abs(reference)`. Unit-amplitude tests retain the original domain;
separate amplitude2 probes test the wider domain against the same absolute and
relative limits. This registers a stronger new check; it does not retroactively
claim that the old[-1,1] qualification covered FIR ringing. Input2, Hann<=1 and
the unscaled1764 transform imply finite bounded magnitudes; the frozen filter
area bound and log10 place features below4. Verify the derived bounds in JSON,
but use the actual numeric oracle errors, not only a range check, for PASS.

## 3. Exact geometry, dtype and output order

Config identifies original fs44100/48000 and known N<=600*fs. Derived resampled
K=ceil(N*22050/fs), R=ceil(K/441)<=30000. N=0 is diagnostic NoEvidence and emits
no rows; product still requires N>0. Input is already mono22050Hz f32 samples,
exactly K. A source metadata mismatch/unknown length is not silently accepted.

For row r0..R-1, center c=441*r, window index n0..1763 reads x[c-882+n], with
+0 outside[0,K). Thus the first frame is[-882,882), the right end is exclusive,
and there is **no FFT circular shift**. At least min(K,c+882) samples must have
been consumed. The row source center is exactly `r*fs*2^32/50`; causal availability
is later. If q=min(K,c+882)>0, original required frames are
`min(N,center_HS03(q-1)+33)`; q=0 requires0. Exclude EOF, emit R rows, not padding
rows until a future frame is entirely outside the source. Time queries at r>=R
return None. Difference history persists across input blocks and budget pauses.

Use frozen CS-02 assets: Hann1764 F64, DFT roots1764x2 F64, filter edge bins146
u16, starts144 u16, offsets145 u32 and1745 f32 weights. Raw bytes/shape/hash/type
are exact and immutable. There are50476 static bytes; edge bins are audit geometry,
not an extra feature dimension. No implicit FFT-size substitution, crop/pad to a
network shape, Nyquist inclusion, regenerated twiddles or changed filterbank.

Pipeline for every complete frame:

1. Multiply f64(input f32) by frozen Hann F64 at its original window index.
2. Unscaled forward DFT length1764, negative imaginary exponent. Kernel works in
   F64 complex arithmetic, no FMA/reassociation. The output is in natural k order.
3. Keep bins0..881, omitting Nyquist k882. Round each real/imag component to f32,
   matching the pinned STFT storage boundary. Magnitude is
   `f32(hypot(f64(re32),f64(im32)))`. Do not use power or scale by FFT/window sum.
4. For each band b in frozen order, sum `f64(mag[start+j])*f64(weight[offset+j])`
   in increasing j with separate F64 multiply/add, then round once to f32.
   This defines a deterministic scalar reduction; it is not a bit-level claim
   about a particular BLAS reduction used by upstream np.dot.
5. `log_b=f32(log10(f64(f32(1.0+filtered32))))`. No ln/mel/alternate multiplier.
6. First row differences are exact+0. Later:
   `diff_b=max(+0,f32(log_b-current_previous_log_b))`, canonical+0 for equality.
7. Emit `[log0..log143,diff0..diff143]`, exactly288 f32 values. No transpose,
   padding, hidden row reset, nonfinite replacement or calibrated confidence.

The reference stores both this dtype-aware scalar output and an all-F64 ideal
log/difference output from direct source-index DFT sums. Candidate features must
meet the unchanged feature tolerance against **both**. Input/output geometry,
bin/band order, first differences and row ownership have no float tolerance.
Candidate output bytes must be identical across its own scheduling variants;
DFT-vs-FFT feature digests need not be identical because the numeric gate is
tolerance-based. Hash both references and every candidate output for identity.

## 4. Fixed FFT implementation plan

No new library/backend dependency. Use the factorization1764=2*2*3*3*7*7 and
an explicit bounded DFS frame stack (depth7), not an uninterruptible recursive
whole-frame call. For transform length n and first radix p in that sequence,
M=n/p, child a0..p-1 transforms input positions start+a*stride+p*stride*t,
storing its M natural outputs at base+a*M. A leaf reads the original immutable
window/ring sample and applies its own Hann factor; it does not read an already
overwritten FFT slot. Primitive raw-transform tests use their borrowed fixed
input array instead. One1764-complex F64 work array is sufficient.

For b0..M-1, load all p child values into a seven-complex temporary before any
overwrite. For c0..p-1 compute, in increasing a:

```text
k=b+M*c
out[c] = sum_a child[a] * roots[((1764/n)*a*k)%1764]
```

Use separate real/imag products, subtraction/addition and accumulator additions,
in the written order. Stage all p outputs in a second seven-complex temporary,
then store them into base+c*M+b. Advance b only after the complete butterfly.
Each complex MAC is one bounded unit; each load/store/stack action is separately
resumable. Leaf assignment is bounded and includes window multiplication. The
exact complex-MAC count is1764*(2+2+3+3+7+7)=42336 per row, including full-spectrum
computation before the public882-bin projection. Natural-order composition and
prime7 phases must be verified against the direct/analytic oracles before use.

This fixes one initial scalar backend. Optimized/FMA/SIMD/library transforms are
separate parity work after HS-04, not an automatic rescue if this one fails caps.

## 5. Storage, streaming and scheduling contract

Future safe typed API:

```text
Config { source_rate:u32, source_frames:u64 }
InputBlock { first_sample:u64, samples:&[f32] }
Session::new(config) -> Result<Session,Error>
step(quota,input,output:&mut[[f32;288]],cancel:&AtomicBool) -> StepReport
finish_input(); cancel(); status(); phase(); progress(); row_time(r)
StepReport { consumed_samples,produced_rows,first_row,units,flow,status,error }
Status = Running | Done | NoEvidence | Cancelled | Failed
Flow = Budget | NeedInput | NeedOutput | Terminal
Phase = Fill | Transform | Magnitudes | Filter | Log | Difference |
        CommitHistory | Emit | Terminal
```

Fixed owned payload:1764-f32 input ring7056B;1764-F64-complex FFT array28224B;
882-f32 magnitude3528B;144-f32 prior log576B;288-f32 staging1152B; two seven-F64-
complex butterfly temporaries224B. Total40760B plus seven stack frames/scalar
state/alignment. Bound `size_of(Session)<=49152`; validate/record actual size,
alignment and constituent capacities. No second full transform/window copy or
per-row queue is authorized. Tables are borrowed immutable static bytes, never
copied into Session. Separate host harness buffers:<=512 input samples2048B and
one output row1152B for long runs. No full-source PCM/mel/spectrum/features retained.

No heap allocation, I/O, callbacks or retained caller slices in Session constructor/
step/cancel/query/finish. Read only up to min(K,c+882) for the current frame;
the1764-sample ring retains all needed real samples while FFT/history work runs.
Input is validated when ingested, including future context. A single work unit
is one input sample, stack/FFT leaf/load/store/complex MAC, magnitude bin, filter
term, log/diff/history cell or phase action. Emit is an explicitly bounded atomic
copy of one already complete288-value row (1152B), not a song-sized operation.
No partial row becomes visible after budget/cancel/error. Check AtomicBool before
every unit, including Emit; a request during the fixed copy is serviced before
the next unit. Max units per case4,000,000,000; record counts and max-step time.

Quota0 returns ZeroBudget without change; then terminal fences ignore buffers;
then pending cancellation precedes input checks; then require exact first_sample,
offered count<=K-consumed. No output space means no ingestion/FFT work. NeedInput
only when another real sample is needed; empty/exhausted output gives NeedOutput,
and either flow may include previously completed progress in that call. Preserve
the unconsumed input tail. Never reset prior logs during temporary starvation.

Errors: UnsupportedProfile, SourceOffset, SourceLength, InvalidPcm,
TruncatedSource, ArithmeticOverflow, ZeroBudget. All except ZeroBudget terminally
fail the session, with prior consumed/emitted progress reported. finish_input
before K consumed fails TruncatedSource; at K succeeds and allows drain with
empty input. Known EOF supplies padding only, not invented missing samples.
Done requires K consumed and R emitted. cancel changes Running only, records
where noticed, never resumes after flag clearing; Done stays stable. Fresh source
requires a new session, with first-row differences reset. Previously streamed rows
are provisional work, never a newly accepted product analysis. No product slot,
wire schema or source codec is touched.

## 6. Public oracles and future preflight

The case JSON fixes primitive FFT probes (delta0,constant,cos-bin7,delta3 and
Nyquist alternating, each amplitude1 and2), short streams and four full streams.
FFT analytic outputs include all1764 bins for primitive tests, so the excluded
Nyquist is independently observable. Frontend rows expose only882 magnitudes.
Short inputs cover zero length,1/440/441/442 samples,882/1764 support boundaries,
positive/negative DC2, impulses at sample0/882/last, nonstationary step2, periodic257,
cosine7 and direct small resampler compositions at both rates/channels. Original
source metadata and row-time/availability records are frozen separately from
input sample count. Composition uses HS-03 unchanged on public321-frame periodic
source; it is generated math, not a trained model frontend parity claim.

All new raw golden arrays are little endian with manifest hash/shape/dtype/offsets.
Short input PCM, dtype-aware features and ideal-F64 features are public artificial
math fixtures, no music. The direct oracle uses frozen roots and source-index
math.fsum, no butterfly/streaming implementation. Keep the references and generator
separate from candidate code; no NumPy/madmom/torch install/import. Verify the
analytic probes against direct DFT and all unit/amplitude2 error limits before
freezing. Static registration checks are not candidate correctness or performance.

Future tests: every short case with input chunks1/7/256/512, output capacities1/7
and quotas1/7/256/4096/usize::MAX (40 schedules/case). Check bit-identical candidate
features/status/times under every schedule and finite feature envelope; compare
each scalar to both references. Check intermediate Hann, natural FFT/magnitude,
band/log/order, exact first-row zeros and history across starvation. Cancel at
every reachable phase, including inside radix7 and a partially accumulated filter;
no row publication, no resume, scratch reuse/canaries and stable Done. Invalid
NaN/Inf/>2/<-2 after a published provisional prefix must report prior progress and
fail without substituting zeros. Test gaps/repeats, oversupply, short EOF, geometry
limits, empty input/output, quota0 and first/exclusive-end ownership. Compare last
fractional original source position/availability and N/fs/K/R identities exactly.

Controls: HS-03 tests (including6120 schedules/profile), existing product-grid
geometry and default runtime context. Preserve known C-oracle-dependent ignores
as NOT_RUN without their real oracle. Run new debug/release, fmt, focused Clippy
(-D warnings), release CLI and Python parse/compile. No unrelated C/P4 matrix.

## 7. Fixed measured cases and reference compression

Order: fixtures, fullzero-44100, fulldc2-44100, fullpulse441-48000,
fullalternating2-48000, cancel. Each full case describes a600s source, K13230000,
R30000 and42336*30000=1270080000 complex MACs. Full input is generated already-
resampled mono data under the envelope, not a new full resampler run. Do not
short-circuit FFT computation for zero/constant/periodic cases. Helpers may reuse
reference equivalence classes, but the candidate executes every prescribed row.

For hop-aligned zero/DC/pulse441 streams, interior frames repeat exactly; for
alternating2, successive interior frames negate and magnitudes/logs are equal.
Use the first two, representative complete interior(s), and last two frames as
reference classes, preserving actual predecessor log differences at transitions.
The JSON freezes class map and representative row indices. Prove compressed row
features agree with direct short complete streams for each signal before freeze.
The full checker compares every emitted scalar against its class reference and
checks all R, rather than evaluating only sampled rows. Raw feature output stays
external as an incremental digest, not a full-source stored tensor. Reference
feature digests identify the oracle; candidate digests identify its tolerated output.

### Bounded candidate-to-checker transport

Use a binary stdout pipe, consumed incrementally by the independent Python worker;
stderr remains a raw exclusive log. Session itself has no I/O. The CLI accepts
only the fixed case name; no file/descriptor/input-data argument is added. Header
is exactly eight ASCII bytes `HS04BIN1`. All following integers are little endian:

- Tag1: u16 primitive-case index0..9,u16 count1764,then1764 complex(re,im) F64 pairs.
- Tag2: u16 stream-case index,u32 row,then288 f32 features. In fixtures, stream
  indices0..19 follow the JSON short-case order; full/cancel uses index0.
- Tag3: u32 UTF-8 JSON byte length<=16384,then one strict final metadata object.
  Require EOF immediately after this trailer; no duplicate trailer/extra bytes.

Fixtures emit all ten primitive records, then every short-case row in source
order, then trailer (including empty NoEvidence cases). Full runs emit all30000
rows then trailer; cancel emits only the provisional prefix and a Cancelled/no-
final-certificate trailer. The checker compares each received scalar to both
bound oracles immediately, updates candidate feature/transport digests and discards
the row. It preserves header,exact trailer bytes,transport digest/byte count,
last complete record/ordinal and any bounded failing-record evidence, not the
entire transient stdout. This is the explicit stdout preservation representation;
retaining the full34.56MB feature tensor would violate this experiment's storage
scope. Raw stderr and worker/supervisor stdout/stderr logs are preserved normally.

One read record is bounded by28229B (primitive) or1159B (row). Reject wrong case,
tag,count/order,nonfinite data,truncation/extra records or numeric mismatch, retain
partial receipt and kill/wait the child. No capture_output/read-all accumulation.
Maximum transient stdout bytes are8+28229*primitive_record_count+
1159*expected_feature_row_count+5+16384; cancel's row cap is the full source count.
The aggregate1MiB output cap applies to retained artifacts, including these
receipts, not to the bounded transient pipe traffic. Record pipe/checker overhead
separately from Session.step timing; pipe backpressure is not free compute time.

Cancel uses fullpulse441-48000, separate requester10ms after starting repeated
bounded calls; require request-to-step-return<=50ms, terminal Cancelled/no final
certificate/no resume. Report phase, provisional counts and scope/drop separately.
No claim of P4 first5s/final60s/cancel50ms or exact-device cleanup follows.

Known live frontend payload is about41KiB plus bounded stack/control/alignment;
50476B tables and3200B long-driver input/output buffers are separate. Host Python
baseline is about19MiB and direct-class oracle storage is bounded by16 frames,
not full source. Fixed host caps per case: CPU120s,wall180s,RSS64MiB,AS128MiB,
aggregate attempt output1MiB. They permit the registered1.27-billion complex-MAC
workload plus independent scalar comparison, not full features/PCM retention.
These are operational caps, not approved target RAM/PSRAM budgets.
CPU120s is inherited as a hard limit by worker and child; additionally record and
reject if worker SELF CPU plus waited CHILDREN CPU exceeds120s for the case.
This combined gate includes pipe/oracle checking. RSS64MiB is a per-process HWM
gate (worker,child and monitored supervisor), not a sum or device RAM allowance.
Wall180s covers the supervised case including transport/checking and child wait.

Reuse unchanged neutral supervision/binding helpers. Refuse fixed wall0.05s vs1s
sleep,RSS16MiB vs64MiB touched child allocation,4096B vs65536B output and AS128MiB
vs256MiB allocation; verify refusal on allocating child. Bind clean implementation,
protocol/fixtures/all raw assets/reference, six source components, frozen authority
files, actual Rust/Cargo binaries/features, executable and loaded source/cache/
interpreter/shared libraries before cases; recheck after each and source at finish.
New exclusive external output/build/binary, preserved stdout receipts/raw stderr
and partial files as specified above,
child HWM and inherited rlimits, process-group cleanup, final supervisor status
and aggregate bytes are required. CLI accepts fixed case names only. This is
API/input isolation and supervision, not an OS filesystem sandbox.

PASS is **HS04_SYNTHETIC_HOST_PASS_ONLY** only when the complete registered set
passes numeric/geometry/status/quota/cancel/no-regression/resource/identity gates.
Semantic mismatch is synthetic FAIL; integrity/resource/incomplete is INVALID/
INCOMPLETE. Stop and preserve every failure; no filter/backend/tolerance/cap rescue,
subset or overwrite. An implementation bug repair needs a disclosed new clean
checkpoint before another run; no musical/real-data retry is authorized.

## 8. Ordered implementation handoff

1. Add only the registered feature/module/bin gate and checked geometry/static
   asset loaders. Establish exact row_time, ring retention and source EOF tests.
2. Implement explicit-stack F64 mixed-radix kernel, verify all primitive FFT
   probes/all bins and bounded work interruptions before integrating log features.
3. Implement magnitude/bin projection, filter/log/difference/history and atomic
   row emission; pass both scalar references and all40 scheduling variants.
4. Complete cancellation/error/backpressure/reuse/canary and two-rate/channel
   small resampler composition checks. Keep HS-03 and all core sources immutable.
5. Add fixed synthetic CLI and strict independent supervised checker; finish
   preflight/controls/lints/build, write implementation receipt, commit/push.
6. Run the seven fixed measured cases from clean exact source, audit all artifacts,
   publish separate aggregate result/handoff and commit/push before another stage.

This checkpoint is registration only. Candidate implementation/tests/measurement,
whole neural/native pipeline, musical/transfer/acceptance and physical P4 evidence
remain NOT_RUN. Model rights/provenance and board budgets are not bypassed.
