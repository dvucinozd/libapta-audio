# Learned-event piecewise timing — implementation and preflight

Date: 2026-10-23. Registration was committed/pushed at
`9c60007d4ed13ae36852fdbad82453178fe0b9a9`, before implementation or measurement.
[Protocol](LEARNED-PIECEWISE-PROTOCOL.json) SHA-256:
`007b1c0cb834c5768947e2df9f1431e6a8483a9ea1a51eace42d0f76c7f4e7fc`.

The pure `tools/apta_learned_piecewise.py` constructor imports only stdlib
`fractions`. It contains no file access, scoring, reference-data or previous
candidate imports. Its interface accepts learned beat/downbeat arrays, total
frames, plain source-window ranges and declared PCM gaps. Extra fields in
window geometry are rejected. The evaluator
`tools/apta_1_1_learned_piecewise.py` hashes input labels but does not parse them
until every source candidate has been written and its candidate manifest frozen.
Scoring then replays construction and requires identical candidate contents.
All candidate, freeze, source and input hashes are rechecked at the end.

The constructor performs exact rational OLS per contiguous learned-event run,
with one deterministic integer anchor/tempo and Q32 published period. Ownership,
one-hop residual and natural-count checks reject invalid pieces. Dynamic
programming on compatible predecessor pieces minimizes count, squared Q32
residual and longest-first ends, in that order. At source seams both affected
windows are invalidated simultaneously without retries. The eight-piece source
cap never truncates: full rejected plans remain as private diagnostics. Known
PCM gaps and analysis abstention remain separate. Meter uses only learned bar
ordinals and maps to the actual fitted coordinate. Independent validation
checks piece geometry, counts, ordinals, periods, residuals and meter binding.

The original scorer remains byte-unchanged. Old OLS tuples and scores must
reproduce. Existing `score_points` separately evaluates own-period and actual-
successor paths for both candidate and control. Missing or final successors
remain unknown; the old OLS nominal metric and new actual-successor metric are
never treated as interchangeable. Frozen meter/downbeat logic is invoked using
a diagnostic proxy; every proxy grid score is discarded. Only its four meter/
downbeat fields are retained. No new meter threshold or correctness definition.

Verification passes **130 Python tests**, including **18 new tests** for exact
fits, generated variable tempo requiring multiple pieces, fractional Q32 and
independent OLS equations, exhaustive small partition objectives, residual/
natural-count failures, two-sided seam invalidation, no-prefix capacity refusal,
gaps, unknown successors, own-grid meter, corruption, translation, input caps,
protocol constants and zero-regression gates. Label isolation tests inspect
imports, forbid file reads during construction, reject reference-bearing
geometry and show changed scoring labels cannot change constructed output.

The unchanged native controls pass **37 Rust tests per debug/release profile**,
with Rustfmt, focused warnings-as-errors Clippy and no-default library checks.
They exercise existing format/native behavior, not native construction of this
new candidate. No Rust DSP, validator, limit, consumer, production pin/default
or release changes. Full Rust/C/sanitizer/CI/product/P4 matrices are not rerun
or claimed for this pure host addition.

External script and verification log:
`D:/AI/LIBAPTA/outputs/rust-learned-piecewise-20261023/`.
Commit/push this preflight before the one clean exact-source development run.
Failure of any predeclared veto closes this exact candidate without same-cohort
parameter, context, capacity, fallback or reference-derived rescue. No transfer
audio/labels or new inference follows a failed development gate.
