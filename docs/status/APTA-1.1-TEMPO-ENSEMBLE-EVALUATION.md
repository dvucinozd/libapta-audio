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

### Current-head historical regression run — 2026-08-25

Source revision `9a878bf3dd241bff53fe84cf45a4beb6f71af349` (native gates: 114
CTest tests passing in both default and trace-enabled configurations,
warnings as errors). Corpus executable SHA-256
`4201ab843842a8a74c286522450de6b444285043e135f62fd658942d96ee892c`.

Full-corpus aggregate (all 188 rows, no subset selection):

| Mode | Within 1% | Octave | Other | Errors >=75 | Octave errors >=75 |
|---|---:|---:|---:|---:|---:|
| S4 | 143 (76.1%) | 4 | 41 | 3 | 3 |
| S4 + S6 endorsement | 166 (88.3%) | 2 | 20 | 4 | 2 |
| S6 | 164 (87.2%) | 8 | 16 | 4 | 2 |

Row-level comparison against the retained Phase-5 baseline partitions:

- development S4 (140 shared tracks): exact 107 -> 107; no selection fixed or
  broken; high-confidence octave errors unchanged at 2;
- development endorsed (140 shared tracks): exact 124 -> 124; one selection
  fixed and one broken (relation class OTHER), a net zero change;
- no historical regression gate is triggered.

This satisfies the historical-regression requirement for the accumulated
`1.1.0` selection changes (dominant S6 tempo family, close-tempo arbitration
and the common-time prior) against the retained baseline. It does not qualify
the ensemble: fresh-set acceptance remains open and requires newly acquired
labelled material frozen before candidate results are inspected.

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

### Frozen fresh set — Ballroom holdout, owner-approved — 2026-08-25

Following the trace analysis that deprioritized meter/downbeat work upstream,
the owner approved spending the previously untouched Ballroom holdout split as
the Task-5 fresh tempo validation set. Recorded before the first candidate
run:

- track count: **40** (the protocol minimum comparison floor);
- manifest SHA-256: `0dc703b20d0603f9e6685bb0cfa83367414a6de92470eb1c4fee5f48097928a7`;
- reference-labels SHA-256: `5aa77e0b23233a38480d43a77b67f63f2407c91ac444073efce1bf8f0214d323`;
- freeze date: prepared directory frozen at validation-set creation (2026-08-25 handoff);
- reference source: Ballroom Rhythm Dataset manually corrected beat/bar
  annotations (CC BY-NC-SA 4.0); tempo truth derived deterministically as the
  median annotated inter-beat interval per track at 48 kHz;
- verification procedure: dataset annotations are hand-corrected beat
  positions; no APTA output was read during derivation;
- tempo-range distribution: <90 BPM: 11, 90-119: 8, 120-139: 7, 140-159: 1,
  160-179: 6, >=180: 7 (broad by design: waltz through fast jive);
- meter composition: 20 tracks labelled 3/4, 20 labelled 4/4.

Comparison configuration, fixed now: baseline is the same HEAD binary run in
S4-only mode (the unchanged Phase-4/1.0.1 production selection); candidate is
the identical binary in `--request-global` ensemble mode. One run per side;
results are reported for all rows with no post-hoc subsetting.

## Fresh-set result — 2026-08-25

Both configurations ran once over the exact frozen set, same HEAD binary,
source revision `96bf8393dd241bff53fe84cf45a4beb6f71af349` predecessor tree at
`fd67566` plus no code delta (binary `apta-tempo-corpus` SHA-256
`4201ab843842a8a74c286522450de6b444285043e135f62fd658942d96ee892c`).

| Metric | Baseline (S4-only) | Candidate (ensemble) |
|---|---:|---:|
| Exact within 1% | 9/40 | 10/40 |
| Octave errors | 10 | 10 |
| Other errors | 21 | 20 |
| Errors with confidence >=75 | 1 | 1 |
| Metrical-family errors >=75 | 1 | 1 |
| Selections fixed / broken | — | 1 / 0 |

Frozen acceptance gates:

- PASS — no exact-accuracy regression (9 -> 10);
- PASS — no promotion regression (0 broken);
- PASS — no high-confidence safety regression (1 -> 1);
- PASS — no metrical safety regression (1 -> 1);
- PASS — demonstrated benefit (exact count improved).

**Classification: diagnostic evidence, not formal Task-5 acceptance.** The
frozen protocol requires >=48 fresh labelled tracks; this set provides 40, so
by its own rule the run cannot close the acceptance blocker regardless of the
outcome. The result is nevertheless meaningful: on an independent, previously
untouched, hand-annotated set spanning 60-200 BPM, the accumulated `1.1.0`
ensemble improved the production selection with zero broken selections and no
safety regressions.

This run also consumes the Ballroom holdout's untouched status: any future
meter/downbeat holdout evidence must come from different material.

### Formal acceptance set frozen — owner-supplied DJ material — 2026-08-25

A second, formally qualifying fresh set was then frozen from owner-supplied
material (48 tracks, DJ domain, none present in any prior pool by content
hash or normalized title):

- track count: **48** (protocol minimum satisfied);
- audio: 48 kHz stereo signed 16-bit WAV, metadata stripped;
- reference source: owner's Rekordbox USB export beat grids (PQTZ modal
  tempo per track), matched to canonical audio by exact content hash;
- verification procedure: owner approved Rekordbox analysis as the tempo
  reference; every retained grid is stable (modal share >= 0.70); derivation
  deterministic; no APTA output was read before freezing;
- tempo range: 106.0-149.99 BPM (90-119: 5, 120-139: 38, 140-159: 5);
- reference-labels SHA-256:
  `f4e3c377553cbd778c18ed2b18b014927003a1464b0ad64b2c171ea44820dec3`;
- freeze-manifest SHA-256:
  `a21b3214fd3ba15d8bb5f6a99c8aea3d0fced423b36f795d7cb1a1c1fdec24c3`.

Comparison configuration identical to the diagnostic run: same HEAD binary,
S4-only mode as baseline versus `--request-global` ensemble mode as candidate,
one run each, all rows reported.

Closing this run's results section will follow after execution; thresholds
and gates are already fixed above and are not renegotiable afterwards.

Closing the Task-5 acceptance blocker formally requires one further run on a
newly frozen set of at least 48 tracks (for example owner-supplied material
with independently created tempo labels), evaluated against the same gates.

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
