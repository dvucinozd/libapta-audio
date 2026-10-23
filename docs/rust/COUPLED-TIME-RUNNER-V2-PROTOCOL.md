# Coupled-time runner v2 operational registration

Date: 2026-10-24. **R0_REGISTERED; v2 implementation/preflight/real execution NOT_RUN.**
Parent: `089603f1176d1158a65823ca520ace3e90750968`.
Machine authority: [operational protocol](COUPLED-TIME-RUNNER-V2-PROTOCOL.json).
Record shapes: [Draft2020-12 schema](COUPLED-TIME-RUNNER-V2-SCHEMA.json).
Registration evidence: [R0 receipt](COUPLED-TIME-RUNNER-V2-R0-20261024.json).
Predecessors: [repair plan](COUPLED-TIME-RUNNER-REPAIR-PLAN-20261024.md),
[cost diagnosis](COUPLED-TIME-COST-DIAGNOSIS-20261024.md) and
[closed CT-05 attempt](COUPLED-TIME-CT05-20261023.md).

## Authority and fixed scientific meaning

This registration changes only orchestration. Implement the separate
tools/apta_1_1_coupled_time_v2.py; retain the old runner/support and all old
tokens/results. Do not invoke the old real dispatch path. Pure constructor,
independent verifier, numerical protocol/schema, baseline, input identities,
event topology, scoring adapters and all16 musical gates remain unchanged.
CT-03 SHA-256 is681fb6f20eb93b2817b7feee683e6f442c142573fe9e4c4860875b209237d4d6.

The machine protocol binds immutable CT-03 components plus the old runner's
evaluation implementation. Historical CT-04 is evidence, not permission to
rewrite its receipt when rust/check.py later gains a new suite. A new R4 receipt
must bind every new v2 component, test/bootstrap/preflight driver and changed
combined runner. Current source hashes must match that receipt at dispatch.

Keep60s/source,1800s/phase,262144KiB/process and67108864bytes/attempt. These are
host operational ceilings. Preserve one fresh worker per source, fixed cohort
order and all-construct -> all-verify -> separate-evaluate sequencing. There is
no parallelism, stat-only digest cache, larger limit, source subset, numerical
shortcut, new inference, training, acquisition or holdout opening.

## Dispatch and authorization

Public commands are construct --attempt PATH --inputs FILE, verify --attempt
PATH, and evaluate --attempt PATH --inventory FILE. Internal commands are _phase
ROLE with the same applicable phase arguments, and _worker ROLE INDEX --attempt
PATH --context-sha256 HEX. Worker ROLE is construct or verify; _phase also allows
evaluate. Context paths are derived from ROLE and PATH, never supplied freely.

All public/internal real roles require this exact operational registration,
the immutable scientific contract, a successful component-bound
COUPLED-TIME-RUNNER-V2-PREFLIGHT.json, and the separate
COUPLED-TIME-RUNNER-V2-ATTEMPT-AUTHORIZATION.json. No production environment,
identity, parameter, cohort or budget override exists. Missing/stale records,
old authorization or v1 completion/supervision formats reject before real work.
The generated bootstrap is test-only, never imported by tools/.

The schema registers future authorization/preflight shapes; no such real token
is created at R0. Authorization fixes one new normalized absolute destination
by SHA-256, exact manifest/inventory and registration/preflight identities, with
maximum_attempts integer1. Existing output cannot be replaced. Every role must
refer to the same clean committed/pushed execution source. The preflight's
source revision is its own earlier clean code checkpoint, not a self-referential
claim about the later authorization commit; exact component bytes must match.

## Record encoding and phase context

All new operational records use UTF-8 finite JSON, sorted keys, indent2 and a
trailing newline, written exclusively. Reject duplicate fields, nonfinite values,
wrong exact runtime types, extra/missing fields and unregistered versions. Booleans
are never integers; reject integral-valued floats in integer fields. SHA fields
are lowercase64-hex and revision fields lowercase40-hex. Schema shape validation
does not replace the following semantic checks.

Numerical candidates retain the exact CT-01 compact sorted JSON convention,
without newline; v2 adds no numerical candidate fields. IDs/paths/context remain
wrapper-only and external. The pure model still receives only beat_frames,
downbeat_ordinals,total_frames,windows,known_gaps.

Each role publishes ROLE.context.json before starting its children. The strict
context includes role, source/operational/scientific/authorization/destination
identities, bound manifest path/hash,100 source IDs in exact registered order,
600 windows and100 ordered rows (index,source_id,job_sha256,candidate_sha256).
Construct candidate hashes and upstream receipts are null. Verify requires
non-null candidate and construction completion/supervision hashes, while its
verification upstream hashes are null. Evaluate requires both upstream pairs.
The context never contains labels, scores, coarse values or numerical job data.

A worker binds the supplied context digest, validates its header against actual
registered authority/source/destination, and compares its selected row with the
hash-bound original manifest. Digest agreement between two untrusted fields is
insufficient. The selected job and output location are derived from its index;
reject wrong role/index, reordered/duplicate IDs, stale upstreams, substituted
manifest, swapped jobs/candidates, symlinks, traversal and overlapping output.
Context fields cannot override an expected input or code digest.

The input manifest may be read by each worker. Reading all other source files
or checking all candidate payloads there is forbidden. validate_source reads
only the selected event/capture pair and verifies exact original observations,
downbeat ordinals, source length and window provenance. The registered full
manifest identity still proves cohort membership; a self-contained job is not
sufficient evidence.

## Global and selected-source integrity boundaries

All expensive full-source snapshots, input/candidate audits and final rechecks
run inside the supervised phase, not in unsupervised CLI preparation. CLI time
and phase time are separately reported. The immutable expected global binding
map is owned by the parent and cannot be overwritten by a worker or rehash cache.

Construct: take the clean tracked-source snapshot; load/audit the full cohort;
write all plain jobs and context; run each fresh worker in order; validate and
recheck its permitted selected report; after all workers recheck original global
bindings, all outputs and clean source before publishing completion.

Verify: require the successful full v2 construction envelope/freeze, snapshot
the clean source, audit all original inputs and all candidate hashes once, then
write verify context. Each worker checks its selected source/job/candidate and
calls only the unchanged independent verify_bytes. Parent validates/rechecks
the selected report. After all workers it rechecks global bindings, all output
hashes and clean source, then binds identical construction/verification cohorts.

Retain component/runtime content checks and source checks before/after each
worker. Allowed worker bindings consist only of registered contract/component
files, actual loaded runtime files, context, manifest, selected event/capture,
job and candidate. No whole tracked tree, unselected source/candidate or cohort
completion recheck may leak into that map. Parent validates expected digests and
allowed scope before rechecking; reject poisoned or out-of-scope worker maps.

An unrelated source mutation may now be detected at a later selected-source
check or the final full recheck. It must prevent a completion/success envelope
and all scoring. This timing change is explicit. Neither the old nor new runner
is an OS sandbox or a guarantee against transient mutations restored between
checks. Persisting mutations to already processed/future inputs, outputs or
tracked source are required negative tests.

Each phase's final recheck must still run if every worker reports PASS. Workers
cannot declare whole-cohort success. Wrong source, altered input/code/runtime,
missing report, disagreement, resource failure or incomplete cohort is INVALID;
retain partials, stop, and do not enter the next phase.

## Receipts, evaluation and resource accounting

Context/worker/completion/supervision formats end in -2. Numeric candidates and
the legacy scientific evaluation payload retain their original versions.
Construct/verify completion paths are ROLE.completion.json; their contents use
the new v2 phase-completion schema, so the old runner rejects them. All outer
supervision records use ROLE.v2.supervised.json. Evaluation's scientific payload
is evaluate.completion.json; its distinct v2 completion wrapper is
evaluate.v2.completion.json and binds that payload's digest. There are no v1/v2
compatibility aliases or success-envelope substitutions.

Every completion binds its context, exact source, both registrations,
authorization, destination/manifest, complete ordered candidate list and global
binding map. Construct/verify bind100 worker-report hashes and report labels
unloaded. Evaluate binds no worker reports, labels_loaded=true and the scientific
payload digest. Each outer envelope binds that phase's completion/resources hashes
and resource PASS. Check role-specific upstream chains and all matching identities;
mere file existence is insufficient. Reject v1/v2 mixing.

Only after complete supervised v2 construction AND verification may evaluation
parse the full inventory, labels and old scores or import scoring modules. Call
the unchanged legacy runner.evaluate function with the verified cohort and
original CT-03 config. It writes the scientific evaluate.completion.json and
uses the unchanged adapters/gates/runtime/baseline reproduction checks. Do not
call constructor or verifier helpers after opening labels. The v2 completion
and supervision wrap this output; a scientific payload alone grants no success.

Supervise the whole phase process group including descendants and every fresh
worker, using the unchanged neutral supervisor. Pure workers retain RLIMIT_AS;
canonical evaluation retains measured RSS supervision. Apply whole-attempt bytes
to all jobs, contexts, logs, worker/candidate/scoring data, receipts and partials.
Account for the final record write; report CLI overhead without hiding it.

Worker timing spans are non-overlapping: startup_seconds,input_validation_seconds,
runtime_binding_seconds,compute_seconds,finalization_seconds. They start at
worker entry and finish just before report serialization. compute_seconds wraps
only the exact pure constructor or verify_bytes call. wall_seconds covers that
inner span; summed stages must agree within1e-6s (timer accounting only). Source
supervision additionally includes launch/report/tail; no measured span is
deducted from the registered ceiling. Worker counters record manifest/context/
selected-pair/job/candidate reads and binds, plus zero unselected reads, full
cohort audits and full tracked-tree rechecks. Generated open/bind instrumentation
must establish these zeros independently of self-reported counters.

## Required R3/R4 evidence before real dispatch

The machine protocol lists conjunctive generated gates. Exercise1/10/100-source
file access scaling, source/role/context/job/candidate poisoning, persistent
cross-worker mutations, missing/partial/stale/mixed envelopes, dirty source,
old-token rejection, overwrite, strict JSON/type/path rules, absent/permuted-label
invariance, exact generated v1 byte parity, canonical evaluator parity and actual
timeout/RSS/output/descendant/orphan failures. All tests must pass without skips.

R4 uses100 deterministic generated sources/six windows each, at most148 events
per realistic source with constant/jitter/ramp/boundary cases. Retain separate
N3072/window8 stress controls. Archive the full clean repair commit into a clean
disposable Git fixture, reproducing its tracked file count/bytes with at least1024
files. Use the original filesystem classes: Windows-mounted source, Linux-side
generated observations/output. CT-05 observations were Linux-side; the old
two-source fixture and a faster topology cannot replace this gate.

Hide generated labels for construction and verification; restore only for the
separate evaluation using canonical NumPy/mir_eval in the existing experiment
environment. Require100/100 construction,100/100 replay and100/600 evaluation,
unchanged fixture baseline reproduction and all caps. Bind code/test/bootstrap/
driver hashes and publish stage/resource/access measurements. A generated
scientific verdict does not qualify real music. Resource/integrity failure closes
the generated measurement; preserve it and use a separate repair checkpoint.

The repair plan's observation-storage wording is clarified here against CT-05:
the original event/capture paths are Linux-side, while the source checkout is
Windows-mounted. No real observation is opened to establish this topology at
R0; it is taken from the existing diagnosis bindings/path categories and handoff.
R4 must explicitly verify its generated topology. This is an orchestration
clarification, not a filesystem move or amendment to the scientific protocol.

## Stop and next checkpoint

R0 is documentation/schema registration only. It runs no v2 worker, numerical
solver/replay, generated cohort, real candidate or scorer, and reads no private
input, audio or labels. Schema examples establish shapes only; runtime
cross-field/integrity/operational gates remain NOT_RUN.

Next: implement R1/R2 and the R3 generated tests, then commit/push before R4.
Keep real dispatch disabled until the new component-bound R4 receipt and a
separately authorized new scope/destination exist. CT-05 remains closed; no
automatic retry, remaining44-source replay or56-source subset scoring follows.
Independent transfer, confidence, whole neural-pipeline/P4 resources, native
consumer, fresh acceptance and release remain open. Stable VERSION stays1.0.1.
