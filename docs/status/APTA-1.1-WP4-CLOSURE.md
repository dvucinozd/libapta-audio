# APTA 1.1 WP4 closure

- **Status:** closed without default key promotion
- **Closure baseline:** `2fd48878bea8140087f2c481a0b510a404cdbeee`
- **Retained diagnostic:** opt-in harmonic projection
- **Acceptance claim:** false
- **Formal key holdout:** unopened

## Decision

WP4 closes with the production folded-chroma key path unchanged. The existing
`APTA_ENABLE_EXPERIMENTAL_HARMONIC_HPCP` path remains an opt-in diagnostic. On
the already-spent 60-track DJ development corpus it moves exact key accuracy
from 20/60 to 22/60, with two fixes, zero breaks and zero high-confidence key
errors. It adds 144 bytes of conditional key-analysis state, no resonators and
only a bounded 36-bin projection plus one selector pass at refresh time.

That result is safe but not promotable: 36.7% is far below the frozen 75%
acceptance gate, and the candidate did not establish independent development
transfer.

## Independent-development outcome

Two distinct, pre-registered derivations reused only the already-open 40-track
ASAP development split. Neither opened or labeled a holdout ID:

| Source | Included | Major | Minor | Disposition |
|---|---:|---:|---:|---|
| Source MIDI `key_signature` events | 6/40 | 6 | 0 | Nonviable: count and mode coverage |
| ASAP `perf_key_signatures` under frozen D2 grammar | 0/40 | 0 | 0 | Nonviable: all values outside frozen token grammar |

Both derivations were byte-deterministic and stopped before any analyzer run.
Their exact rules and hashes are recorded in
`APTA-1.1-WP4-ASAP-KEY-DEVELOPMENT-PROTOCOL.md` and
`APTA-1.1-WP4-ASAP-ANNOTATED-KEY-DEVELOPMENT-PROTOCOL.md`.

## Why promotion is closed

The harmonic candidate lacks the required clear gain on an independent key
development set and the required >=70% independent exact-key accuracy. Changing
the D1 or D2 label grammar after seeing its exclusions would convert the same
data into a tuning set. Reusing the spent DJ corpus cannot establish transfer,
and opening a formal key holdout before the development gate would violate the
one-shot boundary.

No key path is promoted into WP5. WP4 may be reopened only with a genuinely new,
pre-registered independent key dataset or a new tonal-evidence hypothesis that
does not reuse these labels for iterative tuning.

## Subsequent reopened evidence

WP4 was later reopened under new pre-registered evidence. A balanced
GiantSteps-MTG development transfer rejected harmonic-HPCP at 22/96. A separate
96-track, mode-balanced original GiantSteps development run rejected
resource-neutral centered correlation at 28/96 with 52 new high-confidence
errors. Both attempts stopped before the sealed 48-track formal holdout. The
exact hashes are recorded in
`APTA-1.1-WP4-CENTERED-KEY-CORRELATION-PROTOCOL.md`. A third bounded temporal
chord-state candidate reached 38/96 with positive net fixes but failed absolute,
per-mode and confidence-safety gates. A fourth soft temporal-profile candidate
used another class-balanced 96-track FMAK selection with zero overlap and
reduced production from 18/96 to 17/96. Both FMAK splits are spent; their exact
one-shot results are in
`APTA-1.1-WP4-FMAK-TEMPORAL-CHORD-KEY-PROTOCOL.md` and
`APTA-1.1-WP4-FMAK-TEMPORAL-PROFILE-KEY-PROTOCOL.md`. All four attempts stopped
before the sealed formal holdout, and no key path entered the existing WP5
baseline.

A fifth attempt was subsequently pre-registered against a third disjoint FMAK
selection. Unlike the rejected rescoring and temporal candidates, it tests new
front-end evidence by integrating three equal probes across each semitone cell.
Its selected 72-track audio remains unopened and no result or promotion claim
exists; the frozen boundary is in
`APTA-1.1-WP4-FMAK-SEMITONE-BAND-KEY-PROTOCOL.md`.
