# P7: saved meter support boundary diagnosis

Date: 2026-10-25. Audit source `91087aba4a00cc09917542f8b000b7d5b12d369b`,
clean pushed `rust-rewrite`. **READ_ONLY_DIAGNOSIS_PASS**.
[Aggregate evidence](METER-CONTEXT-P7-DIAGNOSIS-20261025.json).

All25 P6 meter/downbeat support losses have the same saved-record cause. One
observed downbeat per affected source moves from window1 to window0 after coupled
timing. The source downbeat ordinals/count remain unchanged. Owned downbeat counts
change3 ->2 in complete-reference window1 and2 ->3 in incomplete-reference window0.
The frozen P1 meter helper requires three observed in-window downbeats to establish
two complete bars. Its saved status changes OWNED -> NO_ELIGIBLE_BARS in window1
and the reverse in window0. Downbeat support depends on emitted meter, so it also
loses25 windows; the new meter windows provide no new reference-supported downbeat.

| Transition | Windows | Owned raw downbeats | P1 meter status | Reference context |
| --- | ---: | --- | --- | --- |
| Meter loss | 25 | 3 ->2 | OWNED ->NO_ELIGIBLE_BARS | complete |
| Downbeat loss | 25 | 3 ->2 | OWNED ->NO_ELIGIBLE_BARS | complete |
| Meter gain | 25 | 2 ->3 | NO_ELIGIBLE_BARS ->OWNED | incomplete |
| Downbeat gain | 0 | — | — | — |

The saved aggregate assigns all losses to the public `boundary` family; constant,
jitter and ramp have none. This identifies window-local evidence availability
as the immediate mechanism. It does not establish that arbitrary context evidence
is safe, that any particular replacement passes, or that real music improves.

The audit binds305 saved files through the P6 completion/manifest chain, verifies
all73 qualified components and rehashes all414 attempt files identically before/
after. It reads neither labels nor audio, and invokes no projector, scorer,
constructor or solver. The supervised audit passes in12.864s with55368KiB maximum
observed process RSS, within the unchanged host caps. Runtime/script hashes are
retained externally under `outputs/meter-context-p7-20261025`.

Next hypothesis: preserve P1 grid/fit and every existing owned meter exactly,
while permitting bounded neighboring observed bars to establish meter when the
window has insufficient local evidence. Publication must still identify an owned
point of the same grid. The new contract must specify context bounds, lookahead,
conflicting meter/phase evidence, gap handling, exact arithmetic, abstention and
deterministic witness selection before implementation. Use a separately named
module and public arithmetic examples; P6 remains rejected and cannot resume.

The [current product budget audit](METER-CONTEXT-P4-BUDGET-20261025.md) remains
UNKNOWN for complete neural analysis. No real dispatch, transfer, native product
activation, physical P4, confidence qualification or release follows from P7.
