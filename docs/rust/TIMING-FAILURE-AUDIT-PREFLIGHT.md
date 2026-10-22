# Timing attribution implementation and preflight

Date: 2026-10-22. Registration was committed and pushed at
`3082d6d1b0180a7e9963a0581c2afbf78b5d24eb` before implementation or new audit
results. The [protocol](TIMING-FAILURE-AUDIT-PROTOCOL.json) remains unchanged,
SHA-256 `8f47045ccd26983b04085aabb4a58d2868f074528bb2031804aed2a3e6270867`.

`tools/apta_1_1_timing_failure_audit.py` reads only the saved event, label and
coarse/OLS records. It reproduces the original model projection and adds a
separately named reference-only witness using the same project/scoring functions.
Exact Fraction arithmetic intersects the local-period tolerance intervals and
classifies seven fixed period ratios. Raw correspondence keeps unknown reference
context separate from phase mismatches and counts missing, nonunique and
outside-window correspondence explicitly. The display triage priority is fixed;
overlapping flags and family/witness cross-tabs remain available independently.

The runner refuses dirty code or existing output, binds all tracked source and
old input/detail files before execution and writes a private freeze before new
diagnostics. It reproduces every old coarse/OLS tuple, per-window verdict and
aggregate, preserves the complete source partition and both original regressions,
hashes every detail and rechecks source/inputs after the run. Only aggregate,
genre and redacted regression diagnostics may be committed. No audio, inference,
reserved transfer, new labels or model/candidate promotion is permitted.

Verification passes **17 new tests**: constant positive control, all seven tempo
ratios, half/double timing, phase offsets, missing/extra/duplicate correspondence,
incomplete reference, unsupported model projection, exact closed period-interval
endpoints, variable-reference witness failure, overlapping flags/priority,
precision-only success with missing reference coverage, source shifts, source
immutability, outside-window matches, aggregation, invalid/capacity inputs and
protocol hash refusal. The 18 previous multi-beat, ten representation, ten neural
reference and six annotation tests also pass, for **61 Python tests**.

```sh
python3 tests/unit/apta_1_1_timing_failure_audit.py -v
python3 tests/unit/apta_1_1_multibeat_timing.py -v
python3 tests/unit/apta_1_1_event_representation.py -v
python3 tests/unit/apta_1_1_neural_reference.py -v
python3 tests/unit/apta_1_1_native_annotated_development.py -v
```

The new suite uses the standard library only and is added to `rust/check.py`.
Rust core, frozen scorers, prior candidates and consumer sources are unchanged.
No new full Rust/C/sanitizer, hosted-CI or physical-P4 run is claimed for this
host diagnostic. Commit/push preflight before the single clean-source audit;
synthetic verification is not a musical detector or product qualification.
