# Dense decoder DD-03: isolated runner and guarded evaluation

Date: 2026-10-23. Previous DD-02 checkpoint:
`69524802bbcdcaaaa914401cd7139616c1f8a9d2`.
The [registered experiment](DENSE-DECODER-EXPERIMENT.md) and
[protocol](DENSE-DECODER-PROTOCOL.json) are unchanged.
[Machine-readable implementation checks](DENSE-DECODER-DD03-20261023.json).

## Decision

**DD-03 generated integration and failure checks PASS. Proceed to DD-04.**
The runner is implemented; the independent full scalar verifier and complete
operational preflight remain open. No real dense logits or reference labels
are opened, no real music is scored and no model inference is run here.
The registered development verdict remains **NOT_RUN**.

The public cohort CLI explicitly refuses execution until a committed
`docs/rust/DENSE-DECODER-PREFLIGHT.json` binds the protocol and exact decoder,
runner, support and independent-verifier bytes, with all required generated/
independence/isolation/resource preflight flags true. This completion artifact
does not exist yet and is a DD-04 deliverable, not a waived gate.

## Implemented construction and freeze

[Runner](../../tools/apta_1_1_dense_decoder.py):
- Public `construct --dense DIR --legacy DIR --output NEW_DIR` has no labels/
  prepared argument. A fresh import loads neither canonical evaluators, NumPy
  nor Torch. Evaluation imports occur only on the evaluate path.
- Bind the exact registered dense report and legacy worker, all payloads/records/
  legacy events, current clean tracked source and loaded runtime identities.
  Verify original source order/count, exact events, binary identity/statistics,
  source/resampled/row geometry, S6/end partition and aggregation coverage.
- Each source receives a fresh pure worker subprocess. Its job contains only
  payload/hash, total, plain windows and result destination; the pure constructor
  itself receives only channel arrays and geometry. No old event array, ID,
  coarse tempo, correctness verdict or reference path is passed to it.
- Save every candidate/hash and job/worker evidence. Recheck source, input,
  loaded runtime, output identities and full cohort before the final
  `candidates-frozen.private.json`. A mid-cohort failure preserves partial files
  without this complete-cohort claim.
- Evaluation additionally requires the construction parent's successful resource
  report, bound to the exact completion-file hash. A frozen file alone does
  not bypass a failed/missing parent watchdog.
- Existing output, input/output overlap, symlinks in artifacts, changed hashes,
  changed source/runtime, incomplete cohorts and duplicate/path-like IDs fail.

The constructor process does not parse labels or old scoring reports. Generated
tests demonstrate unchanged candidate bytes when label files change or disappear.
This is a tested software/process boundary, not an OS filesystem sandbox.

## Evaluation retains canonical meanings

Public `evaluate --candidates DIR --legacy DIR --prepared DIR --output NEW_DIR`
runs as a separate phase process. It checks the entire candidate completion,
source/inputs and parent envelope before opening labels. Generated event warmup
loads the metric dependencies before real-label parsing; runtime identities are
bound and late unbound imports rejected. Real evaluation also replays the pure
constructor and requires exact saved-candidate equality.

Canonical helpers remain unchanged:
1. `apta_1_1_neural_comparison.event_metrics`: saved/refined integer frames,
   original five-second trim, 70-ms event F1 and CMLt.
2. `ref.project`, `score_window` and common `score_points`: candidate-derived
   OLS-compatible grid/meter, original controls and nominal sequence diagnostics.
3. Common actual-event points retain true next source-event deltas, including
   S6 boundaries; the final successor stays unknown. Original OLS actual
   comparison uses the registered previous control/adjacency helper unchanged.

Per-source records keep before/after event metrics and deltas, projected tuples,
nominal/actual diagnostics, support and paired period/phase/joint/meter/downbeat
changes. The aggregate checks full original totals, exact unrounded event/genre
means, all original denominators and both coarse-to-OLS regressions before gating.
Missing/invalid metric counts cannot default to success.
A complete valid musical failure yields REJECTED; technical failures yield
INVALID/INCOMPLETE without a musical verdict. Confidence stays uncalibrated and
all transfer/native/product/P4/acceptance flags remain false.

## Resource handling and one implementation factoring choice

[Neutral support](../../tools/apta_dense_runner_support.py) supplies Linux
supervision and file binding without evaluator dependencies:
- The parent supervises the entire phase process group, including per-source
  workers and Git/runtime/binding/recheck work. It samples RSS/HWM and its own
  RSS, enforces wall time and counts all attempt outputs, including logs,
  requests, candidate and evaluation evidence.
- Source workers have the registered 60-s bound and an additional hard 256-MiB
  address-space ceiling applicable to their pure-Python numerical path.
  NumPy evaluation uses RSS monitoring rather than an inappropriate low virtual-
  address ceiling. Own/waited-child peak RSS is checked as well.
- Every phase retains its own 1800-s outer watchdog. Polling is 20 ms; RSS is
  per-process Linux KiB, not a sum or a whole-device memory measurement.
- Nonzero worker exit, timeout, RSS/output excess and leftover descendants are
  invalid. Process-group termination prevents a failed phase leaving live work.
- Completion records reserve their exact canonical JSON byte size before writing;
  an overflowing resource/completion report cannot itself turn a failure into PASS.

The plan requested reuse of existing I/O helpers. The existing history helper
imports an ASAP validation module, which would cross the construction import
boundary. This implementation instead factors the small mechanical hash/bind/
exclusive-JSON operations into neutral support, without modifying legacy tools.
Tests prove byte/hash equality with the canonical helper on valid generated
records. No scoring/projection algorithm is copied or changed.
The decoder and protocol bytes remain identical to DD-02/DD-01 respectively.

## Checks executed and their limits

**31 new runner tests pass**, including actual source-worker subprocesses, absent/
changed labels, complete and partial freezes, payload/candidate/source/runtime
identity checks, canonical control reproduction, unknown successors, every
musical gate, preflight refusal, timeout, byte limits, self/child/grandchild RSS,
worker failure, orphan cleanup and final-report overflow.

Fixtures contain one or two generated sources. Expected hashes/counts and a clean
test revision are injected only by the unit harness; the public CLI has no
synthetic-count or hash override. Some unit tests use a metric stand-in to isolate
state transitions. A separate test runs construction/evaluation with the unchanged
real `mir_eval` helper on generated labels in the pinned runtime. No test is skipped
in the recorded verification. The synthetic REJECTED result tests gate mechanics;
it is not the verdict of the registered real cohort.

All **212 Python tests** pass: 31 runner, 33 pure decoder and 148 earlier controls.
Unchanged Rust controls pass **37 per debug/release profile**, plus Rustfmt,
focused warnings-as-errors Clippy and no-default library check.
The new suite is wired into [rust/check.py](../../rust/check.py); the full combined
C/Rust/sanitizer/consumer/platform/P4 matrix is not rerun or claimed.

External script/log:
`D:/AI/LIBAPTA/outputs/dense-decoder-dd03-20261023/`.

```powershell
wsl -d Ubuntu -u daniel --exec bash /mnt/d/AI/LIBAPTA/outputs/dense-decoder-dd03-20261023/verify.sh
```

The runner suite uses the existing pinned neural-reference venv for the generated
real-metric helper test, without loading models or installing packages.
The remaining suites use system Python; Cargo uses the established external
target/toolchain. Tested source/test/log identities are in the public JSON.
Generated checks precede this checkpoint commit; component hashes bind the exact
tested working bytes without claiming an uncommitted full-source musical run.

## Next DD-04

Implement the independent scalar recurrence/indexing/traceback verifier, compare
both per-meter optima, localization and ownership on generated fixtures, and
complete full-size phase/CLI operational and failure preflight. Bind the completed
component hashes and truthful PASS flags in the JSON CLI gate only after those
checks pass. Commit/push DD-04 before the one real DD-05 construct/evaluate attempt.

Independent transfer, whole-pipeline P4 feasibility and product budgets, native
representation/inference port, consumer adoption, calibrated confidence and final
musical/physical qualification remain separate open gates.
