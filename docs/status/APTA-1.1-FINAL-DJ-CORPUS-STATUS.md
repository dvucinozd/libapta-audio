# APTA 1.1 final DJ corpus status

- **Snapshot date:** 2026-08-22
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
- beatgrid: **2/60 (3.3%)**;
- high-confidence key error: **10/60 (16.7%)**;
- high-confidence grid error: **23/60 (38.3%)**;
- high-confidence meter and downbeat error: **0/60**.

The private result CSV and evaluator report are retained locally with SHA-256
`03e92ddb25e53a334f78d670910a9f07ab6900fd9ad97ca6555a4c1c8805573f`
and `6c2751a8cb36cc25b370915772e246d19ba1cc637212bf3e8758ea329a672372`
respectively. These hashes identify reproducible diagnostic artifacts without
publishing private audio, labels or source mappings.

This rerun closes the two software-path failures, not the final DJ acceptance
blocker. The labels are automated rather than independently verified by human
review, and the measured accuracy and key/grid safety gates fail by wide
margins. No acceptance or release claim is made.
