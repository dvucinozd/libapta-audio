# APTA 1.1 WP1 closure

- **Status:** closed without candidate promotion
- **Closure revision:** `c7497a28ced0f44791aba3e2803c923db691ec2f`
- **Retained production path:** frozen multiband baseline
- **Acceptance claim:** false
- **Formal holdouts:** unopened

## Decision

WP1 is closed as a completed investigation with no algorithm change promoted.
The production multiband onset path remains the only retained input to S4. An
experimental result is not entitled to ship merely because a work package was
named "new onset evidence"; it must pass the pre-registered retain gates. None
did.

Eight separately named iterations tested materially different hypotheses rather
than a coefficient or threshold sweep:

| Iteration | New evidence or architecture | Rejection point |
|---|---|---|
| I1 | bounded multi-timescale transient representation | spent DJ: no net period/phase gain and excessive cost |
| I2 | normalized rolling transient evidence | open ASAP negative transfer |
| I3 | sixteen-bin local contrast | spent DJ: five fixes/five beatgrid breaks and downbeat regression |
| I4 | causal equal-mean baseline/contrast fusion | native open development: meter regressed on both corpora |
| I5 | causal harmonic consensus | oracle: two ASAP top-1 period breaks |
| I6 | centered contrast curvature | oracle: strong Ballroom period and phase regression |
| I7 | baseline lag ranking with harmonic phase only | oracle: neutral rather than positive ASAP joint transfer |
| I8 | new within-bin peak-to-mean sharpness | oracle: negative top-1 period transfer on both corpora |

Rejected analyzer paths remain opt-in or were never implemented. The I8 peak
trace is retained only as privacy-safe diagnostic infrastructure. Defaults,
public ABI, serialization and the 16-byte onset-bin layout are unchanged.

## Why WP2 may start

The already-spent 60-track DJ trace shows that the retained production onset
evidence has 39/60 correct top-1 periods but 49/60 correct top-three period
candidates. WP2's pre-registered selector target is at least 47/60. Therefore
the candidate ceiling needed for a temporal selector already exists without
promoting an unstable WP1 representation.

This is not an acceptance result and does not authorize a holdout. It is a
development decision to move the failure boundary downstream: WP2 may only
choose among the frozen baseline candidates across bounded consecutive
windows. It may not revive a rejected onset formula, tune a WP1 threshold or
consume an ASAP/Ballroom formal holdout.

WP1 can be reopened only by a separately approved hypothesis that introduces
new information unavailable to I1 through I8 and is pre-registered before any
corpus result. Ordinary progress continues in WP2.
