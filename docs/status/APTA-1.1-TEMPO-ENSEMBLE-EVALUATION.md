# APTA 1.1 tempo/grid ensemble evaluation protocol

**Status:** pre-registered before corpus tuning of the Task-5 implementation

## Purpose

This document freezes the acceptance boundary for the first APTA 1.1
tempo/grid ensemble candidate. It exists specifically because the earlier
Phase-5 threshold-only candidate improved the development partition and then
failed on its one-time hold-out.

The old 188-track Rekordbox corpus is no longer an unseen validation corpus.
Its 48-row Phase-5 hold-out has been inspected and must not be reused to tune a
threshold or represented as independent acceptance evidence.

## Candidate boundary

The candidate implementation is the relation-aware S4/S6 ensemble introduced
on the `1.1.0` development line. Its policy is fixed for the first evaluation:

1. S6 may identify a different metrical family only when the current S4 answer
   and the S6 nominal tempo are a recognised half/double, two-thirds/three-half,
   third/triple or quarter/quadruple relation.
2. S6 chooses the metrical region; the published candidate BPM is re-estimated
   and sub-bin refined from S4's fine onset evidence rather than copied from the
   coarser S6 value.
3. The refined proposal must retain at least the existing S4 endorsement score
   floor of 55000 on the same local evidence.
4. The proposed fine grid must have strictly greater onset grid-fit than the
   currently selected fine grid.
5. A promoted grid is re-phased at the promoted period.
6. One complete ensemble evaluation consumes one bounded cooperative scheduler
   step. No unbudgeted corpus-length scan is permitted.
7. No new public API, ABI, container version or calibrated-confidence claim is
   part of this task.

These rules must not be changed after fresh validation labels are opened. A
policy change starts a new candidate and requires a new frozen evaluation
record.

## Subsequent development candidate — 2026-08-23

The automated 60-track DJ diagnostic exposed a second selection failure after
the first relation-aware candidate was frozen: on 12 tracks the automated
reference period already existed at TEMP rank 1 or 2, but the unrelated rank-0
candidate remained selected. Inspection also showed that handing only the
single longest S6 segment to S4 was fragile; a long low-confidence tail could
override several mutually agreeing global segments.

The subsequent development candidate therefore adds two bounded rules:

1. S6 chooses its representative tempo family by confidence-weighted total
   applicability duration across its at most eight segments; segments within
   one percent vote together and ties retain the earliest representative.
2. For a non-metrical proposal, S6 may reorder only an existing S4 candidate,
   only when global confidence strictly exceeds local confidence, the existing
   55000 score floor is retained and the proposed fine-grid fit is strictly
   better. It may not introduce an S6-only close candidate.

These rules were designed after inspecting the automated diagnostic corpus.
That corpus is consequently development evidence for this candidate and can
never be represented as its fresh validation set. A future qualifying run must
use untouched labels frozen before this policy is evaluated.

## Native regression gates

Corpus evaluation is not acceptance evidence unless the exact candidate first
passes all applicable native gates:

- default configured build and CTest with warnings-as-errors;
- core-only build and CTest;
- ASan/UBSan build and CTest;
- shared-library ABI/export checks;
- S4 relation, endorsement, bounded/cooperative-refresh and serialization tests;
- S6 global-grid, bounded/cooperative-refresh and serialization tests;
- `git diff --check`;
- ESP-IDF compilation for the supported 5.5.4/6.0.2 integration matrix when the
  normal CI environment is available.

Any native regression rejects the candidate before accuracy is considered.

## Historical 188-track regression corpus

The existing 188 rows may still be run in full because they are useful known
regressions, but they are development evidence only. Report all rows and do not
select a subset after seeing the candidate result.

Required aggregate comparison against the retained Phase-5 baseline:

- S4 exact count and endorsed exact count;
- octave and other error counts;
- errors with confidence >=75;
- octave errors with confidence >=75;
- endorsement selections changed, fixed and broken;
- relation class of every aggregate change;
- number of cases where the selected tempo was recovered from outside the old
  public S4 top-three list.

A historical regression blocks the candidate when endorsed exact accuracy drops
below the previous full-corpus baseline or when high-confidence octave errors
increase. Passing this gate does not qualify the candidate by itself.

## Fresh validation set

Stable Task-5 acceptance requires newly acquired labelled material that was not
used to design the ensemble rule. Collection must be frozen before candidate
results are inspected.

The validation manifest must contain only opaque IDs and aggregate metadata in
tracked evidence. Audio paths, titles and artists remain private. Before the
first candidate run, record:

- track count;
- manifest hash;
- reference-label hash;
- acquisition/freeze date;
- reference source and verification procedure;
- tempo-range distribution in aggregate bins.

Fewer than 48 fresh labelled tracks is diagnostic evidence only, matching the
size of the previous hold-out as the minimum comparison floor. More is strongly
preferred.

## Fresh-set acceptance criteria

Compare the unchanged APTA 1.0.1/Phase-4 production selection and the Task-5
candidate on exactly the same frozen fresh set. The candidate is accepted only
if all of these conditions hold:

1. **No exact-accuracy regression:** endorsed within-1% count is not lower than
   baseline.
2. **No promotion regression:** broken endorsements do not increase.
3. **No high-confidence safety regression:** errors with confidence >=75 do not
   increase.
4. **No metrical safety regression:** octave/metrial-family errors with
   confidence >=75 do not increase.
5. **Demonstrated benefit:** at least one of the following improves without
   violating conditions 1-4: endorsed within-1% count, broken endorsements,
   high-confidence error count, or high-confidence metrical-error count.
6. **Bounded behavior retained:** no test or profile shows an allocation growth
   caused by the ensemble, and one ensemble evaluation remains one scheduler
   step bounded by the existing S4 evidence capacity.

If exact counts tie and only confidence safety improves, the candidate may be
accepted for Task 5 because removing confidently wrong metrical answers is an
explicit goal. If none of the benefit metrics improves, the added complexity is
not justified and the candidate is rejected.

## Reporting discipline

The evaluation report must contain baseline and candidate aggregate tables even
when the candidate fails. Do not suppress regressions, re-label the validation
set as development, or tune a threshold against the failed fresh set and rerun
it as if still held out.

A rejected candidate remains documented as a negative result. A subsequent
candidate requires a new versioned policy record and fresh independent evidence
or a fully pre-registered all-fold protocol whose every fold is reported.
