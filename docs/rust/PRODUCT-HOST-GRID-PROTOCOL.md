# HS-02: synthetic decoded path to events and whole-source grid

Date: 2026-10-26. Source baseline:
`701c44222e6304128b23a1c1c6728248a4cc75f7` on `rust-rewrite`.
[Machine registration](PRODUCT-HOST-GRID-PROTOCOL.json),
[public cases](fixtures/product-host-grid-hs02/cases.json).
Status: **REGISTERED_NOT_IMPLEMENTED**. This checkpoint contains specification,
public mathematical fixtures and registration checks, not candidate execution.

## 1. Hypothesis and authorized boundary

The frozen CS-01 event correction and strict single-grid bridge can be implemented
as a deterministic bounded host postprocessor over artificial decoded paths and
probabilities. It must preserve global row coordinates, cyclic meter phase and
all owned Q32 points, and reject unsupported full-source geometry without hiding
events or publishing partial mandatory results. HS-01 proves the separate HMM;
this experiment addresses the missing postprocessing boundary.

The owner accepted public rights/provenance inspection and a separate synthetic
registration. This is an addendum permitting only this opt-in host component,
not completion of product PF-04. The [rights review](PRODUCT-COMPACT-RIGHTS-REVIEW.md)
leaves LICENSE_UNRESOLVED and INDEPENDENCE_UNRESOLVED. PF-01 remains
NEEDS_MEASUREMENT, board unavailable. No neural weights, real logits, music,
labels, formal holdouts, training, model selection, device port, consumer pin,
default algorithm, wire schema or VERSION change is included. H1 stays deferred.

Use new runtime feature `experimental-compact-grid = ["experimental-compact-hmm"]`;
the existing HMM implementation remains byte-identical. New module `compact_grid`
and binary `apta-compact-grid-synthetic` require the new feature. No dependency is
added. Six planned code paths are listed in JSON. Do not add a product feature or
modify core validators to make a synthetic result pass. Registration/push precedes
implementation; focused tests/lints and implementation/push precede measurement;
the clean fixed run is followed by a separate result/push checkpoint.

## 2. Public interfaces and storage

Use safe Rust and typed borrowed inputs, no I/O in the postprocessor:

```text
Geometry { sample_rate: u32, total_frames: u64 }
PathInput { geometry, meter: u8, probabilities: &[[f32;3]],
            decoded_first_row: u32, decoded_end_row: u32, states: &[u16] }
Event { global_row: u32, beat_number: u8, winning_class: u8, reserved: [u8;2] }
Mode = EventsOnly | Grid
Session::from_path(input, mode, caller_event_storage) -> Result<Session, Error>
Session::from_events(geometry, meter, events) -> Result<Session, Error>
step(positive_units, &AtomicBool) -> Result<StepReport, Error>
cancel(); status(); phase(); result()
```

`Event` is repr(C), exactly 8 bytes; class0=beat, class1=downbeat, reserved bytes0.
Event digest encoding is u32 LE row, one byte number, one byte class, two zero
bytes. This private experiment encoding is not a new product wire format.
`from_events` is a separate pure fitter entry for public geometry oracles; it must
not be a bypass into product publication. It borrows events without copying.

Caller-owned event capacity is exactly 3072 entries (24576 bytes); smaller storage
returns Capacity before writing. No heap allocation in Session construction,
step, cancel or result. Generated probabilities (at most360000 B), u16 path
(60000 B), and fixture/serialization owners are allocated before stepping by the
host harness. Frozen graph/maps are borrowed immutable assets. No new HMM
workspace, song-sized parent table, per-row evidence copy or output bank is added.
Record actual sizes/capacities and lifetimes; this is not a physical RAM budget.

Allowed geometry is fs44100 or48000 and 0<N<=600*fs. Other rate/duration returns
UnsupportedProfile; N=0 is InvalidInput. `R=ceil(50*N/fs)` with checked integers;
probability length must equal R, R<=30000. An array/path/event length over its
registered capacity is Capacity before iteration; other shape/span mismatches
are InvalidInput. Meter outside3/4 is UnsupportedProfile. Source channels are
not an input to this already-downmixed postprocessor.

## 3. Probability/support and path validation

Constructor checks only fixed-size geometry, lengths, capacities and span bounds.
Full data checks happen incrementally. Validate **every** source row, including
outside decoded support: all f32 values finite, in[0,1], and
`abs(((f64(p_b)+f64(p_d))+f64(p_n))-1)<=2^-21`. No renormalization, clipping,
epsilon addition or replacement of p_n by subtraction. Zero triple is invalid.

During that scan find first and last rows where `f64(p_b)>=0.05` or
`f64(p_d)>=0.05`. The RHS is F64 0.05; f32-at-0.05 and its predecessor have the
CS-02 frozen decisions. Row0 alone is evidence. No qualifying row requires empty
path with span[0,0] and returns NoEvidence; a nonempty/inconsistent span is invalid.
Otherwise require decoded span exactly[first,last+1), path length equal to span
length, and restore this original offset when constructing events. Threshold trim
never changes N or the declared source origin. A missing path is not a fabricated
NoEvidence result.

State IDs refer to the frozen selected-meter graph. Map tuple is
`(beat_index, interval, q)` from the CS-02 map. State order is b, then i10..75,
then q0..i-1; `state_id=b*2805+(i-10)*(i+9)/2+q`. Reject sentinel65535 and any
out-of-range state. For each adjacent path pair require the previous ID in the
current state's frozen sorted incoming list. Use resumable binary search
(at most7 comparisons per transition); do not recompute exp/log probabilities.
The first state can be any valid state under the uniform prior. This verifies a
legal supplied path, not that Viterbi or a neural model inferred it correctly.

## 4. Event correction

Beat-range membership is the exact integer predicate `16*q<interval`. Find
maximal connected true regions; do not split a region by class or beat number.
Include a region already active at the first decoded row and flush a region still
active at the last row. For every region choose the largest activation among
the two columns p_b,p_d over its rows. Ordering is value descending, global row
ascending, class0 before class1. Update only on strictly greater values while
visiting row then class. This matches the fixed flattened-argmax tie rule.

Output event row is the selected global row; beat_number is the mapped path
beat_index+1 **at that row**, not a two-class downbeat/beat flag and not a label
inferred from the winning activation column. Ignore p_n for peak selection.
Refuse the3073rd event with Capacity; do not truncate or publish a prefix. No
beat-range regions gives NoEvidence. EventsOnly returns a typed diagnostic event
view only when extraction completes; it cannot represent a mandatory product FINAL.

## 5. Exact event-to-grid arithmetic

Validate the entire supplied event list before fitting: row in[0,R), strictly
increasing, number1..m, class0/1 and zero reserved bytes. Malformed values/order
are InvalidInput. Empty returns NoEvidence. For a nonempty list require
`2*m+1<=L<=3072`; insufficient count is UnsupportedGrid/InsufficientEvents.
Require `number[j+1]=number[j]%m+1`; otherwise UnsupportedGrid/PhaseDiscontinuity.
No sorting, dropping, deduplication, interpolation or outlier removal is allowed.

Use checked i128/u128 arithmetic. Define Q=2^32 and j0..L-1:

```text
D = L*sum(j*j) - sum(j)^2
U = L*sum(j*r_j) - sum(j)*sum(r_j)
T = RHU(3000000*D / U)                  # millibpm
P = floor(fs*60000*Q / T)               # Q32 source frames
A = RHU((2*fs*Q*sum(r_j) - 50*L*(L-1)*P) / (100*L))
```

Require D>0,U>0 and T in[40000,300000]. Tempo outside profile is UnsupportedGrid/
TempoOutOfProfile. RHU is exact `floor(n/d+1/2)` for signed n and positive d:
`(2*n+d).div_euclid(2*d)`, all operations checked. Never use Rust signed `/` as
a substitute for floor on a negative numerator. The raw event location
`q_j=r_j*fs*Q/50` is a rational; do **not** floor each q_j before computing A.
This formula is the existing CS-01 bridge written without ambiguous intermediate
rounding. Arithmetic overflow or zero denominator is ArithmeticOverflow/Failed.

In this fixed order require:

1. `0<=A<P` (otherwise UnsupportedGrid/AnchorOutOfRange).
2. `A+(L-1)*P < N*Q <= A+L*P` (UnsupportedGrid/IncompleteCoverage).
3. For every j, `10*abs(r_j*fs*Q-50*(A+j*P))<=50*P`
   (UnsupportedGrid/ResidualExceeded).
4. Let d be the earliest j with number1. Check all phases with
   `(j-d).rem_euclid(m)` and derive witness `(A+d*P)//Q`.

The inequalities own[0,N) exactly once and exclude EOF. There is one regular
MODEL segment, not a piecewise fit or a return to a rejected decoder. The0.10-beat
residual is evidence support, not a ground-truth quality score. A regular but
musically wrong path may pass; musical evaluation stays outside HS-02.

## 6. Result and existing native/wire checks

Successful summary contains source N/fs, T, A/P Q32, L, meter m/4, earliest
downbeat ordinal d and its whole frame. Grid segment[0,N), b0=0, beat_count=L,
segment_id=revision=1, flags0, FeatureState::Final, confidence0. Use Segments
representation with one segment, no explicit beat array and one[0,N) coverage
range. All probability/evidence trim remains recorded separately; evidence frame
range is outward-rounded `[floor(first*fs/50),min(N,ceil(end*fs/50)))`.
For direct events, first/end are first event row/last event row+1.
MODEL describes this regular segment; it is not a new enum or wire flag.

Map the summary into the existing public GridSegment/GlobalGrid, TempoView,
Meter/MeterSegment and revision types. Grid requested/applicability/coverage and
meter applicability are[0,N). Use the evidence frame range above for grid/TEMP
evidence. Meter denominator4, numerator m, witness d/frame exact; never reset d to0.
All confidence fields0, calibrated quality absent, key absent. TEMP wire candidate
array contains the selected tempo once (score/confidence/relation/flags0).
Revision1, previous0, Applied, affected[0,N), proposed one segment/zero explicit
beats. Confidence0 here is an uncalibrated numeric field, not a safety PASS.

Preflight must call unchanged `grid::validate_external`, native validation and
`builder::finalize`/`result::parse` for every positive small fixture. Native test
provenance is ExternalImport, source_name HS02-synthetic, version1 and default
completed NativeResultInfo; do not impersonate a native analysis session. The
wire test supplies a clearly artificial one-column overview/spans covering[0,N)
with min=max0, column flags1 and other column fields0. Source is mono/layout1,
fingerprint_kind0 and all-zero fingerprint. Overview level_id/origin0,
frames_per_column=N, logical count1; one span[0,N) with first index/data offset0
and column_count1. It is serialization scaffolding, not waveform analysis. Bound wire
buffer is4096 bytes; decode and compare all summary/tempo/meter/revision fields,
Q32 fractions and exact witness. If existing validation refuses a fixture, stop
and investigate; do not weaken core validation or edit PF-02. Require independent
bytes after input scratch mutation and no result after any failed mandatory stage.

## 7. Scheduling, statuses and cancellation

Phases: ValidateProbabilitiesAndSupport, ValidatePath, ExtractEvents,
ValidateEvents, AccumulateFit, ComputeModel, CheckCoverage, CheckResiduals,
BuildSummary, Terminal. from_events starts at ValidateEvents. Phases specific to
one mode are skipped explicitly. Each unit processes one probability row, one
state/search comparison, one region-row peak decision, one event, one fixed-size
arithmetic/phase transition, or the final fixed-size summary. Graph validation
is not an uninterruptible per-song constructor operation. max total units per
case is4194304; report count, quota and max-step observations. No hidden full-row
or full-song loop in a unit; integration validation/serialization is separate.

Check AtomicBool before each unit. Quota0 is ZeroBudget without advancing.
Statuses: Running, EventsReady, GridReady, NoEvidence, UnsupportedGrid, Cancelled,
Failed. Errors: InvalidInput, UnsupportedProfile, Capacity, ArithmeticOverflow,
ZeroBudget. For fixture reporting, a constructor Err is normalized to status
Failed plus its error reason; no session/result exists. Error during stepping sets
Failed; unsuccessful terminal states expose no result. NoEvidence/UnsupportedGrid
have explicit reason, not a partial grid.
cancel only changes Running; flag clearing cannot resume a cancelled session and
later cancel cannot alter completed results. Reusing scratch creates a fresh
session and must not expose prior partial data. Preserve caller sentinels outside
the provided scratch/output ranges.

Test quotas1/7/256/4096/usize::MAX and cancellation at every reachable phase.
Because this postprocessor can finish before a10ms requester wakes, the measured
cancel case deliberately times an **already pending** request immediately before
one step: use full4-48000, advance with quota1 until CheckResiduals, timestamp and
set the flag, then call step(usize::MAX). This is pending-request service
latency<=50ms on host, not asynchronous
interruption or whole-product cancel qualification. Existing HS-01 asynchronous
observations remain separate. Time scratch drop separately; do not report it as
fence latency. No accepted product slot is written by this experiment.

## 8. Fixtures, checks and fixed measured run

The public case JSON fixes all shorthand expansion and expected statuses/fields.
The Rust CLI/tests may use fixed literals corresponding to those cases; embed the
fixture byte digest at compile time. The Python harness reads the canonical JSON
and checks every case ID/result independently. Do not add a Rust JSON dependency
or accept arbitrary case data through a file/CLI interface.
Positive grids cover regular3/4 at both rates, fractional Q32 fitting,40BPM and
300BPM/600s, offset/phase witnesses and EOF exclusion. Negative cases cover missing/
extra/duplicate/unordered/EOF events, phase/timing breaks, late anchor, uncovered
tail, insufficient events and capacity. Extraction cases cover initial/final
partial regions, class/row ties, row0-only threshold, preceding f32 threshold,
all-nonbeat, zero/NaN/Inf/out-of-range/sum-invalid probabilities, invalid data after
support, sentinel/state/edge errors and span drift. No malformed row is silently
skipped. Expected fixture arithmetic is an independent Fraction calculation;
registration checks are not decoder/postprocessor PASS claims.

Also require two small composition tests: use the unchanged HS-01 HMM on generated
probabilities (m3,N=3.5*fs; m4,N=4.5*fs, interval25 and row0 downbeat), derive the
threshold span, decode only that span, restore offset, then construct grid. Compare
to the expected regular source-wide lattice; fixture statuses have priority over
upstream convenience behavior. Do not run a full-size monolithic oracle.

Focused preflight: new debug/release tests, fmt and focused Clippy(-D warnings),
existing product_grid_contract controls, prior compact_hmm tests and default
runtime context controls. Preserve the existing C-oracle-dependent ignored test
as NOT_RUN unless its actual oracle is supplied. No unrelated C/P4 matrix is needed.

The measured order is fixed: fixture batch, full3-44100, full4-44100,
full3-48000, full4-48000, cancel. Full cases use artificial **legal supplied paths**,
not inferred music: m3 interval25/start number2/first peak row5, m4 interval10/
start number1/first peak0, N=600*fs. Each runs extraction plus grid once, hashes
events and canonical summary, and checks the registered oracle. No HMM rerun is
needed for those full postprocessor cases; the two small composition tests cover
the interface. All denominators/counts remain visible, including expected refusals.
Event hashes use the eight-byte encoding in section2. Canonical summary digest is
SHA-256 of the complete `expected` object (including GridReady status), sorted
ASCII keys, compact JSON separators comma/colon without spaces, and one final LF;
anchor_q32 and period_q32 are decimal strings, other scalar types follow fixtures.
Do not hash timing or identity metadata into that summary digest. The fixture
freezes both digests for each full case and the applicable small positive cases.

## 9. Resource and identity protocol

Use the observed HS-01 WSL environment (Rust/Cargo1.95.0, Python3.12.3) and
unchanged neutral `apta_dense_runner_support.py`. Whole generated path+probability+
event payload is444576 bytes before maps/controls. The known Python supervisor
baseline is about19MiB. Registered **host-only** per-case caps are CPU10s,
wall30s, RSS64MiB, AS128MiB, aggregate attempt output1MiB. They allow those
owners/runtime overhead without authorizing a full HMM trace workspace. These are
operational caps, not internal/PSRAM allowances or P4 throughput targets.

Before cases, retain fixed enforcement refusals: wall0.05s versus1s sleep,
RSS16MiB versus64MiB allocating/touching child,4096B versus65536B output, and
AS128MiB versus256MiB allocation. Use the proven minimal fresh RSS supervisor
and verify that the allocating child, not just its parent, exceeded16MiB.
Keep per-process HWM/rusage as well as sampled envelopes, process-group cleanup,
inherited limits and all logs even on failure.

Bind exact clean implementation commit, feature/toolchain/binary, this protocol,
fixture bytes, frozen authorities/assets, all six code components, interpreter,
loaded source/cache/extensions/shared libraries and linked ELF libraries before
measurement. Recheck after every case and source at finish. Executable/build/output
must be outside checkout; output new/exclusive and disjoint from inputs. CLI only
accepts fixed case names, no arbitrary input/audio/model/corpus paths. Check the
returned supervisor status and final aggregate output byte count as well as worker
results. This is API/input isolation and supervision, not an OS filesystem sandbox.

PASS is **HS02_SYNTHETIC_HOST_PASS_ONLY** when every registered fixture/status,
field/hash/parity/quota/cancellation/native-wire/resource check completes. An
arithmetic or semantic mismatch is synthetic FAIL; integrity/resource/incomplete
execution is INVALID/INCOMPLETE. Stop on first failure, preserve all artifacts,
never tune tolerances/caps, select a subset or overwrite output. A genuine
implementation repair needs a new clean commit before a disclosed attempt; no
real-data retry or protocol rescue is authorized.

## 10. Implementation order for continuation

1. Implement typed Event/Geometry/summary/errors and checked pure fitter. Run the
   positive rational fixtures and every malformed/unsupported event case first.
2. Implement incremental probability/trim validation, frozen map/edge checks and
   connected-region extraction. Run all extraction fixtures and offsets/ties.
3. Add budget/cancel/reuse fences, exact capacity checks and all quota/phase tests.
4. Add unchanged native/wire validation adapters in tests plus two small HMM
   composition tests. No product publication integration is introduced.
5. Add fixed synthetic CLI and neutral supervised harness, enforce identities/caps
   and strict JSON, then finish tests/lints. Commit/push implementation.
6. Run the six cases once from clean exact source, audit complete artifacts, write
   aggregate result and update handoff/status; commit/push before another step.

This checkpoint completes registration only. Implementation, measured cases,
musical transfer/acceptance, model execution, physical resources/timing and product
activation are NOT_RUN. Original CS-01/PF-02, HS-01 and all older verdicts stay frozen.
