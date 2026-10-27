# HS-06: materialized probability / density / support bridge

Date: 2026-10-27. Branch `rust-rewrite`.
Baseline `406e4d4b13bc61dd338caf565fd17a0ec7063030`.
Status: **REGISTERED_NOT_IMPLEMENTED**.
[Machine registration](PRODUCT-HOST-PROBABILITY-PROTOCOL.json),
[public cases](fixtures/product-host-probability-hs06/cases.json),
[scalar arithmetic reference](fixtures/product-host-probability-hs06/scalar_reference.py),
[independent verifier](fixtures/product-host-probability-hs06/verify_registration.py).

## 1. Hypothesis, baseline and exclusions

A separate bounded host adapter can preserve named binary32 probabilities,
validate the stricter CS02/HS02 sum guard, materialize ordered binary64 log
densities and expose an immutable global support slice to the existing HMM.
Both meter scores, trace replay and HS02 must consume the same retained evidence.
The [readiness audit](PRODUCT-FULL-GRAPH-READINESS.md) identifies this missing
boundary. HS01-05 remain frozen controls; this is not a replacement decoder.

This checkpoint registers public arithmetic only. No Rust candidate, Cargo
feature, dependency, model, audio, labels, checkpoint execution, training,
provider/consumer activation, P4 port or release is added. Generic HS05 output
slots do not establish trained class semantics; this adapter explicitly names
its three inputs. VERSION remains1.0.1. Old rejected/spent musical evidence and
all earlier resource/numeric verdicts are preserved.

Registration commit/push precedes implementation. A separate implementation
receipt with tests/lints/preflight and commit/push precedes the four fixed clean
measured cases. A separate result audit/commit/push precedes any later scope.
PF04 remains NOT_RUN; exact-model rights/training provenance and PF01 exact-board
memory/placement/coexistence/timing remain open. JC1060 availability is the last
owner report, unavailable; assumed revision3.2 has not been observed. None of
this registration proves music accuracy or the full trained graph's feasibility.

## 2. Exact numeric and coordinate contract

Input row order is **`[p_beat,p_downbeat,p_nonbeat]`**, each binary32. Require
finite values in[0,1], and ascending binary64 sum
`((f64(beat)+f64(downbeat))+f64(nonbeat))` within **2^-21 inclusive** of1.
Do not use HS05's standalone2e-6 sum guard. No epsilon, clipping, renormalization,
fallback uniform row, skipped invalid row, temperature or probability repair.
All signed-zero input bits are retained. Negative zero is a valid zero.

Density order is **`[nonbeat,beat,downbeat]`**, each binary64:

```text
nonbeat: ln(f64(p_nonbeat) / 15.0)
beat:    ln(f64(p_beat))
downbeat:ln(f64(p_downbeat))
```

Division is a separate binary64 operation before log. Each zero, including -0,
maps exactly to negative infinity; no log of zero is called. Positive subnormal
binary32 values stay positive when promoted/divided. Every positive argument
produces finite density in[-1024,0], matching the existing decoder's domain.
Finite logs retain the CS02 **absolute4e-13** gate; -Inf is exact, never NaN/+Inf.
No f32 log, alternative base, fused conversion or reordered columns.

Geometry is source sample_rate44100 or48000, source frames N in[0,600*fs].
Checked integer R=ceil(50*N/fs), at most30000; reject unsupported rate/length or
arithmetic overflow before writes. N=0 is a diagnostic NoEvidence, not a product
track. A row r has global50Hz origin r; source endpoint N is exclusive and may
lie between row centers. No pad row or shortened source geometry is invented.

Support consists of the first and last rows where **promoted** beat>=0.05 or
downbeat>=0.05; expose `[first,last+1)`. The threshold is binary64 literal0.05,
not a downward-rounded f32 threshold. Internal unsupported rows remain in the
slice. Row0 and last rowR-1 are legal. No support yields NoEvidence and no HMM
invocation; it does not yield a fabricated density/empty grid. Keep full R-row
probabilities and full R-row densities even when decoding only the support slice.

## 3. Future API, storage and publication fences

Future feature `experimental-compact-probabilities` depends only on existing
`experimental-compact-grid` (which enables HMM); no new dependency/default change.
The six implementation paths are fixed in JSON.

```text
Geometry { sample_rate:u32, total_frames:u64 }
Session::new(geometry, probabilities:&mut[[f32;3]], densities:&mut[[f64;3]])
    -> Result<Session,Error>
InputBlock { first_row:u64, rows:&[[f32;3]] }
step(quota,input,cancel:&AtomicBool) -> StepReport
finish_input(); cancel(); status(); phase(); progress(); result()
PreparedView { geometry, probabilities:&[[f32;3]], densities:&[[f64;3]],
               support:Option<Support{first_row:u32,end_row:u32}> }
PreparedView::decoder_densities() -> Option<&[[f64;3]]>
StepReport { consumed_rows,committed_rows,first_committed_row,units,flow,status,error }
Progress { accepted_rows,committed_rows,units,cancellation_phase }
Status = Running | Ready | NoEvidence | Cancelled | Failed
Flow = Budget | NeedInput | Terminal
Phase = AcceptRow | NonBeatLog | BeatLog | DownbeatLog | CommitRow | Finalize | Terminal
```

Constructor checks both caller buffer capacities>=R before touching either.
Oversized tails/canaries remain unchanged. Session borrows both exclusive mutable
buffers for its lifetime, has no heap/I/O/callback/retained input-block reference,
and stages only one probability triple12B and density triple24B. Fixed payload36B;
whole Session<=512B including references/counters/alignment. Source chunks<=64rows
(768B) in the host driver; transport packing43B is separate. R=30000 requires
**360000B probabilities +720000B densities =1080000B**, not just one streamed row.
Driver control/input object, excluding separately allocated arrays/graphs/trace/
events, <=4096B. Record actual size/alignment, capacities and all live owners.

`result()` is None while Running/Failed/Cancelled. Ready and NoEvidence expose
only immutable R-row slices and final support, with the view lifetime tied to
`&self`, not an unconstrained underlying buffer lifetime. Empty NoEvidence has
two empty slices. NoEvidence with R>0 still retains validated probabilities and
densities; decoder_densities() is None. A Ready view returns exactly the support
density slice, with its global origin retained separately for HS02.

Private partial support/pending triples cannot leak through queries. Committed
buffer prefixes remain provisional until finalization. Dropping a failed/cancelled
Session returns caller access to uncertified buffers; it does not erase them or
certify a result. An instrumentation driver may hash that prefix after the drop
for failure receipts, never advertise it as a Ready view. No reset/resume API.

Keep Session/buffer owner alive and immutable for both Score passes, winning
Trace replay and HS02. The existing Decoder borrows materialized densities; do
not add lazy/recomputed density access or rerun the neural model during replay.
The returned trace path borrows the whole Workspace; retain it through HS02.
Keep full source probabilities for HS02, pass the exact global support range and
the trimmed path, then use existing native/wire validation for GridReady only.
Only a separately owned serialized result may survive reuse of all those owners.
This narrow host composition is not the M1 publication/bank/media-lease lifecycle.

The audited full30000-row trace scenario remains18848270B including the declared
PF01 overlap once,17949380B without it. These are conditional array totals, not
measured peaks or free P4 RAM. Additional model scratch, raw copies, allocator,
stacks, banks and platform residents remain unproved. Small composition allocates
one meter graph/workspace at a time: score3, drop; score4, drop; winner trace,
retain through grid. Record exact payload/capacities; no coexisting hidden caches.

## 4. Bounded work, failures, EOF and cancellation

One complete valid row uses five units: validate/copy3 inputs in AcceptRow; one
scalar log-or-zero unit in each NonBeatLog/BeatLog/DownbeatLog; atomic36B copy of
both triples plus support/counter update in CommitRow. Finalize is one fixed unit.
R>0 success uses5R+1 units (150001 maximum); R0 constructs terminal NoEvidence
with0 units. Each attempted charged unit, including an invalid AcceptRow, counts
before validation; reject a next unit beyond150001 before its writes. No hidden
whole-row arithmetic, output scan, allocation or uncharged log. Finalize does not
rescan buffers. Fixed library calls are host quanta, not an embedded WCET proof.

Priority: quota0 => inert ZeroBudget; stable terminal => ignore caller buffers;
pending cancellation => Cancelled; input offset/count => validate; then check
cancel before every charged unit. Require first_row==accepted_rows and offered
count<=R-accepted_rows. A partial row survives input starvation; return NeedInput
only when AcceptRow actually needs a row. Input already copied may be changed
by the caller without affecting staging. No output backpressure: output capacity
was checked up front. `first_committed_row` is the pre-call committed counter,
including calls producing0 rows. Never consume the first bad row.

finish_input with accepted<R terminally fails TruncatedSource, even mid-row;
with accepted==R it closes input and permits pending logs/commit/finalize. Full
input finalizes automatically without requiring a separate EOF call. Offset,
oversupply, unsupported profile/capacity, invalid probability, truncation,
numerical failure or arithmetic overflow fail explicitly; ZeroBudget alone is
nonterminal. Constructor failure writes nothing. step failures preserve the
already committed prefix but expose no final view. Terminal finish/cancel/step
stay stable; a zero quota still returns inert ZeroBudget first.

Cancellation between log stages or before CommitRow exposes no staged row.
A flag set during the fixed36B commit is observed before the next unit. A flag
set during Finalize may arrive after Ready is established; Ready stays stable.
The product publication fence is separate and not implemented here. Capture
the interrupted phase before changing to Terminal. Clearing a flag never resumes.

Registered timing case is **pending cancellation**, not an asynchronous live
compute measurement: process full-mixed until686 units (138 accepted,137 committed,
phase NonBeatLog), store the cancel flag, timestamp, call step and require
Cancelled/0 new units/no result/no resume and <=50ms store-to-return host latency.
No sleeps, requester race, extended/repeated workload or full result certificate.
This does not prove asynchronous response or the product50ms target on P4.

## 5. Oracles, exact denominators and small composition

Public assets:35 raw probability files/10536B;35 short cases/878 source rows,
850 rows in valid complete cases and seven invalid four-row cases with two-row
valid prefixes. Eleven selector cases are separate. All bytes are synthetic
arithmetic, including explicitly invalid NaN/Inf test encodings; none is audio,
neural output, a label, a trained parameter or a private source identity.

Scalar reference evaluates only public validation/log/support formulas. The
independent verifier imports neither generator/reference nor candidate: it uses
100-digit Decimal logs for every finite short result, checks exact class/zero/
sum/threshold/geometry/counter decisions, bit-coded independent full input cycles
and all60000 long reference rows/digests/anchors. -Inf is explicitly tagged in
JSON. Oracle digests identify reference bytes; finite candidate values use4e-13,
not cross-libm hash identity. All candidate schedule bytes must match each other.
Retained probability bits, statuses/support/coordinates/work/counters are exact.

Every short case runs input capacities1/2/7/64 and quotas1/2/5/31/usize::MAX:
20 schedules per case,700 per build. Invalid cases fail at row2 with units11,
accepted=committed=2; no density records/final result. Test every invalid column
and extra NaN/Inf/-Inf/domain permutations in the API failure matrix as well.
Long sources are fixed recipes full-mixed and full-no-support,30000 rows each,
150001 units each; no memoization/constant shortcut of candidate rows/logs.

Eight composition cases: two frozen HS02 base3/4 trains; two versions shifted by
two global rows; all-down8-row NoPath; one downbeat UnsupportedGrid with reason
InsufficientEvents; no-support and empty NoEvidence bypassing the HMM. Expected
base GridReady events/period/phase are the already frozen HS02 composition control,
not newly guessed full paths. Shifted support slices are exactly identical to
base slices: within the candidate their scores/path bytes must be identical,
and only global event/source anchor offsets change. This registers no new numeric
HMM oracle or tolerance. Existing HS01 numerical/tie/replay controls still apply.
Boolean reachability over the frozen graph independently proves all-down NoPath;
one-row downbeat chooses meter3 because each graph has a zero-log transition into
a downbeat state and frozen prior3>prior4. One event cannot meet2*meter+1.

Both meters always score the **same support density slice** from the same input,
even if the public fixture recipe uses a meter to author pulses. Do not call
the old meter-specific density generator independently per Score. choose_meter
accepts finite<=0 or -Inf scores; NaN/+Inf/positive is InvalidScore. Greater score
wins; finite equal scores (including signed zero) choose3; both -Inf yield NoPath.
Trace only a finite winner; compare trace score to its Score pass under existing
HS01 gate, compare prepared input/density hashes before/after every pass. Run
composition HMM quotas4096 andusize::MAX in tests; measured fixtures use4096.
All HMM passes use block256, the frozen HS02 composition setting.
The selector has11 fixed cases, including finite ties, both impossible and
invalid values. An artificial selector tie is not a claim of an actual trained tie.

Test full/error geometry, both capacities short, oversized canaries, alias-safe
immutable view lifetimes (compile-fail doc test), row gaps/repeats/oversupply,
empty input while staging, early EOF in every phase, exact full acceptance with
pending finalization, every-phase pending cancel, cancel vs malformed input,
quota0 vs cancel, stable terminal calls, no partial result/support, result/view
reuse and owned wire survival after every probability/density/workspace/event
owner is dropped/reused. HMM/HS02 UnsupportedGrid/NoPath stay explicit; never
construct a fake GridReady or run HS02 with N0.

## 6. Fixed run, transport, caps and integrity

Exactly four measured cases in order, once from a clean pushed implementation:

1. fixtures:35 cases,11 selectors and8 small compositions; bridge input cap7,
   quota31, HMM quota4096; full final-view records for850 valid rows only.
2. full-mixed:30000 rows, input cap64, quota31; Ready, full numeric comparison.
3. full-no-support:30000 rows, same schedule; NoEvidence, all rows checked,
   support=None, no decoder invocation.
4. cancel:full-mixed geometry/recipe, stop at the registered pending flag point;
   no row output/result certificate. Prefix hashes are uncertified instrumentation.

Fixed-case CLI only; refuse arbitrary file/model/geometry/descriptor arguments
before writing anything. Header `HS06BIN1` (8B). Little-endian records:

- Tag1:u16 case index,u32 global row,3*f32 retained inputs then3*f64 densities,
  total43B. Emit only through a Ready/NoEvidence immutable view. Invalid/cancelled
  sessions emit none. For full cases index0; short indices are case-book order.
- Tag2:u16 short case index,u16 event count,then count*8B existing Event layout.
  Exactly one record for each composition in book order,zero events when no
  GridReady view. Metadata still records status/reason/meter/scores/path hash.
- Tag3:u32 strict UTF-8 JSON length<=65536,metadata then immediate EOF. NaN/+Inf
  invalid-score test values are strings; legitimate -Inf scores are tagged strings.

Maximum transient fixtures bytes8+43*850+8*5+8*32+5+65536=102395;
full bytes8+43*30000+5+65536=1355549;cancel65549. Strict count/order/EOF,
retained input bits, finite/-Inf density domain, exact zero tags and every numeric
row must be checked incrementally by a separate Python reference worker. It may
import the frozen scalar oracle; Rust never reads expected values. No read-all,
raw full tensor spool or omitted rows. Preserve exact header/trailer,digests/count,
last complete record,one bounded failure witness and raw stderr. Record bridge
step,composition,pipe/checker time and pending cancel scope separately.

Per case caps:CPU30s,wall45s,RSS64MiB/process,AS128MiB; aggregate retained
attempt artifacts1MiB. Full bridge retains1.08MB outputs; smallR<=203 trimmed
compositions use sequential graphs/workspaces. Python checks stream rows and keep
only bounded records/reference caches; existing ~20MiB host worker baseline is
the planning basis. These are operational caps, not device budgets or measured
peaks. Inherit hard limits; require worker SELF+waited CHILDREN CPU<=30s. Waited
child HWM includes fork/exec and is not isolated Rust memory. Same old neutral
supervision/binding helpers remain unchanged. Preflight must refuse wall0.05s
vs1s sleeping child,RSS16MiB vs64MiB allocation,AS128MiB vs256MiB allocation,
output4096B vs65536B; verify group cleanup/final status and artifact cap.

Bind exclusive external output/build/binary,clean source SHA,six changed source
paths,protocol/cases/all35 assets/reference/verifier,frozen controls,actual compiler/
Cargo/feature/executable/linked libs and Python source/cache/interpreter/shared libs
before the first measured case. Recheck after every case and at clean-source end;
every loaded worker runtime entry must have a prior binding. Use established
receipts/manifests and preserve partial failures. No dependency installation.

PASS is only HS06_SYNTHETIC_HOST_PASS_ONLY if all registered numeric/decision/
composition/lifetime/schedule/cancel/resource/identity/control gates pass. Semantic
disagreement is SYNTHETIC_FAIL; integrity,resource or incomplete runs are INVALID/
INCOMPLETE. Stop/preserve; no cap/tolerance/backend/parameter rescue, subset or
overwrite. A disclosed implementation repair requires a new clean checkpoint.

## 7. Ordered implementation handoff

1. After this registration commit/push, add only the six planned paths: feature,
   module/bin gate, checked geometry/buffer constructor, one-row staging and API.
2. Implement five-unit rows, finalization, private support, exact validation/logs
   and immutable result view; pass all700 schedule cases in debug/release.
3. Add selector and eight small compositions using unchanged HMM/grid/native wire;
   prove identical meter inputs, shifted origin, replay immutability, owner lifetime,
   UnsupportedGrid/NoPath/NoEvidence and owned wire survival.
4. Complete failures/cancellation/EOF/canaries/CLI refusal,compile-fail lifetime
   tests,fmt,focused Clippy(-D warnings),release CLI,Python compile/checker malformed
   stream tests. Controls:HS05,HS04,HS03,HS02,HMM,product grid/default context;
   known C-oracle ignores remain NOT_RUN,not PASS. Run applicable targets once.
5. Add independent streaming checker/neutral supervisor,binding/resource refusal
   preflight,record actual layout/capacities. Write implementation receipt and
   commit/push before any long candidate measurement.
6. Execute all four fixed cases once,audit all receipts,write separate result/
   handoff,commit/push. Stop there; selecting/registering full-size neural scope
   still depends on the separate rights/provenance and device-resource gates.

At this registration: candidate implementation/tests/measurement,music,transfer,
fresh acceptance,consumer adoption and physical qualification are **NOT_RUN**.
