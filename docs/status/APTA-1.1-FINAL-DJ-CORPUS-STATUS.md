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

The next engineering work is to reproduce and fix the long-form non-completion
and corrupt-container defects before repeating this frozen automated diagnostic
run. Manual independent ground truth remains a separate prerequisite for any
future official acceptance claim.
