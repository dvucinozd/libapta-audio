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
