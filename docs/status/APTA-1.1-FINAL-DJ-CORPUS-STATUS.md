# APTA 1.1 final DJ corpus status

- **Snapshot date:** 2026-08-23
- **Development branch:** `1.1.0`
- **Qualification source revision:** `9fa2a2e48d88a9732a5130db421b6da75805884b`
- **Current state:** private corpus prepared; manual canonical-WAV verification pending
- **Acceptance claim:** none; the final DJ acceptance blocker remains open

## Completed preparation

A fresh private pool of 324 Rekordbox tracks was inventoried without reading any
APTA candidate output. The selection excluded prior-corpus track identities,
content hashes and normalized titles, as well as duplicate content/titles,
short samples, long mixes, classical material and tracks without a sufficiently
stable Rekordbox grid. Sixty unique tracks were selected from 136 tracks not
present in the prior pool.

The selected BPM distribution is deliberately broad within the available DJ
material:

| BPM band | Tracks |
|---|---:|
| 100–109 | 2 |
| 110–119 | 4 |
| 120–129 | 31 |
| 130–139 | 17 |
| 140–149 | 6 |

All 60 selected sources were copied locally and converted to the canonical
qualification format: 48 kHz, stereo, signed 16-bit little-endian PCM WAV with
source metadata removed. Source-to-canonical hashes and paths remain in a
local-only preparation manifest. The audio, Rekordbox database, private
filenames, staging data and review workbook are not repository artifacts and
must not be committed.

The removable source device is no longer required for the remaining workflow.

## Clean qualification build

The analyzer and inspector were built in Release mode with tests enabled and
warnings treated as errors from a clean detached worktree at the exact source
revision above. The retained local binary hashes are:

- `apta-analyze`: `ea5511364e871c2f73a22da89e05f5bef0cc1cd92ebcd2fdf58f0ab1eed5af79`
- `apta-inspect`: `3ce493a67b739ffdc989182da398218c8aa5576ca4af9a1e20fedd3aad854db1`

No APTA analysis was run against the 60-track corpus before label freeze. This
preserves the protocol boundary between ground-truth preparation and candidate
evaluation.

## Independent pre-review checks

Rekordbox key and grid data were extracted only as provisional review aids. A
separate local automated cross-check, which did not read APTA output, compared
five independent key estimates and a separate rhythm/beat estimate:

- 24 of 60 tracks had at least three of five exact key votes matching the
  provisional Rekordbox key;
- 59 of 60 rhythm estimates were within 1% of the provisional BPM;
- 35 of 60 beat alignments had median error at or below 0.10 beat;
- 12 of 60 provisional downbeat phases had the strongest onset phase.

These diagnostics prioritize human review; they are not truth labels and do
not satisfy the independent-manual-verification requirement.

## Remaining acceptance work

The official acceptance corpus is not frozen yet. Every retained track must be
reviewed against its canonical WAV for musical key, meter, downbeat frame and
beat period. Ambiguous tracks may be excluded, but at least 48 independently
verified tracks must remain.

Only after that review may the workflow:

1. freeze opaque labels and the canonical-WAV manifest;
2. run anonymous `apta-analyze --features all` over the exact frozen corpus;
3. export completed FINAL key, meter and beatgrid results;
4. execute the frozen DJ acceptance evaluator; and
5. publish an acceptance result or failure report bound to exact hashes.

Until those steps finish and all frozen thresholds pass, documentation and
release metadata must continue to report the final DJ corpus blocker as open.

## Automated diagnostic run — 2026-08-23

At the owner's request, the prepared 60-track set was also run with automated
Rekordbox labels and the independent DSP audit as diagnostic references. This
run did **not** use APTA output to create the initial frozen labels, but it did
not include human listening verification and therefore is not official
acceptance evidence.

The diagnostic run failed both operational and accuracy gates:

- the exact 60-track run stopped after 37 completed tracks when one long-form
  input exceeded the desktop analyzer's one-million-iteration guard;
- a temporary four-million-iteration candidate passed the old guard but did
  not complete the same input after more than 45 minutes at full CPU, so the
  guard-only change was reverted as an unproven workaround;
- a post-failure 59-track diagnostic subset completed, but `apta-inspect`
  rejected 6 outputs as corrupt containers;
- among the 53 readable completed/FINAL outputs, exact key accuracy was 24.5%,
  meter accuracy 71.7%, downbeat phase accuracy 11.3%, and beatgrid accuracy
  3.8%;
- high-confidence key and grid error rates were 17.0% and 37.7%, above the 5%
  safety ceiling. Meter and downbeat high-confidence safety passed only because
  no corresponding wrong result reached the high-confidence threshold.

The 53-track accuracy view was derived after observing APTA output-integrity
failures and is explicitly contaminated diagnostic evidence. It cannot replace
the original frozen set or close a release blocker. The private local report
retains exact opaque IDs, hashes, run metadata and failure artifacts; no private
audio, filenames or source mapping is committed.

The next engineering work identified by that run was to reproduce and fix the
long-form non-completion and corrupt-container defects before repeating the
frozen automated diagnostic run. Manual independent ground truth remained a
separate prerequisite for any future official acceptance claim.

## Engineering rerun after operational fixes — 2026-08-23

Revision `d53ee8de6d74177d8eae805b325284ebd3ac0d6d` fixes both operational
defects found above. S6 now performs at most one end-of-input refresh when a
long input has rolled past its bounded evidence ring, so a downstream final key
refresh cannot be starved indefinitely. Tempo candidate promotion also
preserves the TEMP section's non-increasing encoded-score order, and the writer
defensively rejects an invalid order or relation instead of emitting data its
reader cannot parse.

The exact frozen 60-track automated diagnostic set was rerun from scratch with
an analyzer whose SHA-256 is
`83b16851c0644a4ee6643b814d8c32020ece2344424e5db825747fe696b00fb9`.
The runner completed all 60 tracks, and a separate inspector audit accepted all
60 containers with completed sessions and FINAL tempo/grid/key/meter sections.
The previously non-completing long input finished under the original
one-million-iteration guard; the six previously unreadable TEMP containers now
round-trip normally.

The frozen evaluator still rejected the automated diagnostic result:

- exact key: **15/60 (25.0%)**;
- exact meter: **43/60 (71.7%)**;
- downbeat phase: **6/60 (10.0%)**;
- beatgrid: **4/60 (6.7%)**;
- high-confidence key error: **10/60 (16.7%)**;
- high-confidence grid error: **24/60 (40.0%)**;
- high-confidence meter and downbeat error: **0/60**.

These beatgrid figures supersede the initially recorded 2/60 grid result and
23/60 high-confidence grid errors. The first export combined the `MTRD`
downbeat, which native analysis resolves against `LGRD`, with the first `GGRD`
segment's period. The exporter now keeps that pair on `LGRD` whenever the local
grid is available, with a regression test for files containing both grids. A
re-export of the same immutable 60 analyzed files changed no key, meter or
downbeat result; it raised grid accuracy only to 4/60, so it does not alter the
failed diagnostic outcome.

The corrected private result CSV and evaluator report are retained locally
with SHA-256
`a9d1dd9c391c258199cdde5b7bc83a213f9eefdfb52db5c2c4e76a5406fa0ab2`
and `b8150035cc9a8cf76fc49034d8caf72449f1fc09f55a9ebfe87476efedd3be2f`
respectively. These hashes identify reproducible diagnostic artifacts without
publishing private audio, labels or source mappings.

## Post-run error taxonomy — 2026-08-23

A read-only audit compared the frozen automated labels, both encoded grids and
the independent pre-review estimates. It found no single evaluator conversion
that explains the remaining gap:

- both local and global periods are within 1% on 37/60 tracks, despite choosing
  different periods on all 60 tracks;
- 30/60 predicted downbeats are within 0.10 beat of *some* reference beat, but
  only 6/60 are within 0.10 beat of the automated reference bar phase;
- the predicted bar phase is spread across all four 4/4 positions rather than
  showing one constant one-beat offset;
- meter output is 4/4 on 43 tracks and 3/4 on 17, while the automated Rekordbox
  reference labels every retained track as 4/4;
- key errors comprise 13 same-tonic parallel-mode choices, one relative-mode
  choice and 31 other choices; there is no dominant tonic transposition;
- exact APTA key accuracy rises with agreement among the five independent key
  estimators, while the automated reference itself has three or more matching
  independent votes on only 24/60 tracks.

This separates three follow-up problems: beat/downbeat phase selection is the
clearest DSP defect signal, meter needs a targeted 3/4-vs-4/4 corpus, and key
tuning must not treat the weak automated Rekordbox labels as authoritative
ground truth. Independently verified labels remain mandatory before any
official acceptance or release decision.

## Development candidate reruns — 2026-08-23

The taxonomy exposed that the automated-reference period was already present
at TEMP rank 1 on 11 additional tracks and rank 2 on one more. It also exposed
that the S6-to-S4 handoff used only the single longest global segment; on a
reproduced failure that segment was a low-confidence 82.721 BPM tail while
several mutually agreeing segments represented the correct 127.841 BPM family.

Two bounded selection changes were implemented after observing this corpus:

- the representative S6 tempo now comes from the confidence-weighted duration
  of agreeing segment families rather than one longest segment;
- a close, non-metrical S6 proposal may reorder only a candidate S4 already
  found, and only with greater global confidence, the existing S4 score floor
  and a strictly better fine-grid fit.

The first full candidate at revision `ae603ad` changed no exported result and
was retained only as the close-candidate plumbing prerequisite. Adding the
dominant S6 family choice at revision `c12b303` changed three tracks: all three
previously wrong periods became correct and no correct period was broken.
Period accuracy therefore rose from 37/60 to 40/60. One affected track changed
from a correct low-confidence 4/4 result to a wrong low-confidence 3/4 result,
showing that tempo improvement alone does not stabilize meter/downbeat.

Every 3/4 prediction in the baseline automated DJ run had confidence between
38 and 54, while all 60 automated references were 4/4. Revision `1dadfb2`
therefore adds a conservative common-time prior: a 3/4 argmax below confidence
50 no longer overrides 4/4, while a strong synthetic 3/4 regression vector
remains selected. The exact 60-track set was analyzed again from scratch using
an analyzer with SHA-256
`4e6f33ca33a82c078d08e2ca39af2ce51419d82b931b84a3b678721ea3dba82c`.
All 60 containers passed completed/FINAL inspection.

Compared with the corrected `d53ee8d` baseline, the final development rerun
measured:

| Metric | Baseline | Development candidate |
|---|---:|---:|
| Beat period within 1% | 37/60 (61.7%) | 40/60 (66.7%) |
| Exact meter | 43/60 (71.7%) | 58/60 (96.7%) |
| Downbeat phase | 6/60 (10.0%) | 5/60 (8.3%) |
| Beatgrid (period + phase) | 4/60 (6.7%) | 4/60 (6.7%) |
| Exact key | 15/60 (25.0%) | 15/60 (25.0%) |
| High-confidence key errors | 10/60 (16.7%) | 10/60 (16.7%) |
| High-confidence grid errors | 24/60 (40.0%) | 24/60 (40.0%) |
| High-confidence meter/downbeat errors | 0/60 | 0/60 |

The meter gate passes only against labels that are uniformly automated 4/4 and
were already inspected while designing the prior. It is not evidence that real
3/4 recall is adequate. Downbeat lost one net correct phase and remains the
dominant blocker; onset-only phase alternatives tested against the same corpus
produced 4–6 correct tracks, so none was promoted into production code.

The corrected private result CSV, evaluator report, output audit and run
metadata have SHA-256 values
`8a011cf9c695ed5d2fdb5eeaeff92504d0655827306c16c291fd4d0565ae1c0e`,
`14006b0f55fe31aec04a8a0407a85c7094f137390d48cee354a50aec338cb22c`,
`8065694e6976ff46a262b08e16b51f4c60b970791d1b9268dbdea84702a7c911`
and `d0e7fc996c070ad40f827649068fd2729bcd9357f81c16643a1ce58b50c2538d`
respectively. This run is deliberately classified as contaminated development
evidence: the same corpus informed the candidate rules, its labels lack human
verification, and the evaluator still rejects key, downbeat, beatgrid and
key/grid high-confidence safety.

This rerun closes the two software-path failures, not the final DJ acceptance
blocker. The labels are automated rather than independently verified by human
review, and the measured accuracy and key/grid safety gates fail by wide
margins. No acceptance or release claim is made.

## Verification workbench handed to the owner — 2026-08-25

The manual verification prerequisite was prepared as a local-only review
workbench so the owner's listening pass is confirmation-by-ear rather than
measurement from scratch:

- all 60 canonical WAVs staged with opaque file names;
- a self-contained HTML page lists every track sorted by the independent
  pre-review priority score, prefilled exclusively from Rekordbox and the
  independent estimator set (five key estimators, separate rhythm estimate) —
  APTA output is never shown or used;
- per track: audio player jumping to the provisional downbeat, editable key,
  BPM, meter, a mark-downbeat-at-playhead control, an explicit Confirmed
  action (a track counts as verified only after it), and per-track Exclude for
  ambiguous material; at least 48 verified tracks must remain;
- export produces `verification.csv` consumed by the freeze step.

While the owner verifies, upstream blockers continued to close: Task-5
(ensemble fresh-set acceptance) and Task-6 (calibrated confidence acceptance
and integration) completed on this date. The remaining acceptance sequence is
unchanged: freeze verified labels and manifest with hashes, record the pre-run
state in [`APTA-1.1-DJ-ACCEPTANCE-PROTOCOL.md`](APTA-1.1-DJ-ACCEPTANCE-PROTOCOL.md),
run anonymous `apta-analyze --features all` once over the exact frozen corpus,
export FINAL results, execute the frozen evaluator, and publish the verdict —
positive or negative — bound to exact hashes.
