# DD-05 large-error tail: post-hoc diagnostic scope

Date: 2026-10-23. This extends the already-observed failure audit. It is not a
candidate retry, a new decoder, parameter selection, or fresh acceptance evidence.
The original rejected attempt, 1% interval veto and all input identities remain
fixed. Prior diagnosis SHA-256:
`901faeca3290ae9ef7a688960542119d126180eb89caf6d798a47830382816f8`.

Before inspecting additional private tail detail, fix these arithmetic categories:

- Large tail: absolute original interval/local-reference-period error >5%,
  reusing the prior 1041-interval bin. Other intervals retain their denominators.
- Interval/reference-period ratio families: 1/3, 1/2, 2/3, 3/2, 2, 3; each has
  an inclusive 1% relative band around that family ratio. Everything else is
  off-family. These labels describe ratios, not proven musical tempo mistakes.
- Match both original endpoints independently to their nearest annotated beat;
  phase support uses the unchanged 10%-local-period rule. Record reference-index
  steps 0,1,2,>=3 and successor-outside-reference explicitly. Two phase-supported
  endpoints with step>=2 skip annotated events; adjacent matched irregular
  annotations can themselves produce a family-like ratio to the local median.
- Record whether the original row term is also >5% wrong and whether the local
  target lies below 9600 or above 72000 frames (the registered d10..75 range).
- Separate all 5922 known scored intervals, the 95 actual joint regression
  windows, and the 181 nominal-correct/actual-failed windows. Count windows with
  any large tail versus exclusively <=5% period errors. Groups overlap; do not
  add their counts or treat point and window support as interchangeable.

Use the same source/input/scorer binding and full 600-score replay as the first
audit, through a read-only source observer. Save new tail artifacts in a new
external directory; never edit old outputs or score an adjusted candidate.
Reconcile 5922 known intervals, 3577 period failures, 1041 tail intervals and the
95/181 window groups before publishing aggregates. Per-source IDs and window
details stay private. Apply 1800-second / 262144-KiB / 64-MiB audit supervision.
Run generated tests and commit/push this scope/tool before the private audit.

After this audit, deliver an implementation plan with output/reference semantics,
algorithm/numeric decision gates, generated tests, bounded-work/resource checks,
data-exposure/independence requirements and stop conditions. Leave unresolved
choices explicit; no new model, data acquisition, neural run, DSP activation,
training or Rust/P4 port is part of this checkpoint.
