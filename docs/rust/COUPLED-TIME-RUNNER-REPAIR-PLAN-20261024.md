# Coupled-time runner repair and cohort-scale preflight plan

Date: 2026-10-24. **PLAN_ONLY; implementation and generated full-cohort run NOT_RUN.**
Base: `67a509c284ab31a6734458f75056f05db845ba15`.
Evidence: [cost diagnosis](COUPLED-TIME-COST-DIAGNOSIS-20261024.md),
[aggregate](COUPLED-TIME-COST-DIAGNOSIS-20261024.json), and the
[closed CT-05 attempt](COUPLED-TIME-CT05-20261023.md).

Implement the steps below in order. Commit/push each verified checkpoint before
the next. This plan authorizes no real candidate replay, real scoring or retry.
Do not resume CT-05 or replace its scope, authorization, candidates or receipts.

## Fixed boundaries

- Change operational orchestration only. Keep the pure constructor/verifier,
  numerical protocol/schema, original decoded events, all scoring helpers,
  16 musical gates, baselines and topology byte-identical.
- Keep 60 seconds/source, 1800 seconds/phase, 262144 KiB/process and 67108864
  bytes/attempt. No parallelism, increased limits, stat-only digest cache,
  selective cohort, different filesystem or weaker numerical replay as a rescue.
- Preserve one fresh worker per source, all-construct then all-verify then separate
  evaluation, original event/job identity, exact candidate bytes and lossless replay.
- Keep the existing runner/support and historical tokens/results unchanged.
  Introduce a separate v2 runner and operational registration/preflight. The
  CT-03 scientific contract remains a pinned immutable input, not an editable token.
- Current CT-05 authorization must never enable v2. A later real attempt needs
  its own destination, authorization, clean pushed source and scope decision.
  Independence stays UNRESOLVED; even all musical gates passing remains exposed
  diagnostic evidence. No model, inference, acquisition, holdout or native work.

## R0 — register the operational contract

Add docs/rust/COUPLED-TIME-RUNNER-V2-PROTOCOL.json and matching .md before coding.
Bind CT-00/CT-03, the original constructor/verifier/scoring/support hashes,
CT-04/CT-05 history and this plan. Register the phase/worker checks below, context
schema, output schema versions, process boundaries, unchanged caps and stop policy.
The proposed v2 file is tools/apta_1_1_coupled_time_v2.py. Test-only configuration
injection remains confined to tests; production accepts no override/environment hook.

Register exact public roles construct/verify/evaluate and internal roles phase/
worker. All roles require the v2 registration, successful component-bound v2
preflight and a distinct v2 authorization for real inputs. Before these exist,
public/internal real dispatch must reject. A generated test bootstrap exercises
the same code in disposable fixtures without creating real authorization.

Explicitly document the integrity boundary change: all-cohort checks happen at
phase entry/exit; workers check their selected source before/after use. Mutation
of an unrelated source may be detected later, but must prevent a completion
envelope or scoring. This is process isolation, not an OS sandbox.

Exit: reviewed schema/registration, no code or real data run. Commit/push.

## R1 — separate global and selected-source validation

Implement these interfaces in the new v2 runner; use the unchanged neutral
apta_dense_runner_support for content binding, supervision and strict source checks.

| Interface | Responsibility |
|---|---|
| load_input_manifest(path,cfg,bindings) | Hash-bound strict manifest read and full header/order/count validation, returning rows; no event/capture file loop |
| validate_source(row,cfg,bindings) | Read only that row's event/capture pair and enforce original frame/downbeat/geometry provenance |
| audit_cohort(rows,cfg,bindings) | Parent-only full event/capture validation in fixed order |
| read_phase_context(path,expected_sha,cfg,role,index,bindings) | Strict context/header/digest/scope/source/role/index checks; return the selected row's job/candidate identities |
| check_worker_result(report,context,index,role) | Validate exact report fields, expected selected digests and allowed binding scope before parent recheck |

Reading the small bound manifest in each worker is allowed. Reopening all200
event/capture files is not. Validate the selected row against the bound manifest;
never trust a free-standing job/context as the sole source of original observations.
The numerical constructor still receives exactly its five plain arguments, with
no IDs, paths, labels, old scores or contextual provenance objects.

Add an exclusive, immutable phase-context record containing format/version,
role, source revision, v2 operational registration hash, CT-03 hash, authorization
hash, input-manifest hash, source count/order, and ordered job/candidate identities.
For verify, also bind the complete successful construction envelope/freeze hash.
Pass its expected digest explicitly to the child; verify all header identities
against registered authority, not merely against other untrusted context fields.
No self-reported current digest may replace an expected registered digest.

Context paths/job/candidate indices must resolve to the prescribed attempt
locations. Reject extra fields, wrong roles/indices, stale source/auth hashes,
changed manifests, swapped sources/jobs/candidates, duplicate/nonfinite JSON,
symlinks, traversal and out-of-root output paths. Preserve strict typing.

## R2 — move whole-cohort checks to supervised phase boundaries

The phase process owns the immutable expected global binding map. Full source
snapshot, cohort audit and freeze/candidate audit occur inside its existing
1800-second supervision. Report CLI and supervised wall time separately; do not
move expensive work outside supervision to make the phase appear faster.

Construct sequence:

1. Validate tokens/source and take the full clean tracked-source snapshot.
2. Load the manifest; audit all sources; freeze all plain job hashes and context.
3. For each source, supervise one fresh worker with the 60-second ceiling.
4. Worker validates its context, manifest-selected row, selected pair and job;
   checks source identity and component/runtime bytes; constructs only that source;
   binds/rechecks selected inputs/job/output/runtime before reporting.
5. Parent validates the report and rechecks only its permitted selected bindings.
6. Parent rechecks the original full global map, all outputs and clean source.
   Only then write the full-cohort completion and supervised-success envelope.

Verify sequence:

1. Take a full clean source snapshot; require the complete supervised construct
   freeze; audit all original input pairs and all candidate hashes once.
2. Freeze the verify context with the construction/freeze digest and full cohort.
3. Each fresh worker validates only its own manifest row, event/capture pair,
   job, candidate, context and component/runtime bytes, with source checks.
4. Call the unchanged independent verify_bytes for that source. Never call the
   constructor. Retain exact-byte/hash/coordinate/rounding/topology checks.
5. Parent validates/rechecks the bounded selected report; do not call frozen()
   or full-cohort audit from inside individual workers or this per-source loop.
6. After all workers, parent rechecks the immutable global expected binding map,
   all candidates, runtime provenance and source. Bind matching construct/verify
   candidate hashes and only then publish whole-cohort verification success.

Global maps cannot be overwritten by worker observations, opportunistic rehashes
or caches. Local maps must not import all other sources or the full tracked tree.
Retain worker runtime checks and source checks before/after use. Parent entry/exit
checks remain mandatory even when every worker returns PASS. A partial/mutated/
stale cohort produces INVALID, with partials preserved and no completion envelope.

Record stage timings in worker/phase reports, never in numerical candidate bytes:
input validation, context/provenance, runtime binding, the exact constructor or
verify_bytes call, and final rechecks. Define non-overlapping spans or explicitly
mark nested ones. All remain inside source/phase supervision; never deduct them
from the ceiling. Bounded bind/open counters are aggregate integers, not paths.

Evaluation remains a separate process. Require v2 construct/verify context,
envelope and candidate hash agreement before opening labels. Reuse the unchanged
legacy runner's evaluation function only after these v2 gates, or its exact
registered score_source/gates helpers; do not add a new metric implementation.
Bind the v2 operational identity separately from the unchanged CT-03 scientific
identity. Require full original baseline/context/event/genre reproduction before
publishing a musical verdict. Do not permit v1/v2 envelope mixing or the old
authorization as a v2 dispatch bypass.

## R3 — generated regression and integrity checks

Add tests/unit/apta_1_1_coupled_time_v2_runner.py and a separate test-only bootstrap.
Wire the suite into rust/check.py at the repair checkpoint; retain old fixtures
and tokens as history. New preflight binds the changed combined runner as well.
Do not alter old numerical tests or old preflight receipts to make hashes match.

Required tests and expected behavior:

- Selected worker: instrument file opens/bind calls on generated1/10/100-source
  fixtures. It may read the manifest/context but must not open any unselected
  event/capture/candidate or hash the full tracked tree. Call counts for selected
  observation/candidate files stay constant as cohort size grows.
- Parent: full cohort/source checks occur at entry/exit and complete before the
  success envelope. Assert invocation counts rather than brittle wall-time limits.
- Mutate an unselected future source, an already processed source, candidate,
  context or tracked file between workers and leave it changed: selected validation
  or final global recheck rejects; no verification envelope and no scoring.
- A poisoned worker binding map cannot replace registered expectations or add
  out-of-scope files. Wrong source/role/context digest and swapped indices reject.
- Dirty source, stale components, missing/partial verification, old-token-only
  dispatch, cross-version envelopes, overwrite and malformed jobs reject.
- Generated labels absent/permuted during construction/verification leave candidate
  bytes identical. Check actual process/import boundaries, not just mocked calls.
- Byte parity with v1 on generated constant, jittered, ramp, active-box, boundary,
  insufficient-event and gap cases. Preserve numerical abstention/failure semantics.
- Parent/worker timeout, RSS, output cap, descendant termination and live-orphan
  negative probes retain INVALID and prevent evaluation. Reuse the unchanged
  supervisor; test-only reduced caps must never become production options.
- Generated canonical mir_eval evaluation reproduces fixed old/new fixture scores
  through the unchanged evaluator; no stand-in qualifies the canonical path.

Run relevant existing numerical/oracle/verifier controls and new v2 tests with
zero unexpected failures/errors/skips. Historical tests expecting absent real
authorization must use isolated fixtures, not the consumed CT-05 token. Record
exact commands/results. Full C/native/P4 matrices are outside this host change.
Commit/push the implementation before the large generated measurement.

## R4 — complete generated operational preflight at cohort scale

Add a generated preflight driver under tests/unit or tools with generated-only
schema and no capability to select real private manifests. It uses the same v2
phase/worker code through the separate test bootstrap. Keep production override-free.

Run all100 generated sources/six windows each, with synthetic observations up to
the already-public148-event real-shape bound. Include constant/jitter/ramp and
boundary ownership cases in a deterministic fixed order. Do not derive fixtures
from private candidate values or labels. Keep the separate N3072/window8 numerical
stress controls; passing realistic100-source throughput does not replace them.

Use a disposable clean Git source fixture from a tracked-only archive of the
clean repair commit. Record its tracked count and byte size; they must reproduce
that commit, with at least1024 tracked files. The old one-file Git fixture is
inadequate for this gate.
Bind the actual implementation and generated source snapshot. Exercise the same
WSL/Windows-mounted source and generated-observation storage classes as CT-05;
keep generated output on the corresponding Linux filesystem. Record topology
and storage scope without private paths. Do not silently move all inputs to a
faster filesystem to qualify the runner used in the original topology.

Construct all100, independently verify all100, then evaluate all100 in separate
supervised processes with canonical NumPy/mir_eval in the existing experiment
environment. Hide generated labels during the first two phases and restore them
only for evaluation. Bind all generated outputs before scoring. Retain no subset
success claim. Record all600 windows, original fixture baseline reproduction,
paired/event/genre aggregates, generated verdict, per-source/phase/CLI time,
RSS/descendants, stage spans, binding/open counts and total output bytes.

Pass only if every generated phase completes, every original cap is met and all
operational/isolation/parity gates pass. A generated musical verdict is not a
real musical qualification. A resource/integrity failure stops this measurement;
preserve it and return to a distinct repair checkpoint, without cap increases.
Record the generated driver/test/component hashes in a new v2 preflight receipt.
Commit/push the receipt and updated handoff. No real authorization is created.

## R5 — explicit decision after preflight

Report what passed and what remains unrun. Any future real attempt requires a
separate scope decision/authorization record binding v2 preflight, exact code,
inputs, new destination, prior failed attempt and one-run policy. Commit/push that
scope before measurement. This plan and a successful generated preflight do not
authorize a retry, replay of the remaining44 CT-05 candidates or scoring the old
56-source subset. Preserve the failed attempt permanently.

Independent transfer, model membership, confidence, neural-pipeline resources,
native consumer, physical P4, fresh acceptance and release remain separate gates.
