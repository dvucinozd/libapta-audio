# C1 isolated reference-support implementation

Date: 2026-10-24. Status: **IMPLEMENTED; focused generated unit checks only.**
Baseline source: `485dfa50d0043bd66a9c29d3d45c01aa25a95bc9` on `rust-rewrite`.
Authority: [registered amendment](COUPLED-TIME-SUPPORT-METADATA-AMENDMENT.md),
[isolated operational contract](COUPLED-TIME-RUNNER-V2-METADATA-PROTOCOL.md)
and [ordered repair plan](COUPLED-TIME-R5-REPAIR-PLAN-20261024.md).
The [aggregate receipt](COUPLED-TIME-METADATA-C1-20261024.json) binds exact code,
test logs, resources and the retained immutable runtime. C0 files remain the
registration-time snapshot; their `C1_implemented=false` does not describe this
later implementation checkpoint. This receipt and MIGRATION own the next handoff.

## Implemented boundary

- `tools/apta_reference_support.py` validates exact integer reference beats and
  one to eight ordered half-open windows, then emits complementary complete and
  incomplete counters using the unchanged reference-bracketing predicate.
  Unknown coverage fails; gaps do not redefine support. No source-frame ceiling
  is added. Work is O(reference beats + windows), with no sorting, I/O or imports.
- `tools/apta_1_1_coupled_time_evaluation_v2_metadata.py` versions the minimum
  legacy evaluation orchestration. It rechecks the original label-free cohort,
  construction and verification receipts before importing support or reading the
  private inventory. It calls unchanged canonical `score_source` and `gates` with
  the same arguments. The copy-on-write extension checks old complete bits and
  adds only the six registered diagnostic leaves. Aggregation conserves support
  and preserves baseline, genre, event, paired and scientific-gate checks.
- `tools/apta_1_1_coupled_time_v2_metadata.py` is the separate entry point. It
  retains the old source/cohort checks, numerical workers and supervision, with
  new formats, authority markers and distinct paths. Its stdlib validator checks
  exact types, local schemas, complementary counters, ordering, role boundaries
  and source/cohort sums. Evaluation writes a separate proof and scientific
  payload; phase/parent/receipt checks verify actual hashes, original source/window
  order, all four diagnostic totals, per-window aliases and candidate identities.

Canonical `Counter` accumulators are materialized as JSON objects before strict
payload validation. No numeric value is changed. Construct/verify import guards
also reject the support helper and versioned evaluator. Reference support is not
an input to numerical construction or independent verification.

All 25 old R4 component files and 13 immutable scientific dependencies retain
their registered hashes. C0 contracts, old results, old preflight and `VERSION`
remain unchanged. There is no production override or selectable evaluator.

## Checks and limits of the evidence

The three new focused suites contain 26 tests. Together with the unchanged 16
C0 controls, **42 tests pass, zero failures/errors/skips**. They cover exact
endpoints, left/right/both incomplete coverage, missing or ill-typed inputs,
ordered/gapped geometry, generated 400/200 and interface-only 401/199 partitions,
old-bit disagreement, six-leaf parity, unchanged canonical gate results, strict
formats/types, role nullability and corrupt actual proof/payload/detail chains.
The evaluation-boundary regression also ensures that later evaluation-context
bindings are not required inside earlier construction/verification receipts.

Canonical scorer parity uses small public generated fixtures only. The versioned
evaluation orchestration is also exercised with generated labels and a test-only
boundary stand-in; its real boundary rejection is tested separately. This is
unit evidence, **not** full construct/verify/evaluate process isolation evidence.
No saved R4 scoring replay or new cohort attempt is performed here. The known
generated R4 rejection at 14/16 remains frozen, not newly measured by these units.

Tests run inside the retained read-only runtime image, whose full inventory is
verified before and after. The canonical suites use Python 3.12.3 and the retained
metric environment; schema/runtime contract suites use the frozen system Python
and jsonschema installation. The receipt records actual suite supervision times
and observed process RSS/HWM, including the outer supervisor. These are focused
host-test costs, not isolated evaluator, complete pipeline or P4 costs. Runtime
inventory/setup is outside those suite timings. Original experiment caps remain
unchanged. Earlier failing unit iterations are retained externally; they did not
launch a corpus attempt.

Real CLI dispatch fails closed without the new preflight and authorization. It
also rejects shape-only future tokens: the projection/runtime/real-scope report
content contracts and their authorization checks are still a later registered
step. No real-data dispatch is enabled by C1.

## Next checkpoint: C2

1. Add the registered metadata bootstrap, generated preparation driver, fixed
   immutable-runtime launcher and metadata check utility. Production must never
   import the test bootstrap. Reuse the old runtime helper API without editing it.
2. Exercise actual new construct/verify/evaluate processes, withholding and
   permuting generated labels. Cover selected-source access at 1/10/100 sources,
   full global checks, mixed/stale/corrupt receipts, missing proof/counters,
   time/RSS/output/descendant/orphan failures and existing numerical controls.
3. Implement the exact saved-generated parity comparison: all 100 candidate byte
   hashes and all 600 diagnostic records, removing only the six allowed leaves;
   validate deliberately new identities against actual files. Preserve every old
   event/family/paired value and all 16 gates, including the 14/16 rejection.
   Do not rewrite or run a subset of old attempts.
4. Commit/push the component-bound generated preparation before C3. C2 does not
   create the final metadata preflight or a real authorization. C3 is a separately
   supervised fresh full 100-source/600-window attempt on clean pushed code.

The five remaining registered implementation paths are test/bootstrap/preflight/
runtime support, not missing numerical or scorer implementations. Projection is
UNDECIDED, historical real-runtime metadata NOT_AUDITED and independence UNRESOLVED.
There is no new model inference, training, real-data replay, transfer, confidence
qualification, native activation, physical P4 evidence, acceptance or release.
