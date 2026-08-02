# Phase 5 — Tempo generalization status

Phase 5 introduced a frozen development/hold-out protocol for the independent
188-track Rekordbox corpus, evaluated one candidate, and rejected that candidate
after it failed to generalize. No phase-5 tempo-selection or confidence change
remains in the production library.

This is a useful negative result rather than an incomplete experiment: the
hold-out prevented two development-set improvements from being shipped as if
they were general solutions.

## 1. Frozen protocol

The split is deterministic from opaque track IDs and seed
`libapta-phase5-v1`. It uses a 25 percent requested hold-out fraction within
baseline outcome strata. Single-item strata remain in development; every
larger stratum contributes at least one item to both partitions.

| Baseline stratum | Total | Development | Hold-out |
|---|---:|---:|---:|
| Endorsement broken | 4 | 3 | 1 |
| Endorsement fixed | 26 | 19 | 7 |
| S4 exact and unchanged | 139 | 104 | 35 |
| S4 high-confidence octave error | 3 | 2 | 1 |
| S4 other unresolved | 16 | 12 | 4 |
| **Total** | **188** | **140** | **48** |

The split deliberately used the already-known phase-4 outcome only for
stratification. This is a locked hold-out, not a claim that its labels were
historically unseen: phase 4 had already reported the complete aggregate and
three high-confidence S4 IDs. Phase-5 tuning read only development rows; the
hold-out was run once after the candidate and native regression gates were
frozen.

`tools/rekordbox_tempo_corpus.py split` writes:

- `split.json`, including corpus and baseline CSV hashes;
- `tracks-development.txt` and `tracks-holdout.txt` under the ignored prepared
  directory;
- aggregate-only `split-baseline-report.json` and `SPLIT-BASELINE.md`.

The tracked tool does not place titles, artists, paths, audio or per-track
hold-out outcomes in public reports.

## 2. Frozen baseline by partition

| Partition / mode | Tracks | Within 1% | Octave | Other | Errors >=75 | Octave errors >=75 |
|---|---:|---:|---:|---:|---:|---:|
| Development S4 | 140 | 107 (76.4%) | 2 | 31 | 2 | 2 |
| Development endorsed | 140 | 124 (88.6%) | 1 | 15 | 2 | 1 |
| Development S6 | 140 | 123 (87.9%) | 5 | 12 | 4 | 2 |
| Hold-out S4 | 48 | 36 (75.0%) | 2 | 10 | 1 | 1 |
| Hold-out endorsed | 48 | 42 (87.5%) | 1 | 5 | 2 | 1 |
| Hold-out S6 | 48 | 41 (85.4%) | 3 | 4 | 0 | 0 |

Baseline endorsement changed 33 development selections, fixed 20 and broke 3,
for net +17. On hold-out it changed 10, fixed 7 and broke 1, for net +6.

## 3. Added diagnostics

`apta-tempo-corpus --results-csv` now records:

- every published S4 candidate tempo and score;
- S6 nominal tempo and confidence when a global grid is requested;
- the existing selection, relation, confidence and threshold fields.

The Python reader accepts both the old phase-4 CSV and the extended format.
This made it possible to distinguish “correct candidate absent” from “correct
candidate present but not selected”, and to reconstruct an endorsed result's
original S4 winner without another algorithm-specific probe.

## 4. Candidate evaluated on development

The two development high-confidence half-time errors both contained a near-2x
candidate among the published three, at 0.86 and 0.96 of the S4 winner's score.
The strongest metrical sibling among correct development results was 0.78.

The rejected candidate therefore combined:

1. a refined-candidate ambiguity knee of 0.82, in addition to the existing
   integer-lag family scan;
2. a minimum endorsement score of 59000 for an independent candidate, retaining
   55000 for a recognised metrical sibling.

The production files were frozen before hold-out with these SHA-256 values:

| File | Candidate SHA-256 |
|---|---|
| `src/core/apta_internal.h` | `39324d3f382bc59af4ce06ee6d6aa51017df0abf8ec91bd073d0500389346d0c` |
| `src/core/apta_tempo_policy.h` | `420c6d9d66bf4a0c3ce3a4d014181852a4d387f6498f786b7e24b8a91b8d709e` |
| `src/tempo/apta_s4.c` | `bffab97b740dfc696556e35a2e947b60e0af88a332b0240029d94998d4e7c828` |

Development outcome:

| Mode | Baseline exact | Candidate exact | Baseline errors >=75 | Candidate errors >=75 |
|---|---:|---:|---:|---:|
| S4 | 107 | 107 | 2 | **0** |
| Endorsed | 124 | 123 | 2 | **0** |

Candidate endorsement changed 25 development selections, fixed 16 and broke
none, net +16. This deliberately traded one exact result for removal of the
three observed regressions. The complete default, core-only and sanitizer
suites passed before the hold-out was opened.

## 5. One-time hold-out result

The unchanged candidate did not reproduce either apparent development win.

| Mode | Baseline exact | Candidate exact | Baseline errors >=75 | Candidate errors >=75 | Baseline octave >=75 | Candidate octave >=75 |
|---|---:|---:|---:|---:|---:|---:|
| S4 | 36 | 36 | 1 | 1 | 1 | 1 |
| Endorsed | 42 | **41** | 2 | 2 | 1 | 1 |

Candidate endorsement changed nine hold-out selections, fixed six and broke
one, net +5. The stronger independent-candidate gate removed one useful
promotion but did not remove the broken promotion represented in hold-out.

The remaining high-confidence half-time result also exposed the limit of the
candidate ambiguity hypothesis: unlike both development errors, it had no
sufficiently strong correct family member in the published top three. A rule
that only interprets those candidates cannot lower its confidence reliably.

## 6. Decision

The candidate was rejected and its production changes were reverted. The
tracked phase-5 deliverable is the reproducible split, aggregate reporting,
extended candidate/S6 diagnostics, tests and this evidence.

B1 remains open. Reusing these 48 rows to tune another threshold would turn the
hold-out into development data. A next candidate should add genuinely new
evidence — most plausibly a multi-band onset novelty feature, richer candidate
retention, or an independently calibrated agreement model — and be validated
against newly acquired held-out material or a pre-registered cross-validation
protocol that reports all folds rather than selecting on one.

## 7. Verification

Before the rejected candidate's one-time hold-out run:

- default CTest: 83/83 passed, including the temporary direct policy test;
- core-only CTest: 71/71 passed;
- ASan/UBSan CTest: 83/83 passed;
- development corpus: 140 S4 and 140 endorsed analyses completed.

After rejecting the candidate, the production algorithm is identical to the
phase-4 merge. The retained Python suite contains ten tests and covers ANLZ
parsing, candidate/global CSV diagnostics, aggregate metrics, comparison and
deterministic split behavior.
