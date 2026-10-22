# Temporal host model — stage B

Date: 2026-10-22. Stage B of the
[registered experiment](S6-TEMPORAL-MODEL-EXPERIMENT.md) is implemented and
synthetically verified. No real-audio selector scoring, native selector,
publication, production adoption or acceptance result follows from this step.
The [protocol JSON](S6-TEMPORAL-MODEL-PROTOCOL.json) remains byte-identical:
`bf8dc41fcce167b3ed9fb23d08d8106121e1c0e74c2d012d253025d893c9bbf2`.
Base revision remains `1876eb34a9169299ece0d908927850e6f6e79670`, plus the
archived dirty stage B source inventory.

## Implemented model

`tools/apta_1_1_native_temporal_model.py` exposes a label-blind `select` kernel
and a causal `select_window` adapter. There is deliberately no corpus-analysis
CLI in this stage. The kernel receives exact native f32 values, source-frame
geometry and an optional shared resource budget. It receives no reference
labels, corpus identity, old grid, meter, source filename or tunable DSP option.
The adapter reads only the target and its two preceding native windows, checks
their completeness/geometry and refuses gaps. Future and older observations
are excluded. Each invocation creates new state; source reset cannot retain a
previous source's path table.

The implementation uses all 128 registered tempo states and two distinct paths
per `(bin,tempo)` cell. Scores are f64 arrays and predecessor links packed u32
indices; no Python object graph is retained per cell. All eligible start and
predecessor paths participate with the registered emission/transition formula.
Identical full paths cannot arise from different final predecessor nodes;
each predecessor cell already retains distinct paths. No extra deduplication,
beam pruning, peak gate or old-candidate fallback is introduced.

Exact score ties compare the complete extended path, including the new node.
Comparing only predecessor tuples would reverse some prefix ties; a separate
regression exercises that case. Terminal selection uses two streaming argmax
passes: the winner over every valid retained terminal, then the best distinct
alternative relative to that winner. This is equivalent to the registered
sorted ordering and preserves same-period phase competitors before applying
the strictly positive margin rule.

Least squares uses integer-rational sums in coordinates relative to the source
context, avoiding cancellation at large absolute frame offsets. It is the same
ordinary least-squares fit, with no robust fit, labels or observation weighting.
Zero-strength path nodes remain beats and retain their ordinal. Residual and
tempo checks use exact integer comparisons; milli-BPM/anchor rounding is half
up, and ambiguity compares the converted Q32 periods and cyclic phases.
Confidence is fixed at 49. No meter/downbeat is fabricated on the new grid.

`SourceBudget` enforces 16 MiB numeric-workspace, 256 MiB RSS and 120-second
per-source caps. Share one budget across all windows of a thirty-second source;
individual windows do not reset it. Resource violations raise an operational
veto, rather than becoming a musical abstention or fallback. The resource
monitor uses host `resource` measurements; run this probe under WSL/Linux.

## Independent synthetic verification

Thirteen focused tests pass with warnings treated as errors. They cover:

- exact two-best scores/paths against a separate exhaustive enumerator at every
  cell, including ties, integer and fractional intervals;
- full-path lexical ordering, bounded interval/tempo transitions, missing beats,
  slow drift and unsupported abrupt-fit rejection;
- exact source coordinates through a `2^55`-frame shift, milli-BPM conversion
  and every represented Q32 beat of an independently specified pulse lattice;
- same-period phase competitors, half/double-tempo distinctions, tie abstention
  and the confidence cap;
- silence, short evidence, nonfinite/non-f32 observations, invalid source scope,
  incomplete/partial context, causal slicing and source reset;
- numeric/RSS/time vetoes and a budget shared across multiple windows.

The full-size synthetic fixture is independently defined as 5625 bins, pulses
of strength 0.125 at `16+272*n`, with one missing pulse at bin 1104. Six target
windows exercise 1024/2048/3072-bin startup, full maximum context and partial
EOF under one source budget. At the third target, the model retains every
specified beat ordinal, including the missing one, and emits source anchor
561152 / 41360 milli-BPM, matching the independently derived pulse lattice.

The same fixture also exposes an unresolved limitation: its first and last
windows emit 82721 milli-BPM, approximately twice the specified pulse tempo.
Four observations per target/eight path nodes do not require eight nonzero
observations; the registered model may infer extra zero-strength beats in a
short/boundary context. This is a retained diagnostic counterexample, not a
reason to change the frozen support rule or a claim of six correct grids.
Correct implementation of the formula does not establish musical accuracy.

The full thirteen-test process takes 20.78 seconds wall (20.34 user / 0.09
system), maximum RSS 38996 KiB. The six-window source uses 20.22 seconds of its
shared budget, with 10080960 bytes peak measured numeric buffers and 38868 KiB
RSS observed inside the model. Maximum table size is 786432 slots, with
4416880 predecessor transitions examined for a full 3072-bin context. These
are host measurements on synthetic input, within the registered host caps;
no physical P4 memory, CPU, stack or PSRAM qualification is implied.

The test runner records all six outcomes, paths and resource counts in an
external synthetic report bound to both model and test-file hashes. It refuses
to overwrite an existing report. No real dense trace or audio is fed to this
model, and no reference label is used to choose a path.

Twenty-three existing Python protocol/scoring checks also pass: seven history,
five dense capture, five frozen event-oracle regressions and six dense-reference
scorer regressions. Those exercise synthetic inputs and the retained native
diagnostic binary; they do not rerun the old oracle on recorded corpora.
Native processing/runtime code, scorer and binary hashes remain unchanged
from stage A. Its 289/328 debug/release and 66 focused sanitizer tests remain
historical unchanged-code evidence; no Rust/C/product/P4 matrix is rerun for
this Python-only model. The test-driver change adds the new suite to
`rust/check.py`.

## Reproduction and retained evidence

```sh
PYTHONWARNINGS=error python3 tests/unit/apta_1_1_native_temporal_model.py -v
```

Set `APTA_TEMPORAL_SYNTHETIC_REPORT` to a new external file to retain the full
synthetic fixture report. This is not a corpus-analysis option.

Private source snapshot and evidence are under
`/home/daniel/.local/share/libapta-audio/rust-rewrite/temporal-model-20261022/`.
Aggregate reports/scripts/logs are under
`D:/AI/LIBAPTA/outputs/rust-s6-temporal-model-20261022/`.
The exact source inventory SHA-256 is
`62e484766fad32d8233e589370b5bdec03b7a129a34c9461019cfa786d6bd052`.
`report.json` records source/config/model/test/native-binary/synthetic hashes,
resource limits and the unrun gates. Existing stage A artifacts remain intact.

## Next gate

Stage C is not run. Freeze the exact candidate/config/trace identities, then
run only the forty registered ASAP development sources first, using the
unchanged dense-reference scorer and every source window. Ballroom development
is permitted only if every frozen ASAP gate passes. Stop on the first failed
corpus; no threshold/support/penalty/context rescue after results. Preserve the
synthetic alias limitation and use it to interpret results, not to retune them.

Keep formal holdouts, the spent DJ set and fresh acceptance audio outside this
experiment. Both external development passes are required before any native
selector implementation. Same-grid meter/downbeat, consumer adoption, physical
P4 measurements and FINAL qualification remain later gates. Changes stay local
on `rust-rewrite`, with no commit/push, version, provider or consumer-pin change.
