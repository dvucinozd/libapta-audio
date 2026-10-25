# MC-05: public phase-evidence companion

Date: 2026-10-25. **PUBLIC_IMPLEMENTATION_VERIFIED** on baseline
`1dd78470d8d741e31904765bf7b5f55d8740ac6c` plus the component hashes in the
[machine receipt](METER-PHASE-MC05-20261025.json). Host Python only; no musical
candidate, campaign result, Rust/native port or product qualification.

The [contract](METER-PHASE-MC05-PROTOCOL.md) and
[registration](METER-PHASE-MC05-PROTOCOL.json) were frozen before implementation.
Registration SHA-256:
`c9104badbd1125eabe6a4140a707abd6331653878ed8260728ab05fdd22a2f9c`.
External copies remain identical. Frozen P3, MC-00 and MC-03 bytes/results are
unchanged. MC-03 remains rejected.

## Implementation and interpretation

The pure [extractor](../../tools/apta_meter_phase_points.py) validates complete,
exact fixed-grid geometry and selects every owned point congruent to the meter
witness. Its interface has no references, source IDs, I/O or imports. It checks
P1 tempo/Q32 consistency, ownership, exact meter binding, source/window order,
gaps and capacities. Returned points/models are detached copies. It never fits,
stitches, fills a gap or changes a publication.

The [evaluation companion](../../tools/apta_meter_phase_evidence.py) preserves the
canonical original published-point evidence and calls unchanged P3 arithmetic for
every extracted point. It reports K count, reference-supported E count, correct
point count and an all-supported-points-correct flag. Empty E has UNKNOWN/null
conditional correctness. Wrong meter and phase correctness remain distinct.
No best-point selection or replacement of the original M/D counters occurs.
Neither module is connected to an existing runner or public/native API.

The public same-grid examples verify equal phase evidence for whole-bar witness
shifts even when original published-point support differs. Different geometry is
kept distinct. This is measurement interpretation, not an improved detector, an
amended veto, equivalence of different grids or rescoring of the 25 saved losses.

## Verification actually executed

[Public suite](../../tests/unit/apta_1_1_meter_phase.py): **16/16 PASS**, zero
failures/errors/skips; **234 complete-record comparisons** with a separate
[Fraction/median oracle](../../tests/unit/apta_meter_phase_oracle.py) that imports
no production module. Coverage includes both meters, equivalent witnesses,
different grids, fractional Q32 carries, half-open window/inclusive reference
endpoints, complete/partial/disjoint references, wrong meter/phase, exact 0.10
tolerance boundary, irregular local periods, invalid shapes/types/capacities,
input/output detachment, null evidence and fresh-process call-time I/O denial.
This is Python import/call isolation, not an OS sandbox.

Four unchanged canonical controls also pass: incomplete-window support,
Q32/tolerance/endpoints/stable ties, even local-median cutoffs and validation
before missing-meter abstention. They make ten meter-oracle and six canonical
comparisons. No historical full runner suite or saved campaign was replayed.

Commands, from the repository under WSL, with `ulimit -v 262144` and
`timeout 1800s` (logs/result outside Git):

```sh
python3 -B tests/unit/apta_1_1_meter_phase.py
python3 -B tests/unit/apta_1_1_context_matched.py \
  MatchedTests.test_downbeat_support_inside_incomplete_window_and_unscored_meter \
  MatchedTests.test_q32_tolerance_endpoint_and_stable_local_period_ties \
  MatchedTests.test_even_local_median_half_frames_and_cutoff_ties \
  MatchedTests.test_missing_meter_validates_truth_and_points_before_abstention
```

Final public run: 0.512142 s including test setup; max measured source comparison
including oracle 0.019537 s; self process maximum RSS 21912 KiB. The 256 MiB
address-space ceiling is stricter than the inherited per-process RSS cap; RSS is
observed for the test process, not a summed process-tree/product memory estimate.
The fresh-process no-I/O call additionally has a 60 s timeout. The source-call
timings are checked against 60 s after completion; suite timeout is the hard
outer enforcement, not a separate watchdog around every in-process call.

An initial 15-test/229-comparison run passed; final coverage adds tolerance and
irregular-reference checks. Both logs are preserved. No production bug or frozen
contract amendment was required. Registration/bound authorities and all existing
tracked files were checked unchanged before writing this completion/handoff.

The 8-window maximum-size geometric case uses the full 1440000-frame source and
300 BPM, yielding 150 owned points. The configured 3072-point capacity remains
unchanged; that many points cannot occur in this bounded continuous geometry at
the registered tempo range. Over-capacity input rejection is tested separately.

## Remaining work

Next checkpoint: MC-06, **register a bounded saved-projection interpretation audit**
only after reviewing this public companion. Bind exact source/runtime components,
the retained MC-03 projection/input closure and a new external output root before
opening any saved per-window data. Specify paired source/group/cohort and complete/
incomplete ledgers, keeping point counts separate from window counts and reporting
original M/D alongside E. Require original evidence unchanged and reject attempts
to replace old verdicts. Reuse canonical routines and existing verification paths;
do not create another projector, inference or worker framework for this diagnostic.
No saved-projection interpretation audit is registered or executed by MC-05.

Real runtime H0 remains NOT_AUDITED, independence UNRESOLVED, confidence unqualified
and whole-pipeline product budgets UNKNOWN. This checkpoint performs no private
scoring, inference/training, consumer change, Rust/C build, P4 run, fresh acceptance
or release. Those gates remain separate. VERSION remains 1.0.1.
