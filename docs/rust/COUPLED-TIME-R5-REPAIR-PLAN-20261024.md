# R5 separate repair and registration plan

Date: 2026-10-24. Status: **PLAN_ONLY; no repair or real execution enabled.**
Authority: [completed saved-generated audit](COUPLED-TIME-R5-AUDIT-20261024.md),
[aggregate evidence](COUPLED-TIME-R5-AUDIT-20261024.json), the frozen CT03/runner
registrations and [machine checklist](COUPLED-TIME-R5-REPAIR-PLAN-20261024.json).
Implement on the user's `rust-rewrite` branch. The machine's next item is C0.
Do not skip a registration checkpoint or combine the counter and projection axes.

## Preserved controls

Keep the current13 immutable files, old scientific/operational registrations,
all historical result/preflight/authorization files and old attempts byte-identical.
The two failed generated gates remain failed. Preserve400/200 generated coverage
and the real protocol's401/199 expectation; neither is a tuning target. The original
60s/source,1800s/phase,262144KiB/process and67108864-byte attempt limits remain fixed.
The immutable runtime and Windows-source/Linux-observation/output classes stay bound.
Commit/push every verified checkpoint and confirm matching clean local/remote SHA
before a new measured run. No real authorization is created by this plan.

## C0 — register a metadata-only extension before implementation

Produce a versioned support-metadata amendment and new operational registration,
with schemas, exact parent hashes, allowed implementation paths, output identity,
parity tests, caps and one-attempt/stop rules. Proposed document family:
`COUPLED-TIME-SUPPORT-METADATA-AMENDMENT.json/.md` and
`COUPLED-TIME-RUNNER-V2-METADATA-PROTOCOL.json/.md`. Names are planned files;
no such executable authority is created by this plan.

The amendment changes the emitted context-accounting interface only. Original
constructor/verifier, projection, event/grid/meter scores, gates and expected real
counts stay frozen. Explicitly authorize a **new versioned evaluator component**;
do not patch the old immutable legacy evaluator or monkeypatch its scorer globals.
The mutable v2 orchestration may select the new evaluator only under the new
hashed registration. No environment/CLI override may select an implementation.

Use a distinct scientific payload format/metadata identity and preflight path
(e.g. `COUPLED-TIME-RUNNER-V2-METADATA-PREFLIGHT.json`), with separate authorization
identity. Bind the original scientific parent plus amendment and operational
identities in the relevant records. Schema/cross-field checks must reject old/new
mixed envelopes, stale metadata, missing counters and original CT05 authorization.
Preserve the current v2 preflight file; new source cannot qualify by changing its
stored component hashes. Keep execution disabled until fresh generated preflight.

Pass: registration/schema/link checks and explicit metadata-only parity contract;
no candidate/scoring/data run. Commit/push the registration before C1.

## C1 — implement isolated support accounting and versioned evaluation

Planned new helper: `tools/apta_reference_support.py`.
Interface: `reference_support(reference_beats, windows) -> list[dict]`.
Validate strictly ordered integer reference beats, valid half-open windows and
explicit coverage availability. For each window retain the frozen predicate:
`reference_beats[0] <= first_frame and end_frame - 1 <= reference_beats[-1]`.
Emit integer `windows=1`, `reference_complete_windows=int(context)` and
`reference_incomplete_windows=1-int(context)`. Unknown/missing reference coverage
is an explicit error/unavailable state, never silently zero incomplete windows.
The helper is used only after full candidate construction and independent verify;
it must not be imported/called in pure workers or become a constructor input.

Planned versioned evaluator: `tools/apta_1_1_coupled_time_evaluation_v2_metadata.py`.
Reuse the unchanged canonical `score_source` and gates. Copy/version only the
minimum evaluator orchestration needed to add support metadata before aggregation;
retain exact source/order/input/baseline/runtime checks. Compare the helper's
complete-context bit to the old emitted bit, then add the missing complementary
field to the registered diagnostic records. Enforce complete+incomplete=windows
per window, source and cohort. Do not change any old numeric score, projection,
mask, unknown successor, topology, event metric, paired counter or scientific gate.

Update the mutable v2 orchestration to the newly registered evaluator and record
identities. Preserve original13 immutable dependency bytes and keep all new
components hash-bound. Document the additional O(number of reference beats +
windows) metadata work and measure actual host resources; infer no P4 budget.

Pass: implementation and focused tests under the new metadata registration;
old full R4 is neither replayed in place nor relabeled. Commit/push before C2.

## C2 — generated parity and integrity tests

Add pure context tests for complete, left/right/both incomplete, exact endpoint,
empty/invalid/boolean/duplicate inputs and unknown coverage. Use public synthetic
geometry to test both400/200 and401/199 partitions; the latter is an interface
example, not evidence from the unopened real cohort. Reject explicit0 incomplete
when totals require200 and reject all missing/ill-typed/nonconserving counters.

Compare all600 saved generated diagnostic records: after removing only the new
metadata fields, every original field/value must agree exactly. These are exposed
regression controls. Require original complete bits, all event means, per-family
means, unknown masks, paired counters and16 scientific gate verdicts unchanged.
The original generated scientific verdict must stay REJECTED (14/16) in this
counter-only checkpoint. A changed meter/downbeat verdict vetoes metadata parity;
it is not a successful counter repair.

Test actual construct/verify/evaluate process separation and absent/permuted labels,
strict new schema types/identities, stale/missing/mixed receipts, old-token rejection,
corrupt support fields, timeout/RSS/output and descendant/orphan failure paths.
Retain numerical controls, selected1/10/100 access checks and all original caps.
No full C/P4 matrix is required for this Python metadata layer. Commit/push a
component-bound generated preparation before C3.

## C3 — fresh complete generated preflight

Use a new generated destination from the clean pushed C2 source and exact prepared
runtime. Construct100, independently verify100, then evaluate100/600. Do not reuse
old attempt envelopes. Require candidate-byte parity with the existing R4 because
this axis changes no numerical input or model. Require explicit400 complete +200
incomplete=600 and exact old-score/gate parity; publication must distinguish
operational success from the still-rejected generated scientific outcome.

Any identity/resource/parity/support failure closes this distinct attempt INVALID;
preserve partials, no automatic retry or cap increase. Publish exact source,
code/runtime/input hashes, resources and new preflight only if all registered
operational gates pass. Commit/push. This still does not authorize a real run.

## P0 — projection-contract decision, separately from the counter repair

The audited failure is a valid-tempo OLS fit with an out-of-window rounded anchor;
window event membership and downbeat ordinals were preserved. Do not clamp or
shift the anchor, borrow the old meter or relax the guard just to pass this case.
Those operations change representation/scoring semantics or candidate behavior.

Review the native/APTA consumer representation and source/window ownership contract
before selecting one scientific axis. If keeping the existing projector, a new
candidate hypothesis must specify how representability is preserved. If keeping
the existing candidate, a new projection/representation hypothesis must specify
its anchor, phase, meter and endpoint semantics. Do not change both axes together.
The numerical parameters, unknown support, independent verifier/oracle, no-regression
veto, exposure/transfer scope and resource budget must be registered before coding
or measuring either hypothesis. This decision is UNDECIDED, not implementation work
for a weaker model to invent. Retaining the current candidate also retains its known
limitation and REJECTED fixture result; any diagnostic-only real scope must say so.

## H0 — historical real runtime metadata, then an explicit real-scope decision

The saved-generated audit did not open the historical real neural-runtime freeze.
A separate metadata-only scope must inventory that closure without opening audio,
labels or model inference. Compare actual dependency bytes, not version strings.
Record whether the known system-library delta affects that real freeze; do not
rewrite it or assume current generated compatibility requalifies it.

Only after counter qualification, an explicit projection-scope decision and the
runtime audit may a new real-scope proposal be written. It must bind the new
preflight/source/code/input identities, prior failures, new destination, exposure/
independence limitations and one-run stop policy. Commit/push before measurement.
This plan neither writes that authorization nor launches a real replay, the old
56-source subset, transfer, acceptance, training, native integration or a release.
