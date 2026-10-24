# P3 matched context-anchor evaluation registration

Date: 2026-10-24. **REGISTRATION ONLY; evaluator, runner and campaign disabled.**
Baseline: `499db930a01948b9ec73ed2deb8887872b6556da`, `rust-rewrite`.
Parents: unchanged [P0 projection contract](CONTEXT-ANCHOR-PROJECTION-PROTOCOL.md),
[P1 arithmetic implementation](CONTEXT-ANCHOR-PROJECTION-P1-20261024.md),
[P2 interoperability](CONTEXT-ANCHOR-PROJECTION-P2-20261024.md) and
[C3 aggregate authority](COUPLED-TIME-METADATA-C3-20261024.json).
The [machine registration](CONTEXT-ANCHOR-MATCHED-EVALUATION-PROTOCOL.json)
binds these authorities and canonical scorer bytes. The
[ledger schema](CONTEXT-ANCHOR-MATCHED-EVALUATION-SCHEMA.json) and
[public arithmetic examples](CONTEXT-ANCHOR-MATCHED-EVALUATION-EXAMPLES.json)
define accounting; they contain no campaign results or authorization.

## One axis and fixed scope

Hypothesis: applying the same source-valid, owned-point P1 representation to
both original neural events and the already fixed coupled candidate permits a
fair matched comparison. Additional supported windows are representation support,
not evidence that the candidate became more accurate. Keep candidate event bytes,
downbeat topology, fit parameters, tolerances and confidence unchanged.

Future complete generated scope is exactly the exposed C3 bundle:100 sources,
600 fixed windows,400 complete and200 incomplete reference contexts. Bind its
existing input/candidate/completion/support/result hashes through C3 and verify
each leaf later. Reuse its exact original and candidate event bytes; do not run
the solver again, synthesize replacements or read a different saved cohort.
P3 reads only tracked code/aggregate authorities and public accounting examples,
not that bundle's cases, labels, private paths or runtime closure. No source
eligibility, private case selection or correctness result is inspected here.

The GTZAN real100/600 scope with401/199 support is excluded. No real data,
inference, training, transfer, formal holdout, native producer or product change
is registered. Old C3 operational PASS / scientific REJECTED14/16 stays frozen.

## Four comparison cells

| Arm | Legacy representation | Context representation |
|---|---|---|
| Original event control | unchanged canonical project/nominal points | unchanged P1 project_window |
| Fixed coupled event candidate | unchanged canonical project/nominal points | the identical P1 project_window |

Use original source frames and original per-arm downbeat ordinal indices. Both
arms receive the same source geometry and fixed window list. There are1200 new
context projections, independently verified before labels, and2400 evaluated
arm/representation cells. No arm-specific rescue, meter borrowing, anchor shift,
reference-selected window or post-result parameter choice is permitted.
Legacy projection is invoked only in the evaluation process: its module imports
scorer dependencies and must never enter the label-blind P1 projection worker.
Legacy outputs/metrics must reproduce the saved600-window control exactly in a
separate frozen-control channel. No old file or verdict is overwritten.

## Metrics and reference support

For context nominal grid diagnostics pass only `(position_q32, period_q32)` for
the P1-owned points into unchanged `apta_1_1_event_representation.score_points`.
For legacy use unchanged `points_for_control`. Preserve its nearest-reference
tie order, local-period routine, inclusive reference span,0.10 phase and0.01
period tolerances, duplicate/one-to-one checks and unknown-successor semantics.
Exact integer model coordinates remain intact in records; canonical evaluation's
existing binary64 operations are evaluation-only, never an intermediate OLS fit.
Use canonical keys `phase_windows`, `period_windows`, `joint_windows` and
`complete_sequence_windows`. Grid joint means phase AND period, without meter.

A new meter-only adapter will reproduce exactly the four canonical meter fields
from `apta_1_1_native_annotated_development.score_window`:
`meter_emitted`, `meter_correct`, `downbeat_scored`, `downbeat_correct`.
For a published owned meter, numerator/denominator match the reference meter;
`frame = downbeat_frame + downbeat_fraction_q32 / 2**32`. Score downbeat only if
`truth.beat_frames[0] <= frame <= truth.beat_frames[-1]`; use the nearest reference
downbeat in original order, canonical `_local_period(truth.beat_frames, nearest)`
and canonical0.10 tolerance. No meter means four explicit zero fields.
Verify this adapter against the unchanged canonical meter fields on public
fixtures, including context anchors; do not consume that routine's grid fields
or generate unowned context points to score the new grid.

Truth must satisfy the parent reference contract, including a nonempty ordered
beat/downbeat list and known meter; unknown/malformed labels are technical errors.
Reference context is independently `reference_beats[0] <= F and E-1 <=
reference_beats[-1]`. Use unchanged `apta_reference_support.reference_support`
only in evaluation. Require complete+incomplete=windows per window/source/group/
cohort and the fixed400/200 totals. Unknown context is not a false/zero counter.

Raw beat/downbeat F1 and CMLt stay unchanged, macro over all100 sources, with the
canonical5s trim,70ms F1, empty prediction0 and1e-12 comparison epsilon. They are
computed once per raw arm, not once per representation. Raw actual intervals
use unchanged `points_for_events` and `score_points`: global next observed event,
including across a window boundary, and final successor None. Record all-point
known/unknown successor availability separately from the canonical
`unknown_successor_points` counter, which increments only inside reference span.
These raw-event controls are representation-invariant. Never stitch independently
fitted projected windows to invent an actual successor or a final-source grid.
Keep the older stitched OLS-actual diagnostic separately under its frozen name.

## Denominators and paired ledger

Keep every one of600 windows, including all unsupported outcomes. In each cell:

- G: accepted projection with nonempty owned grid points.
- M: G and an estimated meter bound to an owned point of that grid.
- D: M and that downbeat lies inside the inclusive reference beat span.

For legacy, derive exact Q32 ownership of its emitted meter separately. An old
meter outside `[F*2**32,E*2**32)` is `legacy_native_unowned_meter_conflicts`, not M
or D support in the matched channel. Preserve its old canonical score unchanged
in the frozen-control channel. A newly owned replacement is support gain, never
a paired correctness fix on the old shared domain.

Comparison order is fixed: `candidate_vs_original_legacy` and
`candidate_vs_original_context` have original on the left/coupled on the right;
`context_vs_legacy_original` and `context_vs_legacy_coupled` have legacy on the
left/context on the right. For every comparison and family, emit all/complete/
incomplete strata at source, group and cohort levels. Grid families phase,
period, joint and complete_sequence share G; meter uses M; downbeat uses D.
Also publish each cell's canonical all-window counters and complete/incomplete
subtotals, so conditional accuracy cannot hide support loss.

Each ledger partitions its stratum into both_supported, left_only_supported,
right_only_supported, neither_supported; their sum equals windows. On the shared
domain, correct_both + fixes_on_both + breaks_on_both + incorrect_both equals
both_supported. Left/right shared-correct counts are respectively
correct_both+breaks and correct_both+fixes. One-sided correctness is bounded by
its own support count and never added to fixes/breaks. Conditional rates are
exact numerator/denominator objects; both are null when both_supported=0.
The legacy conflict counter counts windows with any such conflict once per
comparison, even if both legacy cells conflict; never sum meter objects as windows.
No missing field, boolean, negative counter, guessed zero or binary64 count is
allowed. Finite duplicate-free JSON and exact lossless integer parsing precede
schema and conservation checks. Examples' zero protocol hash is a template;
substitute the actual digest before validation, never issue it as a result.

Primary conditional grid comparisons use both-G AND complete reference context.
Meter correctness uses both-M; downbeat phase uses both-D, including an incomplete
window whose published downbeat itself has reference support. Always show the
remaining strata alongside the primary denominator. Do not pool their rates.

## Decision and stop policy

Technical eligibility requires exact source/event/window identities, both-arm
projection oracle parity, owned meter/ordinal binding, all denominator identities,
frozen legacy replay, unchanged raw metrics, no partial cohort and resource PASS.
Any failure makes the new attempt INVALID; preserve partials and stop. No subset
publication, automatic retry, threshold rescue or modification of an old attempt.

Within each raw arm, legacy-owned accepted grid/meter tuples must remain exactly
identical under context projection and G/M/D support must not decrease. A failure
is a representation implementation veto, not a musical regression to tune away.
For context candidate versus original, report a conservative generated pair
screen: zero G/M/D support losses; zero phase/period/joint/complete-sequence
breaks on complete shared-G; zero meter breaks on shared-M; zero downbeat breaks
on shared-D; no raw actual phase/period/joint/complete-sequence breaks; no event
macro decrease beyond the parent1e-12 epsilon. Failure is
`PAIR_REJECTED_ON_GENERATED_DIAGNOSTICS`; otherwise report only
`PAIR_NOT_REJECTED_ON_GENERATED_DIAGNOSTICS`. It is not musical acceptance or
evidence of independent transfer. Report every fix/break and support transition.
Old16 gates and14/16 REJECTED are never relabeled or evaluated on the new domain.
No absolute real-cohort target (such as188 nominal windows) is imported here.

## Implementation sequence, isolation and resources

P4 implements the separately named pure ledger/meter adapter and independent
oracle on public generated fixtures only. P5 prepares a generated-only runner,
strict records, source/runtime/input bindings and operational preflight. Future
paths are listed exactly in the machine registration; no old runner dispatch or
environment override may select the new implementation.

P5 separates PROJECT -> VERIFY -> EVALUATE processes. PROJECT receives only
per-source original/candidate integer events and geometry, never references,
saved scores, source selection hints or construction callbacks. No solver or
legacy scorer import occurs there. Freeze all1200 projections before EVALUATE
opens labels; VERIFY independently checks every projection before allowing that
boundary. Bind source, protocol, P1 code, new code, exact leaf inputs and the
actual generated runtime closure before execution. A new component-bound
generated preflight is required; no P0/P1/P2/C3 receipt is a dispatch token.

P6 may propose one fresh, clean-pushed generated comparison only after P4/P5
pass and their receipt identifies the exact source/runtime/input closure. It
reads fixed C3 events/results, does not reconstruct100 candidates, does not
resume an old attempt and never activates real dispatch. No campaign is enabled
by P3. Historical real-runtime metadata remains NOT_AUDITED and its separately
registered H0 audit/real-scope decision remains required before any real proposal.

Limits stay60s/source,1800s/phase,262144 KiB/process,67108864 external bytes for
the whole new attempt including staged inputs/logs/receipts. Sources are bounded
by3072 events/owned output points,8 nonoverlapping windows,1440000 source frames
and48000Hz. Preserve the parent immutable generated runtime and bind a separately
qualified derived closure for new files; version strings alone never suffice.
P3 registration checks use only public ledger arithmetic, schema/hash/link checks
under the same host caps. No scorer/projector, saved case, runtime image, corpus,
current consumer, P4 device or full native matrix is executed in P3.

Independence stays UNRESOLVED, confidence unqualified, real authorization absent.
There is no native activation, acceptance, release or VERSION1.0.1 change.
