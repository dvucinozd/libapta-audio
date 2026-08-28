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
