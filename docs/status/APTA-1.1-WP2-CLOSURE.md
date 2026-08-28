# APTA 1.1 WP2 closure

- **Status:** closed without candidate promotion
- **Closure revision:** `dfc1d1916fcf63ccdc19f893864f1d57696a0081`
- **Retained selector:** frozen full-window S4 rank zero
- **Acceptance claim:** false
- **Formal holdouts:** unopened

## Decision

WP2 is closed as a completed temporal-selector investigation with no production
change. Both pre-registered selectors preserved the original full-window
top-three set but failed the external-development gates:

- I1's unweighted Viterbi continuity promoted 21/40 tracks in each corpus,
  produced three Ballroom period breaks and negative Ballroom phase/joint
  transfer;
- I2's strict 4/7 local-winner majority still regressed ASAP period and phase,
  produced two Ballroom period breaks and had negative phase transfer on both
  corpora.

Testing neighboring Viterbi weights or 5/7, 6/7 and 7/7 vote thresholds would
be a selector sweep forbidden by the frozen protocols. The stable engineering
outcome is therefore to retain the existing S4 full-window ordering.

The spent-DJ 49/60 top-three ceiling remains diagnostic information, not a
license to optimize against that spent corpus. Neither WP2 iteration reached
the stage that permits a spent-DJ run. No formal holdout or fresh acceptance
corpus was opened.

## Downstream boundary

WP3 starts from the unchanged production period and beat phase. It may improve
meter and downbeat using accent information conditional on that frozen lattice,
but it may not rerank tempo candidates, revive a rejected WP1 onset formula or
add a WP2 selector threshold. WP2 can be reopened only for a pre-registered
hypothesis with new temporal information not represented by I1 or I2.
