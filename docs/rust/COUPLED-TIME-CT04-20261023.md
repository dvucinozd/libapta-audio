# CT-04: separate construction, verification and evaluation

Date: 2026-10-23. **GENERATED_OPERATIONAL_PREFLIGHT_PASS; real development NOT_RUN.**
Implementation parent: `e1f93fcf9aa31cdd98e4b8dfc4a3c88f1dae5175`.
The [CT-03 development registration](COUPLED-TIME-DEVELOPMENT-PROTOCOL.md) and
[machine protocol](COUPLED-TIME-DEVELOPMENT-PROTOCOL.json) remain unchanged:
`681fb6f20eb93b2817b7feee683e6f442c142573fe9e4c4860875b209237d4d6`.
The [aggregate receipt](COUPLED-TIME-CT04-20261023.json) binds exact component,
test, bootstrap, runtime-log and generated evidence hashes.

## Implemented phase boundary

The opt-in [runner](../../tools/apta_1_1_coupled_time.py) has construct, verify and
evaluate roles, each with a separately supervised process. Construct/verify use
one fresh worker per source. Its public CLI has no model, metric, parameter,
resource, cohort or identity override. Every public and internal role requires
the registered tokens; a hidden worker role does not bypass authorization.

Construction reads only the bound CT-03 label-free input manifest and original
event/capture provenance. It checks exact event/downbeat/frame/window relationships,
source order and denominator, and writes jobs with exactly beat_frames,
downbeat_ordinals,total_frames,windows,known_gaps. IDs, paths, labels, old scores
and coarse values do not enter numerical construction. Each worker checks its
job against the immutable registered observations and rejects extra fields.

All candidates use the CT-01/02 compact sorted finite JSON byte convention.
The complete candidate cohort and hashes freeze before verification. Verification
imports only the independent verifier, requires the complete supervised freeze,
and checks exact candidate bytes/hash/coordinates through its own arithmetic.
The complete verified cohort then binds the construction completion hash.

Only evaluate may open the separately bound private inventory, labels and old
score files. It first requires both supervised completion envelopes, matching
candidate hashes, same source revision, input identities and current bindings.
Missing, partial, corrupted or stale verification stops before label/scorer use.
No constructor or verifier helper is called after label loading.

The evaluation path imports the unchanged registered score_source and gates
helpers plus their canonical adapters. It rechecks the original model/runtime
freeze, warms metric dependencies, binds loaded runtime files, and rechecks them
after scoring. Every old event metric, OLS/coarse score, fixed baseline aggregate,
reference-context count and genre macro must reproduce before a verdict.
Actual outgoing intervals retain cross-window successors and final None through
the registered helper. All 16 musical gates remain conjunctive and unchanged.

Candidate/worker reports, source/runtime identities, frozen cohort, supervision
records and score details remain external. Output directories are exclusive;
partials and INVALID diagnostics survive a failure. Time/RSS/total-output checks
cover all phases and preserve the CT-00 ceilings. The unchanged neutral supervisor
accounts for parent and descendants; pure workers also impose RLIMIT_AS.
Evaluation's NumPy/scipy runtime uses measured RSS supervision, not RLIMIT_AS.
This process/import boundary is not an OS sandbox.

## Operational and numerical checks

The [new suite](../../tests/unit/apta_1_1_coupled_time_runner.py) has **16 tests**:

- Complete construct/verify/evaluate in separate processes with labels physically
  absent during construction/verification, then restored for canonical scoring.
- Changed label contents leave independently constructed candidate bytes identical.
- Missing verification, candidate corruption, stale verification/freezes, mutated
  labels, baseline mismatch and partial source cohorts cannot publish a verdict.
- Mutated input hashes, forbidden input fields and tampered worker jobs are
  rejected; input/source/component identity and clean Git snapshot checks execute.
- Existing attempt output cannot be overwritten; dirty source and stale component
  identities reject a new phase.
- Actual parent timeout, RSS and output-limit probes terminate and retain INVALID
  artifacts without a completion claim.
- Public construct/verify/evaluate and internal phase/worker roles remain disabled
  without operational and real-attempt tokens; import boundaries are checked.

The test-only [bootstrap](../../tests/unit/apta_coupled_runner_bootstrap.py) injects
generated identities and a disposable clean Git fixture into this same phase
machinery. Production never imports it and has no environment/settings hook.
Test-only limit reductions exercise parent enforcement without changing worker
limits or registration. The ordinary stdlib suite uses a named metric stand-in;
the separately recorded experiment-runtime run uses unchanged mir_eval functions
for every generated fixture and all three actual processes. The stand-in is not
canonical event-metric evidence and is never installed on the production path.

The final aggregate records **53 tests:23 CT-01,14 CT-02 and16 CT-04**, with zero
skips/failures/errors, plus the additional16-test standard-library routine run.
CT-02's 21-case numerical/process preflight is repeated. The CT-01/02 numerical
implementation, verifier and supervisor bytes remain unchanged; two historical
test assertions that required runner absence are updated because CT-04 now tests
disabled real dispatch directly. This does not amend old evidence or protocols.
The old dense component-bound preflight token also remains unchanged.

The canonical end-to-end fixture has two generated sources/four windows. Old and
new event macros are equal. Its verdict is REJECTED under the original absolute
real-cohort gates; a generated fixture is not allowed to qualify the detector.
Detailed phase wall/RSS/output measurements are in the bound aggregate receipt.
They include runtime loading, file binding, subprocess startup and rechecks;
they are host operational costs, not P4 or neural-inference budgets.

| Canonical generated phase | Wall seconds | Maximum observed process RSS/HWM KiB |
|---|---:|---:|
| Construct | 3.261 | 18196 |
| Verify | 3.743 | 18144 |
| Evaluate | 6.969 | 108968 |

The combined generated attempt uses 478659 bytes including its completion and
supervision artifacts, below 67108864. Each phase remains below 1800 seconds
and every observed process below 262144 KiB. These two-source/four-window values
do not predict the full 100-source diagnostic's total time.

Development checks exposed two test-harness issues before the final unchanged
run: a child completion-byte check fired before the intended parent-output probe,
and a stale-component test encountered a source edit during that development
run. Parent-only test limits and a fresh fixed-code run resolve those checks.
No numerical/scoring algorithm, frozen parameter or old result was changed.

Reproduce routine checks on Linux/WSL from the repository root:

```text
python3 tests/unit/apta_1_1_coupled_time.py -v
python3 tests/unit/apta_1_1_coupled_time_verify.py -v
python3 tests/unit/apta_1_1_coupled_time_runner.py -v
```

For canonical event metrics, use the already-bound experiment Python environment
with NumPy/mir_eval and set APTA_CT04_CANONICAL_EVENTS=1 for the test suite only.
The suite is wired into [the combined runner](../../rust/check.py). The broader
C/Rust build matrix was not rerun for these Python host modules. Evidence is
component-bound on the CT-03 parent; phase cleanliness tests use a real disposable
generated Git repository, not a claim that the in-progress checkout was clean.

## Real dispatch remains disabled; next CT-05

The public runner requires the exact CT-02 token, immutable CT-03 registration,
this CT-04 component-bound operational receipt and a distinct
COUPLED-TIME-ATTEMPT-AUTHORIZATION.json record. That record does not exist at
CT-04. Its checked format is libapta-coupled-time-authorized-attempt-1,
authorized=true,scope=EXPOSED_DIAGNOSTIC_ONLY,matching registration/CT-04 hashes,
maximum_attempts integer1 and the SHA-256 of the normalized absolute attempt
path. This allows one fixed external destination without publishing its path.
Missing/stale tokens, wrong destination or existing output are rejected.

**Next: CT-05, one separately authorized exposed diagnostic attempt.** Before
dispatch, record the exact attempt scope/destination hash, verify matching
components/protocol/runtime, and commit/push the execution checkpoint. All phases
must use the same clean source revision; confirm it is pushed before the run.
All 100 candidates must freeze and independently verify before real scoring.
Preserve any INVALID/INCOMPLETE attempt; no automatic retry, parameter rescue,
new inference, model change, source replacement or topology fallback.

No private corpus input, real labels or audio was read during CT-04. All label
and metric checks here use generated fixtures. No real candidate was constructed
or scored. Independence remains UNRESOLVED; even all musical gates passing on
the already-exposed GTZAN cohort means EXPOSED_DIAGNOSTIC_PASS_ONLY. Independent
transfer, whole neural-pipeline/P4 feasibility, confidence, consumer integration,
fresh acceptance and release remain open. Dense-v1 stays REJECTED.
