# Fixed musical anchors with local PCM timing evidence

Registered 2026-10-22 at clean `rust-rewrite` baseline
`28875884990145133d173ef871d729665e126e46`, before this candidate implementation
or audio inspection. The [fixed protocol](PCM-ANCHOR-TIMING-PROTOCOL.json) owns
the complete algorithm, input hashes, stages, resources and vetoes. This follows
the [rejected four-neighbor fit](MULTIBEAT-TIMING-RESULT-20261022.md); none of its
parameters or outputs are repaired.

## Single evidence change

Keep the existing Beat This! event identities, ordinals, metrical level,
downbeat indices, S6 windows and original OLS projection. Add one new axis:
local PCM evidence around the fixed learned anchors. Search +/-20 ms at 1-ms
spacing, comparing summed stereo energy in 2-ms windows immediately before and
after each possible position. Squaring channels separately avoids cancellation
of opposite-polarity channels. The measurement does not assume every musical
beat is an audible attack and cannot correct a missing/extra beat or wrong tempo
family.

Accept only a positive, sufficiently energetic and isolated rise: minimum PCM16
RMS 64 over the combined context, normalized contrast >=1/4, and at least twice
the strongest competing rise outside a 2-ms exclusion radius. These constants
are fixed engineering hypotheses, not thresholds selected from this corpus.
An ambiguous, weak, boundary or missing-attack measurement leaves the original
anchor unchanged and explicitly unrefined. Do not substitute a weaker peak.
An input gap is unavailable PCM and causes projection abstention in affected
windows; it is never treated as silence. No padding or inferred attack fills it.

Use the original OLS projection and same-grid meter rule on the refined event
coordinates. Retain every event, its downbeat association and original window
membership. No broader context, half/double-tempo search or label-driven shift.
Precision of the sample coordinate alone does not establish musical correctness.

## Ordered evidence and veto

Stage A reads the existing hash-bound 100-source GTZAN canonical PCM prefixes
and saved events. Keep all 600 windows and 401 complete reference contexts.
Hash full WAVs and code/runtime before and after execution. Reproduce every old
coarse/OLS tuple and score; use the unchanged frozen scorer and existing common
point/sequence diagnostics. Record all point/anchor changes and support reasons.

Require joint correctness >187 and complete-sequence correctness >178, zero
OLS period/phase/joint breaks, zero old-correct meter/downbeat losses and no
new coarse joint-break identities. The two pre-existing coarse-to-OLS failures
remain visible and prohibit product promotion if retained. This is a diagnostic
timing-refinement gate; all frozen product gates remain unchanged. Unlike the
rejected full reconstruction candidate, it does not claim to solve pre-existing
tempo-family errors through a <=20-ms movement.

Freeze the existing forty ASAP development-only performances as conditional
stage B before implementation, using the manifest hash in JSON. They have been
used for native development, but not this PCM candidate. Only if every GTZAN
gate passes, separately freeze the unchanged final0 model/runtime, render/audio,
new events and OLS control, then evaluate this identical refiner on all forty
performances / 240 windows. Require strict joint/sequence improvement and zero
old-correct timing/meter/downbeat or new coarse-joint losses there too. No
formal holdout, fresh acceptance or training-set nonoverlap claim follows.
On a stage-A veto, stop before transfer audio, labels or inference are opened.

## Implementation boundary

Commit/push registration, then implement a separately invoked host-only tool.
Test exact onset timing, stereo polarity, silence, steady energy, competing
attacks, low-level signal, source/search/window boundaries, explicit gaps,
event order/count, unchanged-input identity, missing/extra beats, metrical
ambiguity and exact meter association before corpus scoring. Commit/push that
preflight before the one clean-source run. Keep private PCM, IDs, filenames and
per-event/detail reports outside Git; publish only aggregates and hashes.

One source is held at a time. The fixed stage-A host cap is 180 s / 256 MiB;
the scan is 41 constant-time prefix-energy comparisons per anchor. This is not
a P4 runtime or memory measurement. Default DSP, native validators/limits,
consumer pins/providers, confidence calibration, release and versions remain
unchanged. Any failed veto rejects this exact candidate without a same-cohort
threshold/radius/dominance/fallback rescue.
