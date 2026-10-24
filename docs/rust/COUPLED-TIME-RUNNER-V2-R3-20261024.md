# Coupled-time runner v2 implementation and focused generated checks

Date: 2026-10-24. **R1/R2_IMPLEMENTED; R3_FOCUSED_GENERATED_CHECKS_PASS; R4 NOT_RUN.**
Implementation parent: `75bf2f6accfa71e977dec49674b6101c8cecdb60`.
Authority: [v2 protocol](COUPLED-TIME-RUNNER-V2-PROTOCOL.md), unchanged operational
SHA-256 `9c252c0734c43d3856146b2915648e6273d38060edf635fce717298131a2370e`.
[Aggregate evidence](COUPLED-TIME-RUNNER-V2-R3-20261024.json) binds all code,
test helpers, logs and the canonical generated operational receipt.

## Implemented behavior

The separate [v2 runner](../../tools/apta_1_1_coupled_time_v2.py) performs the
full source/cohort audits at supervised phase boundaries. A fresh numerical
worker opens its own source's event/capture pair, job and candidate. It may
read manifest/context/upstream receipt metadata, but never rechecks the full
upstream binding map or opens unselected source/candidate payloads. Expected
job hashes come from the original construction context, not new opportunistic
hashes. Parent entry/exit checks remain mandatory before a success envelope.

Worker maps are restricted to selected files, registered components and actual
runtime files; missing/poisoned/out-of-scope expectations reject. Full receipt
checks require every expected global binding and matching ordered worker/candidate
cohorts. Context, job, upstream, source and both registration identities agree
before use. Persisting mutations to past/future inputs, candidates or tracked
source prevent completion. A final recheck still executes after all workers PASS.

Snapshot, loaded runtime and legacy evaluation maps merge without replacing
previously registered expectations. A dedicated collision regression rejects
a conflicting snapshot before any worker is started.

Records use the pinned strict schemas with exact runtime types, finite values,
duplicate-field rejection and lowercase digests. Traversal, symlink paths,
overwrite, stale/mixed receipts and completed/INVALID attempt replay reject.
Git source checks remain before/after workers; zero whole-tree worker SHA rechecks
does not mean Git stops checking working-tree cleanliness.

Worker stage timing separates startup, selected input validation, runtime binding,
the exact pure numerical call and finalization. All costs remain inside source/
phase ceilings. Independently instrumented file access tests establish locality;
self-reported counters alone are not used as proof.

Only a complete supervised v2 construct AND verify chain may enter evaluation.
The unchanged legacy evaluate function writes the scientific payload, wrapped
by a separate v2 completion/supervision chain. Candidates and all scientific
adapters/gates/baselines remain unchanged. The constructor/verifier/scorer/support
and all13 immutable components match the registered hashes. There is no new
neural inference or native/Rust product integration in this host-only change.

Production has no test settings/environment hook. All public/internal real roles
require a distinct R4 component receipt and new v2 authorization; neither exists.
The historical CT-05 token cannot enable this runner. Its failed attempt stays
closed and is never replayed or scored by these tests.

## Executed generated checks

**76 distinct focused tests,99 evidence executions,zero failures/errors/skips:** CT-01 23,
CT-02 14,legacy operational16,new v2 23,plus an additional23-test v2 canonical run.
The53 unchanged legacy/numerical control results were reused only after
verifying their dependencies and original log hashes. Following the final
immutable-map guard addition, both23-test v2 suites ran again on the final
component set. The earlier22-test development passes are retained externally
but excluded from this receipt. The routine operational suites explicitly use
a named metric stand-in. The
separate v2 canonical run uses unchanged NumPy/mir_eval in the retained experiment
environment; the stand-in never enters production evaluation.

- CT-02 repeats its21-case full generated numerical/process preflight, including
  N3072/window8, corruption, descendant RSS/group termination and orphan rejection.
- V2 constructs/verifies exact candidate bytes for seven registered shapes:
  affine25000,jitter,ramp,full_bound_active,crossing,short and gap. No parameter,
  rounding, abstention or topology fallback changes.
- Actual two-source/four-window construct,verify,evaluate execute in separate
  processes. Labels are physically absent in the first two phases. Changed
  generated labels leave candidate bytes unchanged. Canonical old/new macros agree.
- An independent Draft2020-12 library checks13 actual emitted operational records
  per complete fixture, in addition to the production record validator.
- Construct AND verify selected-worker access is instrumented at cohort sizes
  1,10,100 (six child calls). Unselected observation/job/candidate opens are zero;
  each selected event file has exactly3 bind calls regardless of cohort size.
  The verify access unit fixtures use hand-sealed generated receipt metadata;
  these are not full100-source phases or R4 supervision evidence.
- Boundary traces prove one full source snapshot and one input audit per phase,
  plus final global recheck before completion. Verify additionally audits the
  retained full construction receipt at entry. Worker loops do no full-tree audit.
- Wrong role/index/context, swapped plain jobs, poisoned maps, stale/v1/missing
  verification, dirty source, processed-candidate mutation and persistent past/
  future-source mutations fail closed. Completed/INVALID attempts cannot replay.
- Actual parent timeout/RSS/output and worker timeout/RSS/output probes retain
  INVALID without a success envelope. A live descendant blocks supervised success.

The legacy operational test for unregistered output now checks failure and no
output without requiring a missing-token error: CT-05's scope already exists
and is consumed; old component identities are now stale. No old numerical test,
preflight receipt, protocol or result was rewritten. The new suite is wired into
[the combined runner](../../rust/check.py); its full C/Rust/P4 matrix was not run.

Reproduction commands (Linux/WSL, repository root):

```text
python3 tests/unit/apta_1_1_coupled_time.py -v
python3 tests/unit/apta_1_1_coupled_time_verify.py -v
python3 tests/unit/apta_1_1_coupled_time_runner.py -v
python3 tests/unit/apta_1_1_coupled_time_v2_runner.py -v
APTA_CT04_CANONICAL_EVENTS=1 experiment-python tests/unit/apta_1_1_coupled_time_v2_runner.py -v
```

experiment-python denotes the retained canonical experiment interpreter. The
aggregate records interpreter hashes and Python/NumPy/scipy/mir_eval/schema
versions. The environment flag is a test-fixture choice only; production has no
such switch. The external driver binds exact commands/logs and reused controls.

## Canonical small-fixture resources and limits

| Phase | Supervised seconds | Maximum observed process RSS/HWM KiB |
|---|---:|---:|
| construct | 4.987 | 18660 |
| evaluate | 8.134 | 109020 |
| verify | 5.477 | 18684 |

The canonical attempt contains 1205069bytes including generated access
instrumentation, below67108864. All phases fit1800s and262144KiB/process. Its
scientific verdict is REJECTED under the unchanged absolute real-cohort gates;
a generated fixture cannot qualify music. These two-source/four-window numbers
do not establish100-source throughput or fix CT-05's operational result.

Evidence is component-bound to this code while using disposable clean generated
Git source fixtures. It is not a claim that an in-progress implementation checkout
was a clean measurement source. Test fixtures are temporary; bound logs and the
canonical aggregate are retained externally. Syntax checks were completed before
the final serial suite sequence; no explicit compilation checks or source edits
overlapped those tests.

Early development exposed two test-harness errors (worker positional arguments
and treating scientific score files as worker records). A separate development
integrity veto occurred while a syntax compilation overlapped tests, consistent
with a bytecode-cache identity change. These preceded the final unchanged-source
serial pass. No real candidate, musical threshold or numerical module was repaired.

## Next checkpoint and remaining gates

**Next: add the generated-only R4 driver, bind the full clean repair archive and
exact storage topology, then commit/push before R4 measurement.** Run100 generated
sources/600 windows on the registered source/observation/output filesystem classes,
with full tracked file count/bytes and the same caps. Keep real dispatch disabled.

No R4 receipt or real authorization is created here. Independent transfer,
confidence, full neural-pipeline/P4 feasibility, native consumer, fresh acceptance
and release remain open. Dense-v1 stays REJECTED; CT-05 stays INVALID. VERSION and
production behavior are unchanged. Private corpus files/audio/labels are unopened.

Verification driver, logs, code bindings and canonical receipt are external under
`D:/AI/LIBAPTA/outputs/coupled-time-runner-v2-r3-final-20261024/`. No source IDs, coordinates,
private manifests or corpus-level score artifacts enter Git.
