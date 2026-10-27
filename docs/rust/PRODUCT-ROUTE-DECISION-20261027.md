# RC-03: whole-route decision preparation

Date: 2026-10-27. Source `1e029785500e8a0ae3d674b63c56bede77cf3779`,
`rust-rewrite`. Status: **PREPARATION_COMPLETE; WAIT_EXTERNAL**.
[Machine ledger](PRODUCT-ROUTE-DECISION-20261027.json),
[target probe protocol](PRODUCT-TARGET-PROBE-PROTOCOL.md),
[continuation plan](PRODUCT-CONTINUATION-PLAN-20261027.md).

## Decision and scope

HS01–HS06 are reusable numerical/operational host components. HS06 now has its
complete clean four-case result. There is still no complete trained graph or
physical whole-product resource evidence. The next substantial implementation
has **no GO**. This is an unresolved feasibility decision, not a musical rejection
or proof that the P4 route is impossible.

The work ready without hardware is now complete: source-bound owner inventory,
conditional lifetime scenarios, operation counts, exact missing rights evidence
and a bounded probe preparation. No component/model/corpus/device ran here.
RC-04 requires the exact board and the BSP/allocator/instrumentation capabilities
listed below. RC-05 may record WAIT when these remain absent; RC-06 cannot start.
Do not create another small synthetic experiment to replace these prerequisites.

Consumer `origin/main` was fetched and remains
`4a8c660183910b0950ae7ddabce9e8b395a2f10d`. Its remote currently resolves to
`https://github.com/dvucinozd/Ruzina.git`. Eighteen exact source files were read
with `git show`, including the first-boot runbook, hardware matrix, platform
guards, memory probe, allocator policy, toolchain and analysis storage/streaming.
The older local main checkout and its untracked artwork/files were preserved.
No dependency pin or provider was changed; core pin remains25d09de364e9507532c0508c838a8fecf0772d13.

## 1. Owners and simultaneous lifetimes

The JSON ledger gives each owner a source path/line/revision/hash, count/element
size/alignment where established, capacity status, lifetime, placement and borrow
constraint. A null is UNKNOWN; a logical payload is not an allocator reservation.
Host layouts below are not RV32 layouts. Internal/PSRAM/DMA placement remains
unassigned until target code and an actual image prove it.

| Owner | Known logical bytes | Lifetime / accounting |
|---|---:|---|
| Source read+PCM scratch |6144|Existing consumer scenario; bounded reads; actual codec/cache/async controls extra |
| Whole source PCM |115200000 PCM16 /230400000 f32 stereo|Forbidden retention scenario for600s/48kHz; use streaming |
| HS03 resampler |260 ring;76960 static tables|During preprocessing; whole control layout extra/target unknown |
| HS04 frontend |41448 host Session;50476 static tables|During frontend; includes embedded arrays, do not add40760 array payload again |
| Feature handoff |1152 per288-f32 row|Potential bounded handoff; frontend already contains a row, extra copies require proof |
| Full feature tensor |34560000|Not part of allowed streaming baseline |
| Neural weights |3068500|767125 f32 parameters; target residency/storage/lifetime unresolved |
| Neural committed h+c |4800|4 layers*150*(h+c)*4B; pending state could add4800; no full kernel exists |
| Neural other intermediates |UNKNOWN|Conv2x279=2232B, pooled278=1112B, projection150=600B are individual logical shapes, not a proven simultaneous allocation plan |
| Probabilities |360000|30000 f32 triples, retained through event correction |
| Densities |720000|30000 f64 triples, immutable through both score passes and Trace |
| HS06 control |144 host Session;36 staged payload|Payload is embedded in Session; no additional36B in a total |
| HMM score3 / score4 |134640 /179520|Two f64 score vectors per state, sequential workspaces |
| HMM winner4 Trace |16575840|Includes179520 scores,10591680 checkpoints,5744640 parents and60000 path |
| Typed graph4 |235304|Slice-length numeric payload, not measured Vec capacity or duplicate raw assets |
| HS02 state map4 |33660|Separate3-u8/state map; not graph class-vector alias |
| Event scratch |24576|3072 Event8; retain through grid/native validation |
| Native generations + selected consumer overlap |898890|Old PF01 declared scenario, count once; includes scratch/beat/rendered banks, not a separate allocation to add to those rows |
| Additional pending/retained copies |UNKNOWN|One beat+preview payload example38464B; number of live copies, cues and allocator overhead unresolved |
| Audio FX/history |1540704 /131072|Conditional resident owners; stopped playback does not prove release |
| Display buffers |1228800 single /2457600 double|1024x600 RGB565 scenarios; actual buffer count/placement unqualified |
| Codec/cache/USB/UI stacks/futures/heap reserves |UNKNOWN|Not covered by incomplete analysis subtotal |

Conditional materialized decoder scenario, matching current API dimensions:

```text
16575840 trace +235304 graph +33660 map +360000 probabilities
 +720000 densities +24576 events =17949380 bytes
 +898890 PF01 declared overlap once =18848270 bytes
 +3068500 weights IF retained through decode =21916770 bytes
```

None is a measured/approved peak. These sums exclude unknown live owners,
allocator capacities, raw asset duplication, controls, alignment, fragmentation,
stacks and any additional banks. Do not subtract them from documented32MiB to
declare the remainder free. Graph/map residency is a declared conservative
scenario; the eventual product may rebuild/share them only under a verified plan.

| Phase | Owners that must be tracked together | Release/validation fence |
|---|---|---|
| Admit | Product residents, old retained result, storage plan, source lease | Refuse before accepted-job clock if capacity is unavailable |
| Source/frontend/neural | Residents, scratch, resampler, frontend/tables, weights, neural state, probability/density outputs | Output triples remain provisional; no full PCM/features tensor |
| Score3 | Residents, full evidence, graph3, score3; model/front-end only if explicitly still owned | Drop graph/workspace before loading score4 in sequential scenario |
| Score4 | Residents, full evidence, graph4, score4 | Both meters use identical support slice |
| Winner Trace | Residents, evidence, winner graph, checkpoint/parents/path; declared banks | Keep entire Workspace while path is borrowed |
| Postprocess | Same borrowed Trace owner, original probabilities, map/events, native builder | Completion of HMM does not free Workspace |
| Publish | New immutable generation plus retained/pending consumer banks and old reader-held result | Job/source/media identity must still match |
| Cancel | Currently live phase owners plus retained old result | Stop work and fence publication; no new result from provisional prefix |
| Cleanup | Readers/leases/futures/banks that still own memory | Await mandatory close/reclamation; cleanup budget still unknown |

Per-class peak is `max_phase(sum(actual simultaneously live allocations))`.
Unknown class/capacity/lifetime makes that peak UNKNOWN. Typed references or
"phase finished" are not evidence of deallocation. A product array reused by
two stages is counted once only after a tested exclusivity/lifetime proof.

## 2. Alternatives, not implemented or budgeted as savings

| Alternative | Derived benefit | Cost / contract change |
|---|---|---|
| Copy60000B owned path before dropping Trace workspace | Postprocess could release16515840B of trace working payload | At copy time an extra60000B overlaps existing16575840B; does not reduce the trace-stage peak, may increase peak; owned-path acceptance and replay/lifetime tests needed |
| Lazy/recomputed f64 densities from retained probabilities | Nominal720000B materialization removed | HS01 immutable-slice API changes; exact column/zero/log order and replay stability required; extra logs across score/trace/replay, bounded cancel cost and ownership need registration |
| Different traceback/checkpoint layout | UNKNOWN until a specific design/oracle exists | Current block maximum256 and frozen plan unchanged; no claimed factor or memory allowance; new path/tie/score parity, replay work and cancel bounds needed |

These are mutually conditional designs. Do not sum their possible savings or
silently retrofit them into HS01/HS06. If evidence later shows a budget shortfall,
present one specific design with its tradeoff and a separate registration.

## 3. Compute and product deadlines

Recomputed from the pinned graph: Conv5580 + projection41700 + four LSTM720000
+ head450 =767730 affine MAC/row. At30000 rows:23031900000 MAC; even giving all60s
to these dots requires383865000 MAC/s. The scalar path also implies3003 nonlinear
calls/row,90090000 total, before FFT/FIR/HMM/I/O/elementwise/publication costs.
This is operation accounting, not a measured target lower/upper time bound.

Host scopes remain separate: HS04 step20.561–20.646s; HS01 full worker14.628/16.634s;
HS06 bridge step0.004544/0.006349s. Different cohorts/scopes cannot be added into
an executed pipeline result or extrapolated to P4. Tiny HS05 timings are not a
throughput estimate for the full graph.

For first-useful<=5s, reuse existing `publication::plan_features`, incremental
overview/local BPM and generation publication infrastructure. The selected
waveform column resolution, source prefix duration, BPM availability and admission
cost still require an integrated job specification and physical proof. The current
two-pass consumer streaming wrapper withholds final handoff until both source
hashes and closes succeed; it does not automatically expose the5s provisional
result. Add provisional delivery only under existing generation/media rules, with
explicit provisional meaning. A progress percentage is not a useful waveform/BPM.

Final<=60s starts at accepted job request and includes waiting/setup, source passes,
decode, frontend/neural/HMM/grid validation and result preparation. Cancel<=50ms
includes quiescence and the publication fence; host pending-flag measurements
prove neither. Cleanup/reclamation has no approved numeric budget yet and cannot
hide blocking playback or an unlimited close. All nine PF01 resource categories
remain UNKNOWN in this checkpoint.

## 4. Rights and checkpoint provenance: concrete missing inputs

This checkpoint uses the existing dated rights review and full-graph review;
**no new upstream/public legal refresh was performed**. Their observations are
historical evidence, not a statement that the upstream repository is unchanged
today. No new grant or checkpoint-specific training record is present in this
workspace. LICENSE_UNRESOLVED and INDEPENDENCE_UNRESOLVED remain open. This is
an engineering gate, not a legal determination about possible private grants.

The artifact requiring evidence is BeatNet+ source
`bb90eb0a9065b101a4b4c4cb2b2061950266cb4b`, checkpoint
`src/BeatNetPlus/models/generic_main_weights.pt`, Git blob
`7aefd5e56ef65482be6f671c3cca8de457dad102`, SHA-256
`5bbae630b4112f3c1193654e3dbc946b850caa06a2a82e10e8de9e48bb673519`.

Local request draft, **not sent**:

> Please identify the rights holder and affirmative use, modification and
> redistribution terms for the code revision and checkpoint digest above,
> including incorporation in standalone commercial Rust/ESP32-P4 firmware,
> required attribution and third-party obligations. Separately, please provide
> the checkpoint-specific training run/config/code digests, initialization or
> fine-tuning lineage, dataset versions and actual train/validation/test/selection
> membership sufficient for recording/performance overlap checks. Private
> membership lists can remain private; a digest-bound verification arrangement
> is acceptable. Please state any membership that cannot be established.

Receiving terms does not establish independence; receiving a manifest does not
prove absence of duplicate recordings. GTZAN is exposed development; dataset
names alone do not establish a fresh transfer/acceptance split. Code, weights,
dependencies and paper licences are separate. Author messaging requires explicit
authorization; no email, issue, comment or external message was created.

## 5. Exact target prerequisites and next dispatch

The consumer runbook is concrete for BUILD_ONLY diagnostics and read-only log
ingest, but explicitly supplies no flash partition layout/command. Actual port,
unit/revision, factory readback and recovery must be observed before programming.
Last user report is board unavailable; no fresh physical observation is claimed.
Assumed revision3.2 remains unobserved; HAL minimum302 is a policy, not detection.

More importantly, `platform/mod.rs` rejects product-owner features without
`integration-check`, while `integration-check` cannot accompany a Guition board
selector. That check compiles owners without starting them or registering heap.
Therefore **there is no currently executable resident-product Guition workload**
from which to derive PF01 free/largest-block reserves. Do not remove compile
guards or enable a provider merely to manufacture measurements. A separately
reviewed board bring-up and allocator/instrumentation capability is a prerequisite.

The [probe protocol](PRODUCT-TARGET-PROBE-PROTOCOL.md) freezes preparation cases,
math recipes, limits and evidence while keeping these capabilities unresolved.
Its preparation is complete; its device execution registration is BLOCKED and
no target result is claimed. Next RC-04 may proceed only when exact-unit access
and required capabilities exist. If those or model documents remain absent,
report the missing inputs once and keep RC-05 at WAIT; do not start RC-06.

Evidence: `D:/AI/LIBAPTA/outputs/route-decision-20261027-rc03-01/` contains18
consumer source snapshots, their manifest and recomputed arithmetic. Verification
here is source/hash/link/formula/JSON consistency only, with frozen authorities
and VERSION1.0.1 preserved. No Rust/DSP performance, model or device test ran.
