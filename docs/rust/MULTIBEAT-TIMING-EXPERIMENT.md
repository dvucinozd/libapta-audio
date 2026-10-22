# Bounded multi-beat timing reconstruction

Registered 2026-10-22 on clean `rust-rewrite` baseline
`8733376524fb9ca4c7ea851aa17dd96a1bda7734`, before candidate implementation or
scoring. The [fixed protocol](MULTIBEAT-TIMING-PROTOCOL.json) owns all parameters
and vetoes. This follows the
[negative raw-event representation audit](EVENT-REPRESENTATION-RESULT-20261022.md).

## Hypothesis and scope

Saved Beat This! anchors are quantized to 20 ms. A longer regression baseline
can reduce period variance without treating each adjacent interval as a precise
tempo. Test one bounded change: extend the existing S6 ordinary least-squares
fit with up to four adjacent events on each side. Use exact rational arithmetic
and the existing millibpm/Q32 publication convention. Keep source coordinates,
original event ordinals and the six fixed S6 window ranges.

Require at least nine support events. Reject unsupported tempo, a fitted anchor
outside its original window, any support residual above two hops (1920 source
frames), and any change in the in-window beat count. These fixed consistency
guards do not use truth. They deliberately abstain instead of inserting beats,
selecting another metrical level, shifting phase or falling back to OLS. Known
PCM gaps invalidate affected support; missing musical events do not establish
a known PCM gap. Unsupported windows remain present and cannot be bridged.

Each accepted window is one provisional constant-period segment, at most six
on this cohort and bounded by the existing eight-segment cap. Neighbor fits
can have different periods; a source graph is not assumed continuous or
consumer-compatible merely because the window diagnostics pass. Check ordinal
and position ordering across accepted windows. Keep meter on the reconstructed
positions of its original supported downbeat ordinals, using the existing
two-equal-complete-bars rule. Confidence remains uncalibrated.

## Fixed comparison and stop

Use only the already-open 100 GTZAN development sources, their saved events,
existing labels and coarse/OLS scores: 600 windows, 401 reference-complete.
No new inference, audio, labels, corpus selection or holdout access. Retain raw
representation failures as the baseline; reproduce every saved OLS tuple and
score, then use the unchanged constant-grid scorer for this candidate. Also
report the unchanged common point and one-to-one sequence diagnostics.

The complete fixed cohort is one experiment. Require joint correctness to rise
from 187 to at least 188 windows and complete-sequence correctness from 178 to
at least 179, with **zero OLS period, phase or joint breaks**, zero coarse joint
breaks, and zero old-correct meter/downbeat breaks (including lost support).
Report both original OLS regressions individually. A veto rejects this candidate
and stops further corpus work; no context, threshold, fallback or model rescue.
Identity, code, hash or control-score mismatch stops execution immediately.

## Execution boundary

First commit and push registration. Implement an explicitly invoked host-only
tool and focused synthetic tests for quantization, offsets, source boundaries,
missing/extra beats, declared gaps, tempo-family ambiguity, exact meter ordinals,
count preservation, partial support and capacity. Commit and push this preflight
before scoring from a clean exact source. Save per-source evidence outside Git
and publish only aggregate metrics and hashes. Expected host budget is at most
120 seconds / 256 MiB; no new core allocation or runtime path is introduced.

This measures reconstruction, not production adoption. A positive result would
justify a separately checked native segmented-grid implementation and independent
transfer test. A negative result must remain visible. Native validators, consumer
pins, production providers, default features, release versions and frozen scorers
remain unchanged. Physical P4, confidence, cross-dataset identity and fresh
product acceptance gates remain open regardless of this cohort's outcome.
