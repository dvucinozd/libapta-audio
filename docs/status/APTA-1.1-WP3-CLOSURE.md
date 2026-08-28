# APTA 1.1 WP3 closure

- **Status:** closed without default downbeat promotion
- **Closure baseline:** `60ebb5a26da1f00110433822e30ca072948a7aac`
- **Retained diagnostic:** opt-in three-band overview phase
- **Acceptance claim:** false
- **Formal holdouts:** unopened

## Decision

WP3 closes without changing production meter or downbeat selection. The
pre-existing opt-in `APTA_ENABLE_EXPERIMENTAL_3BAND_DOWNBEAT` candidate is the
only retained diagnostic because it introduces independent low/mid/high accent
evidence and transfers without regressions:

| Open development evidence | Baseline downbeat | Three-band candidate | Fixes | Breaks |
|---|---:|---:|---:|---:|
| ASAP development | 1/40 | 2/40 | 1 | 0 |
| Ballroom development | 7/40 | 8/40 | 1 | 0 |
| Already-spent DJ corpus | 5/60 | 7/60 | 2 | 0 |

It changes no meter numerator and no Q32 lattice. The focused
`apta.dj.meter_downbeat` test passes on the current Werror multiband/three-band
build. These properties justify preserving the path as an experiment, not
enabling it by default.

## Why promotion is closed

WP3's pre-registered exit target is at least 80% downbeat accuracy on each open
development family. The current candidate reaches 5% on ASAP and 20% on
Ballroom. On the Ballroom trace, only 10/40 tracks have a period within 1%, and
the joint period/consistent-bar-phase oracle ceiling is 9/40. Production
broadband phase scoring already reaches that ceiling where its evidence is
representable.

WP1 and WP2 closed without a lattice promotion, so the upstream condition
needed for an 80% bar-phase result was not established. Another meter-stage
threshold or recombination cannot repair an absent beat lattice and would tune
the already-open corpora. WP3 therefore closes with the production baseline
unchanged and the useful three-band path disabled.

No ASAP/Ballroom meter/downbeat holdout or fresh acceptance corpus was opened.
WP3 may be reopened only after a separately qualifying lattice or genuinely
new high-resolution bar-periodicity evidence is available.
