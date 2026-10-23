# Label-blind learned-event piecewise timing

Registered 2026-10-23 at clean `rust-rewrite`
`b19d3451dea6acabff88926fb378cc5b43847cd4`, before implementation or measurement.
The [fixed protocol](LEARNED-PIECEWISE-PROTOCOL.json) owns this bounded attempt.
Reference-construction research remains closed after the
[terminal result](QUANTIZED-GUIDED-RESULT-20261022.md).

Hypothesis: variable tempo within an S6 window needs multiple periods rather
than a wider constant fit. Fit deterministic OLS pieces to learned events only,
with at least four events per piece, one-hop residual (960 frames), original
ordinal/window/cell ownership, natural-count geometry and a two-hop actual-seam
guard. Minimize piece count then squared residual then longest-first ends.
At most eight pieces per source; over-cap sources explicitly abstain, without
truncation. Failed adjacent-window seams invalidate both windows simultaneously.
No alternate anchor, tempo-family correction, reference path or fallback search.

Construction runs in a separate stdlib module that receives learned events,
plain windows, total frames and declared gaps only. Freeze all 100 candidates
before the evaluator loads reference labels. The immutable original OLS tuples,
scores and both original regressions remain controls. Score nominal periods
and actual successors separately with the existing common point diagnostic;
evaluate same-grid meter/downbeat using the unchanged frozen meter logic.

Expected cost is below 180 s / 256 MiB host RSS. Worst-case declared event caps
do not promise P4 feasibility; the cap is an operational veto, not a fallback.
Require joint/complete-sequence improvement with zero old period/phase/joint,
coarse joint, meter and downbeat breaks. Any failure rejects this exact attempt
without radius, residual, context, cap or minimum-piece-length rescue. New audio,
model inference, untouched transfer/holdouts and fresh acceptance remain closed.

Commit/push registration, then implement and synthetically verify exact fits,
piecewise variable tempo, residual/count/seam failures, missing support, native
Q32 quantization, small exhaustive partition solutions and label isolation.
Commit/push preflight before one exact-source development run. A passing result
only authorizes separately registered independent development transfer; no
production/native integration, confidence calibration or physical qualification.
