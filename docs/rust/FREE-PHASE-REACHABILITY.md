# Free-phase reference timing reachability

Registered 2026-10-22 at clean `rust-rewrite` baseline
`5ba400185e03fb4eb1dc8fc26795ac9c2f883023`, before implementation or measurements.
The [protocol](FREE-PHASE-REACHABILITY-PROTOCOL.json) freezes the hypothesis,
inputs, exact inequalities, solver/checker and stop conditions. This follows the
[47/100 anchored-family result](SEGMENTED-REFERENCE-RESULT-20261022.md).

The question is whether the other 53 full reference paths remain impossible
when fixed anchors and piecewise/quantized periods are relaxed, while keeping
the exact reference envelope, original ordinal/window/ownership bounds,
guarded 0.10-beat phase and guarded 1% actual interval constraints. An independent
continuous nominal period per event also preserves necessary natural-count
geometry and the original tempo range. Its nominal period can differ from the
actual successor interval; this relaxation is deliberately necessary only.

Every old native witness embeds in this relaxation: interior previous/next
points lie in adjacent ownership cells; segment endpoints satisfy the old
natural-count inequalities. If an old witness is excluded, stop as a bug.

An exact rational interval recurrence decides continuous reachability. Strict
geometry endpoints stay strict. Backward reconstruction produces one complete
path or a private first-empty-prefix certificate. Independently check the same
raw inequalities using difference constraints with symbolic strictness; no
floating epsilon is permitted. Unit tests include strict singleton and strict
cycle contradictions, moving phase, cumulative drift, missing context, source/
window endpoints, nominal tempo limits, gaps and corrupted witnesses.

No audio or new inference is run. Bind and retain all 100 original sources,
600 windows, 401 complete contexts, old anchored plans and old scoring records.
Report all 106 previously constant-period-incompatible windows. Coordinates,
identifiers, private records and negative certificates stay outside Git.

Expected cost: linear interval solving per source, with an independent bounded
difference-constraint checker and prior-plan replay inside 180 s / 256 MiB.
No expected detector-accuracy gain is asserted. Stop on any changed baseline,
input hash, old witness exclusion, solver/checker disagreement, invalid witness
or resource cap; do not retune this protocol after results.

Commit/push registration, then implement/test and commit/push preflight, then
run once from clean exact source. A continuous path cannot be serialized as a
native result by this diagnostic and is not meter/product/P4 qualification.
If additional paths are reachable, the next separately registered question is
free-anchor **quantized** native segmented construction within the existing
eight-segment limit. Previous failures, production pins/defaults, core validators,
confidence, formal holdouts and fresh acceptance gates remain unchanged.
