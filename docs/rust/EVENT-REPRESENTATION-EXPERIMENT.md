# Saved-event representation audit

Registered 2026-10-22 on clean `rust-rewrite` baseline
`96bf323af7e2f964f697637f395831785b2291bf`, before implementing or scoring this
audit. The [fixed protocol](EVENT-REPRESENTATION-PROTOCOL.json) owns data,
algorithms, diagnostics, capacities and vetoes. This continues the
[measured neural comparison](NEURAL-REFERENCE-RESULT-20261022.md).

## Question and fixed scope

Determine what is lost when saved Beat This! event coordinates are fitted to
one constant-period S6 grid, and what the existing explicit and segmented
representations can retain. Use only the existing 100-source development run;
no network inference, fresh labels/audio, tuning or holdout access. Retain all
600 windows and the original 401 complete-reference contexts.

Compare the unchanged OLS control with exact raw explicit events and lossless
equal-interval segment compression. No new event selector, smoothing or timing
refinement is introduced. Keep source sequence ordinals and bind meter/downbeat
to those coordinates, using the existing two-complete-equal-bars support rule.
Event support does not establish full-source coverage, and a missing musical
beat does not by itself establish an unavailable PCM gap.

The old scorer only describes constant-period hypotheses. Reproduce its old
results unchanged, including all regressions, and use **separately named common
point/interval diagnostics** to compare constant and explicit timing. These
retain 1% local period and 0.10-beat phase tolerances. Add a one-to-one reference
coverage diagnostic so deleting difficult beats cannot count as a complete
sequence. Do not call results from different diagnostics interchangeable.

The model uses 50-Hz event coordinates. Quantized event positions can have
excellent event F1 but noisy adjacent intervals at a 1% tolerance. Measure that
constraint and projection displacement rather than assuming a new container
representation solves it. A negative result is useful and does not authorize
another threshold search.

## Software and consumer boundaries

Exercise the actual Rust grid writer, strict reader and semantic validators.
Payload preservation and complete-result acceptance are different checks:
current external/native validation couples explicit adjacent intervals to one
selected TEMP, while segmented validation checks each segment's own period.
Do not change those validators during this audit. Keep wire caps of eight
segments and 3072 explicit beats and report capacity rejection without truncation.

The actual M1 consumer is inspected at the pinned snapshot in JSON, with its
production dependency unchanged. Exercise provisional graph handling and
synthetic unsupported-gap/meter/capacity/revision cases using an isolated host
probe against the unchanged adapter. A tempo-only fallback is not a successfully
adopted playback grid; structural fixtures are not musical or hardware evidence.

Before scoring, test source offsets, half/double tempo, missing/extra beats,
quantized intervals, gaps, exact compression boundaries, same-grid meter,
capacity refusal and roundtrip identity. Bind code/protocol, saved event files,
truth and original score records before the first audit result and recheck
afterward. Save raw detail outside Git and publish only aggregate evidence.

Commit/push registration, implementation and final measured result as separate
completed checkpoints. Production providers, consumer pins, stable version,
release, scorer and core DSP remain unchanged. This audit may select the next
implementation problem; it cannot close confidence, independent transfer,
native musical acceptance or physical ESP32-P4 gates.
