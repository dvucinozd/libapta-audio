# HS-01: synthetic-only checkpointed HMM host prototype

Registered 2026-10-26 from `3b0dbdb647cc752fdaa5111609c038ca6ba6ad84` on
`rust-rewrite`. [Machine protocol](PRODUCT-HOST-HMM-PROTOCOL.json).

The owner explicitly requests registration AND implementation of the proposed
narrow synthetic experiment. This is a scoped addendum to the product-first
plan, not completion of product PF-04. Missing board budgets and BeatNet+ rights/
training provenance remain gates for model use and the substantial product port.
This experiment uses only our frozen public mathematical graphs and generated
log densities: no neural code/weights, music, labels, saved logits or old attempts.
H1 is not needed and is not run. No provider, consumer pin, wire, C/core DSP or
VERSION change. Host success is not musical, neural, embedded or release success.

## Hypothesis and fixed algorithm

Checkpoint/recompute can reproduce the same F64 sparse Viterbi optimum and path
as a monolithic/exhaustive oracle, with the CS-01 bounded parent/checkpoint storage
and interruptible work units. The changed axis is traceback memory organization;
transition tables, recurrence, class mapping, ties and graph parameters are fixed.

Implement only a safe Rust, opt-in host module in `libapta-runtime`, feature
`experimental-compact-hmm`, with required-feature binary `apta-compact-hmm-synthetic`.
Core remains unchanged. Planned files: `rust/runtime/src/compact_hmm.rs`,
`rust/runtime/src/bin/apta-compact-hmm-synthetic.rs`,
`rust/runtime/tests/compact_hmm.rs`, `tools/apta_compact_hmm_synthetic.py`.
Cargo/runtime exports may change only to gate these paths. No new dependency.
Reuse neutral binding/supervision from `tools/apta_dense_runner_support.py`;
do not import old decoder/corpus/neural runners or build a runtime framework.

Graph inputs: borrowed u32 incoming pointers, sorted unique u16 predecessors,
finite non-positive F64 log transitions, u8 observation classes0..2, uniform prior.
Density input: borrowed `[f64;3]` per row, finite [-1024,0] or -inf; NaN/+inf fail.
The -1024 guard conservatively contains all log probabilities derived from f32
probabilities (including p_non/15) and prevents finite-score overflow. Transitions
and prior use the same finite range. No epsilon clipping or normalization.
Limits:1..11220 states, at most28380 edges, at most30000 rows, block1..256.
Production-sized graph runs use B256; smaller B only tests the tiny oracle.

Use CS-02 graph assets and exact stored log-priors (3:0xc0221356bc07601a,
4:0xc022a6a1cc9106f7). Density class is2 when beat0 and16*q<interval,1 for other
beat ranges, otherwise0. Finite score recurrence is exactly
`(previous[p]+log_transition)+log_density`, with no FMA/reassociation.
Strict `>` preserves the first incoming predecessor; final argmax uses lowest ID.
All -inf paths give NoPath, zero rows NoEvidence, not a fabricated path.

API: `StoragePlan::new`, `Workspace::allocate`, `Decoder::new`, `step(quota,cancel)`,
`cancel`, `status`, `result`. Explicit errors for malformed/capacity/allocation/
zero-budget/replay failures. Workspace owns pre-sized Vec arrays in the std host
layer; decoder borrows them. No allocation/reallocation in step or cancel.
Score mode uses two score vectors. Trace mode adds `ceil(R/B)*S` F64 checkpoints,
`min(B,R)*S` u16 parents and R u16 path slots. Check arithmetic before allocation.
No partial path is exposed, including after failure/cancel. Workspace can be reused
after decoder drop; no allocator/free-time guarantee is inferred from a logical phase.

Initialize scores, copy/restore checkpoints, visit state, evaluate predecessor,
select final state and traceback all advance in bounded scalar units. Every unit
checks an AtomicBool cancellation request. A zero quota is invalid. A terminal
decoder cannot resume even if the flag is cleared. Constructor validation/allocation
and subsequent workspace destruction are outside step and reported separately.
Host cancellation gate concerns request-to-step-return/publication fence, not P4
I/O cancellation or an approved whole-product cleanup deadline.

Trace algorithm: save the complete previous-score vector before each forward
block; select final state; restore/replay each block from last to first, retaining
only its local parents; carry the predecessor across boundaries. Do not read a
frame0 prior-pointer as an output state. Verify replay end-score bits against the
forward final score or next checkpoint before exposing any path. Sentinel65535
is never valid. Score-only mode stores no checkpoints/parents/path.

## Oracles, generated inputs and pass criteria

Tests before the measured run:

- CS-02 four exhaustive rational tiny cases and an independent monolithic F64
  oracle; compare score bits/path/status, including ties and -inf/no path.
- Boundaries257/513 rows on the frozen large graphs against monolithic reference;
  full-size monolithic allocation is forbidden. Verify B1/2/3 on tiny graphs.
- Same results under quotas1/7/256/4096/usize::MAX; cancellation at every phase,
  terminal fence, scratch reuse, no partial output or step reallocation.
- Malformed pointers/classes/edges, sentinel/out-of-range IDs, duplicate/unsorted
  incoming edges, nonfinite/out-of-domain values, capacity/zero quota/block errors.
- Explicit replay checkpoint corruption must fail, not publish an altered path.

Fixed measured cases in order: small3(R257), small4(R513), full3(R30000),
full4(R30000), cancel(R30000 on graph4). No filtering/replacement/retry after failure.
Generated f32 probability triple per row r, beat interval25 and target meter m:
downbeat `r%(25*m)==0`: [.02,.96,.02]; other beat `r%25==0`: [.96,.02,.02];
other rows:[.01,.01,.98]. Convert to F64 logs `(log(p_non/15),log(p_beat),log(p_down))`.
No RNG, source ID, label or file input. CLI only selects these named cases.

Ordinary cases score graphs3 then4; ties select3. Trace the selected graph and
require identical score bits to its score-only pass. Independently verify every
path edge and its recomputed score; publish path SHA-256, not a massive trace.
Selected meter must match the generator's m. Record owned workspace payload,
stage/max-step time, units, wall/CPU/peak RSS, toolchain, executable/protocol/source
hashes. Small monolithic checks do not license a full-size dense parent table.

Cancel case: a separate thread requests cancellation10ms after starting a huge-quota
decode; require Cancelled, no result and no resumption with cleared flag.
Record request-to-return and drop/reclaim separately. Request-to-return<=50ms is
a HOST synthetic fence gate; it does not establish the P4 product target.

## Fixed host operational caps and outcome

Environment observed at registration: Linux WSL Ubuntu, Python3.12.3,
Rust/Cargo1.95.0,16 logical CPUs, MemTotal16052120 KiB. This selects test operational
caps, not internal/PSRAM budgets. For each Rust case: CPU120s, address space256MiB,
wall180s, RSS128MiB, aggregate attempt output1MiB. Expected live payload is roughly
18MiB plus host controls; the RSS/AS caps deliberately allow host allocator/runtime
overhead without allowing a full parent table. No claim of a60s P4 final result.
Use Linux inherited rlimits and existing parent process-group/RSS/wall/output monitor.
Record child rusage high-water in addition to sampled RSS, rejecting excess even
if the monitor missed a short-lived peak. No orphan descendants or overwritten output.

Before cases, verify enforcement with fixed synthetic probes: wall0.05s vs1s sleep,
RSS16MiB vs64MiB allocation, output4096B vs65536B write, and AS256MiB vs512MiB
allocation. These intentional refusals are separate preflight results. Preserve logs.

PASS means HS01_SYNTHETIC_HOST_PASS_ONLY. Numerical/parity/capacity/cancel failures
are synthetic FAIL; integrity/resource/incomplete execution is INVALID/INCOMPLETE,
never a musical rejection. Stop on failure and preserve all artifacts. Implementation
bugs may be repaired under this fixed mathematical protocol with a new clean source
checkpoint; do not silently relax caps or algorithm/tolerance rules. No real-data retry
or promotion is authorized. Frozen older verdicts and acceptance criteria stay unchanged.

Commit/push this registration before implementation. After tests/lints, commit/push
implementation; run measured cases only on that clean exact revision, then separately
commit/push the aggregate result. Build output and run logs remain outside checkout.
The user explicitly requests both registration and implementation in this continuation.
