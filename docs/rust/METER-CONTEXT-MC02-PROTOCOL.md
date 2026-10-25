# MC-02 generated operational adapter registration

Registered 2026-10-25 against `45b2838f96d7891273e9c69f74896625c85d8d7b`,
`rust-rewrite`, before adapter code or new measurement. The
[MC-00 contract](METER-CONTEXT-PROTOCOL.md) and
[MC-01 receipt](METER-CONTEXT-MC01-20261025.json) freeze meter behavior.
[Machine registration](METER-CONTEXT-MC02-PROTOCOL.json) binds this document,
the old phase engine/evaluator, MC-01 and all operational authorities.

## Fixed adapter, reused engine

Implement only `tools/apta_1_1_meter_context_runner.py` and
`tests/unit/apta_1_1_meter_context_runner.py`. Load the unchanged P5 runner as a
private module instance. Reuse its strict records, job staging, completion chain,
source audit, canonical evaluator, phase loop, independent proof checks and
resource enforcement. The original public module/CLI and source bytes remain
unchanged; no new scoring implementation or solver is introduced.

In new PROJECT/VERIFY worker processes only, install fixed module facades at the
two names selected by the old phase loop. PROJECT delegates to the MC-01 helper;
VERIFY delegates to the independent MC-01 oracle. Each receives only the five
per-source event/geometry arguments; the adapter explicitly passes known_gaps=[]
after the phase rejects any nonempty/unknown job gap declaration. Preserve the
original P1 modules under fixed aliases and bind their actual files as well as
the facade source. This avoids recursive calls and retains every loaded byte in
the runtime closure. The verifier must load neither new nor old production
projector code. The adapter has no arbitrary module/function/environment switch.

EVALUATE imports the unchanged warmed canonical evaluator only after complete
PROJECT/VERIFY proofs. Its P3 ledgers/tolerances/denominators remain authoritative;
MC-00 owns the new projection meaning. Before references are loaded, additionally
reconstruct minimal jobs from the label-free scoped event leaves and require exact
typed equality with the staged jobs. Rehashed jobs/manifest/proofs cannot substitute
different events or geometry under the same fixed scope. This is input binding,
not another constructor or score calculation.

Use new worker/profile entry points, a fresh derived runtime descriptor and a
per-attempt meter-context identity binding MC-00, this operational protocol,
P3 scientific protocol, runtime, source and manifest. Verify that identity at
every phase boundary. Old descriptor format and phase record formats may be
reused, but components/profiles/new identity must identify the new adapter. No
old descriptor or authorization can select it. All73 P5 components and all MC-01
component bytes remain frozen. No parent runtime image is modified.

## Scope, isolation and qualification

Operational fixtures reuse the existing public fixed60-beat recipe in the P5
test helper. Both arms remain deliberately equal. Run a complete100-source /
600-window fixture,400 complete /200 incomplete references,1200 new projections
and2400 cells. This is PUBLIC_PREFLIGHT, never the saved C3 cohort. Reuse/inherit
the existing14 process tests where applicable; add checks for actual MC-01
projection records, independent loaded profiles, old-token/runtime rejection,
new identity corruption and rehashed job-to-scope divergence before label loading.
Retain no-resume, parity, strict JSON/type/order, missing/changed-label and actual
timeout/RSS/output negative checks. Repeat the15 frozen MC-01 public controls.

Bind the actual parent interpreter, Python/extension/shared-library files and
all loaded source bytes. Keep parent runtime roots and source read-only and
disable bytecode caches. Verify actual loaded profiles before/after phases and
parent inventory before/after qualification. Full runtime preparation must occur
on exact source bytes; public preparation fixes retain earlier logs/descriptors.
No derived profile may add an unbound dependency absent from the frozen parent
inventory. Report parent-observed and worker self RSS separately.

Limits remain60s/source,1800s/phase or test suite,262144KiB/process and67108864
bytes per fresh attempt including staged inputs/logs/receipts. Public fixtures
are included in supervised accounting. Preserve all failures; do not overwrite
outputs or resume an INVALID attempt. Existing tests may be bound to the new
private engine in the test process only, without editing the old test file.

## Future authorization and closeout

MC-02 publishes a new component/runtime-bound
`METER-CONTEXT-MC02-PREFLIGHT.json` only after complete public qualification.
Fixed C3 dispatch requires a later separately tracked
`METER-CONTEXT-GENERATED-SCOPE.json`, distinct format, all three protocol hashes,
exact components/runtime/preflight/scope and one fresh destination on clean pushed
source. That document is absent and is not created at MC-02. Read its inherited
C3 scope hash only from the frozen P5 receipt; no C3 event/label/score leaves are
opened here. A later operator must rehash that full closure before dispatch.

MC-02 does not change the MC-00 algorithm, old gates/verdicts or candidate events.
Keep real dispatch disabled. H0 remains NOT_AUDITED, independence UNRESOLVED,
confidence and complete product budgets unqualified. No model inference/training,
native activation, physical P4, fresh acceptance, release or VERSION change.
Commit/push registration before implementation and verified closeout before the
next generated comparison registration.
