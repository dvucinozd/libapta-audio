# Learned beat/downbeat reference experiment

Registered 2026-10-22 on the clean research-decision baseline
`546085909853cd4eef1d4e6c358244c6886ebd5c`, before new annotation values or model
predictions are inspected. The [JSON protocol](NEURAL-REFERENCE-PROTOCOL.json)
owns the fixed model, data-selection, numeric gates and stop conditions.
The user approved the [research recommendation](BEAT-MODEL-DECISION-20261022.md)
and explicitly authorized acquiring GTZAN audio.

## Hypothesis and boundary

Learned musical beat/downbeat evidence should improve over the exhausted
handcrafted onset selectors. Original BeatNet model 1 + official PF is the
P4 candidate; Beat This! final0 with its default non-DBN postprocessor is the
offline control. Only these two checkpoints are compared. Source/weights,
runtime dependencies, conversion and input manifests are frozen externally
before inference. No weights or audio enter Git.

This experiment first separates event quality from constant-grid projection.
Event gates are beat F1 >=0.85, downbeat F1 >=0.70, beat CMLt >=0.75 and downbeat
CMLt >=0.60, macro-averaged over all selected tracks. These are screening
criteria before an expensive port, not relaxed product acceptance. Run both
fixed references even if one fails quality, so the control can distinguish a
weak candidate from a common evaluation problem. Operational/parity/resource
failure stops scoring; do not repair a failed accuracy result by changing seeds,
thresholds, data selection or checkpoints.

Use 100 GTZAN excerpts, ten per genre, chosen by the JSON's deterministic hash
order before opening annotations. Verify the original archive SHA-256 and each
selected WAV's published index MD5; eliminate exact audio duplicates before
selection. If annotation, audio or index coverage is missing, stop rather than
replace a source. Both checkpoint sources declare GTZAN excluded from training.
Cross-dataset recording/composition reuse remains a separate provenance check;
do not call this fully independent transfer solely from dataset names.

Keep annotation values and source mappings in private external manifests.
Use the existing canonical WAV preparation tool and unchanged native dense
reference scorer. The old formal ASAP/Ballroom/GiantSteps holdouts and the
fresh final DJ pool remain closed. Dataset acquisition grants no redistribution
rights or product release qualification.

## Fixed mechanics

Each model starts with fresh source state. NumPy's legacy RNG and BeatNet's
module-local default_rng are reset to the registered seed for every source.
Model input is mono 22050 Hz, derived identically from canonical 48000 Hz PCM
with soxr HQ. Preserve the source time origin; do not shift events to maximize
agreement with labels. BeatNet's centered feature windows have lookahead;
online-network evidence is not yet a zero-latency streaming implementation.

Retain raw event times before projection. Event F1 uses 70 ms tolerance and
ignores the first five seconds; CMLt separately exposes continuity errors.
Project only candidate events in each native S6 window by consecutive-ordinal
OLS, rounding anchor/tempo as frozen in JSON. Predict meter only from two
consecutive equal bars on that same event sequence and grid. Abstain when the
evidence does not support it. Uncalibrated model probabilities or particle
agreement must not be advertised as APTA confidence.

The unchanged coarse S6 binary supplies per-window geometry and a paired
baseline. Report all windows, missing reference contexts, fixes/breaks, meter
coverage and errors. No candidate borrows baseline meter or reference phase.
Product promotion still requires zero period/phase/joint breaks, independent
transfer, confidence safety and the complete frozen qualification sequence.

## Representation audit and execution stages

Before real model scoring, add synthetic regressions for source offsets,
half/double tempo, variable rhythm, missing events, boundary context, meter,
non-finite/duplicate events and resource/hash refusal. Verify official network
framewise versus chunked stateful inference on synthetic features. Serialize
and independently rescore saved events with the unchanged APTA scorer.

Separately fit the same label-derived OLS grid to the already-open forty ASAP
and forty Ballroom development references. A passing fit proves that this
particular grid can represent that window. A failed fit does not prove that
every possible constant grid fails. Keep these reference-derived results
diagnostic-only and separate from candidate predictions.

Stages are (A) this registration, (B) tested host tooling, artifact/data freeze
and representation audit, (C) one two-model GTZAN run if all preflight checks
pass. Commit and push each completed checkpoint before moving to the next.
No native estimator/backend, production provider, consumer pin, release,
stable version or failed temporal experiment changes in this experiment.

## Availability amendment, before the first music prediction

The first preparation stopped for a missing annotation, as required. Its
selection/canonicalization remain retained, with no music model inference.
The [availability amendment](NEURAL-REFERENCE-AVAILABILITY-AMENDMENT.json)
registers metadata-only eligibility from annotation-file presence before
applying the same hash selection. It preserves all 100 tracks per attempt,
ten per genre, with no post-freeze replacement. The original registration is
unchanged. All models, seeds, metrics, numeric gates and output rules remain
identical; this repair does not respond to a model accuracy result.

## Exclusive source endpoint, operational repair before scoring

The first music attempt at `447b7cbfa5d56bb87ecc09839f8f0e32497e79f2`
stopped after four Beat This! sources because its next output includes an
event exactly at 30.000 seconds for 30-second PCM. Centered STFT produces this
exclusive endpoint frame. The sibling BeatNet process was stopped with thirteen
saved outputs. No accuracy metrics were calculated or inspected.

Retain that complete frozen attempt and raw partial outputs. The adapter now
records exact-endpoint events separately and supplies only events in the
source domain `[0, duration)` to projection/scoring. Negative, non-finite,
duplicate or later events still fail; nothing is shifted or fitted to labels.
A regression covers exact endpoints and invalid timestamps. A new attempt
uses the identical frozen 100-source prepared manifest, checkpoints, seeds,
metrics and gates. This is a source-boundary adapter repair, not model tuning.
