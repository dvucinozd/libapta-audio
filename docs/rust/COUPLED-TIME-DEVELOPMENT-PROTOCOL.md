# CT-03: exposed-development scope and unresolved independence

Date: 2026-10-23. **REGISTERED_EXPOSED_SCOPE_EXECUTION_DISABLED.**
Registration parent: `0e38d1eaee4fc5b5a064b5a47d1fd11b2f3c2643`.
Machine authority: [development protocol](COUPLED-TIME-DEVELOPMENT-PROTOCOL.json).
Registration checks: [CT-03 evidence](COUPLED-TIME-CT03-20261023.json).
The [CT-00 numerical protocol](COUPLED-TIME-SYNTHETIC-PROTOCOL.json),
[CT-01 implementation](COUPLED-TIME-CT01-20261023.md) and
[CT-02 generated preflight](COUPLED-TIME-CT02-20261023.md) remain frozen.

## Decision and evidence level

Register the exact existing GTZAN cohort as a possible **exposed diagnostic** of
`coupled-neural-event-time-v1`. Its baseline, references and failure patterns
were already inspected before this hypothesis. It cannot establish independent
development, independent transfer or fresh acceptance. Independence remains
**UNRESOLVED**; no independent cohort is selected or acquired by this checkpoint.
This satisfies the plan's explicit unrun-independence exit, not an independence
PASS. A future diagnostic pass must be called EXPOSED_DIAGNOSTIC_PASS_ONLY.

CT-03 binds existing files and fixes conditional evaluation rules. It does not
construct or score any real candidate. Real execution remains disabled: CT-04
must first implement and preflight separate construct/verify/score processes;
CT-05 needs a separately recorded scope/authorization for one exposed attempt.
If independent development is requested instead, first register a different
cohort with verified recording/training membership and appropriate new baseline
denominators. Do not transplant the absolute counts below onto another cohort.

## Exact observations and geometry

The external audit checks 100 original Beat This! event files against the original
worker and capture reports, 100 capture records, 100 dense-payload hashes,
100 coarse-output hashes and 100 baseline-score-file hashes. It rechecks the
original freeze, input manifest, prepared-label-manifest hash, scoring report,
checkpoint, upstream/runtime files and 100 audio-file hashes. In total 643 bound
artifacts verify. WAV bytes are hashed only, never decoded. Dense payloads and
model/runtime bytes are hashed only; no inference/import of the model occurs.
The baseline report index is parsed for file identities; per-source score values
and prepared label values are not parsed. Existing public baseline metrics are
bound records, not newly rescored measurements.

The cohort order is exactly the worker/capture/label-free-input order, with
100 unique sources and 600 contiguous source windows. There are 6095 in-domain
beats, 1851 downbeats and 20..148 beats per source. All 1851 downbeats match an
original beat frame exactly; map each to that frame's original global ordinal.
No nearest-event match, inferred bar phase, truth or meter participates. Ten beat
and three downbeat events exactly at source end remain excluded and retained in
old raw metadata. They are not clamped into the source or sent to the constructor.

The private inventory records exact original event/capture/coarse/score identities,
source audio hashes and original windows. A separately frozen label-free input
manifest contains only source identity metadata, event/capture provenance and
plain beat frames/downbeat ordinals/total/windows/known_gaps. It contains no
labels, scores, coarse tempo/meter, selected grid or rejected-v1 paths. Source IDs
and provenance are parent-runner metadata; the pure worker receives exactly
`construct(beat_frames, downbeat_ordinals, total_frames, windows, known_gaps=())`.
No source ID, file name or correctness verdict enters numerical construction.

Known gaps are empty for these retained complete source prefixes, following the
original capture contract. This records existing input metadata, not a new PCM
continuity measurement. If later validation finds unsupported PCM coverage or a
binding mismatch, invalidate the registered attempt; do not repair the manifest
or replace sources after construction. Retain all source/windows and abstentions.

## Frozen model and preprocessing provenance

These are historical bound identities, not a new model recommendation:

- Beat This! upstream revision `b95c8ab0c58c2d9fcfd40508ae8dffbc05ac4f5c`,
  final0 checkpoint SHA-256
  `8c328b45f59d8dd3dff219253ff6a8d6482be57d0133a29140e2febbf8eb8331`.
  The [retained acquisition record](NEURAL-REFERENCE-PREFLIGHT.md) documents
  mirrored provenance; no new author-signed authenticity claim is made.
- Original CPU two-thread offline inference and upstream minimal dbn=False
  postprocessing, using the unchanged bound inference module. Original canonical
  48kHz stereo PCM is read as float32, channel-mean mono, soxr HQ to22050Hz.
  The original model's n_fft1024/hop441/128-mel centered reflect-padded analysis
  has50 feature rows/second and960 source frames/row. Future context is allowed;
  feature center is not causal availability.
- Original event seconds are source-domain filtered before the original integer
  frame conversion; those saved integer frames are the only observations here.
  CT-03 does not repeat resampling, inference, postprocessing or conversion.
  Current model/upstream/runtime bytes match their retained bindings.

The [original protocol](NEURAL-REFERENCE-PROTOCOL.json),
[availability amendment](NEURAL-REFERENCE-AVAILABILITY-AMENDMENT.json),
[reference result](NEURAL-REFERENCE-RESULT-20261022.md) and
[capture protocol](NEURAL-DENSE-CAPTURE-PROTOCOL.json) own acquisition/selection
history. The cohort uses the previously amended availability-aware selection;
there is no new source replacement or annotation eligibility rule.

## Baseline, scoring meanings and conjunctive veto

Use the original neural events and their unchanged per-window OLS projection as
the comparison controls, with the retained coarse diagnostic as a separate
no-regression control. The numerical candidate receives original neural events,
not the rejected dense-v1 event path or a chosen legacy OLS grid. No old candidate
is retried or amended. The frozen public baseline is:

| Quantity | Frozen value |
|---|---:|
| Sources / all windows | 100 / 600 |
| Reference complete / incomplete contexts | 401 / 199 |
| OLS nominal joint / complete sequence windows | 187 / 178 |
| OLS actual-interval joint / complete sequence windows | 101 / 97 |
| Coarse joint windows | 47 |
| Original coarse-to-OLS joint regressions | 2 |
| Beat F1 / beat CMLt macro | 0.8982778764061308 / 0.8164907923270326 |
| Downbeat F1 / downbeat CMLt macro | 0.7855239309299203 / 0.6743889274673666 |

The 401/199 context counts are retained label-support evidence from the exact
bound reference manifest, not newly derived here. Future scoring must reproduce
every original event score, projection, coarse score, aggregate and genre macro
before publishing a new verdict; otherwise the attempt is INVALID.

The machine protocol copies all 16 musical gates exactly from the old registered
development comparator. They are conditional targets for this same cohort:

- Nominal joint >=188, nominal complete sequence >=179, actual complete >=98.
- Zero OLS nominal period/phase/joint/meter/downbeat breaks; zero coarse joint
  breaks; zero OLS actual period/phase/joint breaks.
- Each of beat/downbeat F1/CMLt macro means is nondecreasing within1e-12, using
  exact unrounded saved fractions, not displayed percentages.

Integrity, independent replay, baseline reproduction, full denominator and all
resource ceilings are required before applying these conjunctive musical gates.
Unknown/missing support is not success. Keep per-source/per-genre paired fixes,
breaks, support and both original regressions externally; publish permitted
aggregates only. Confidence remains0/uncalibrated, never a safety qualification.

Reuse unchanged canonical scoring/adapters bound by the machine protocol:

1. Event metrics: integer source frames /48000; original5s trim,70ms F1 and CMLt
   separately for beats/downbeats, macro over all100 sources, empty predictions0.
2. Projection: candidate-owned events through the unchanged OLS projection and
   candidate-derived two-complete-equal-bars meter rule; unchanged score_window.
   No reference/old-meter borrowing. This is compatibility, not variable timing.
3. Nominal sequence: unchanged points_for_control/score_points, matching projected
   period/phase/joint results. Retain all600 windows and complete context status.
4. Actual sequence: final integer source events, consecutive global successor
   even across window boundaries; last successor None. Unchanged score_points.
   OLS actual baseline retains its historical control_points/score_point_windows
   semantics; report successor availability differences. Do not fabricate a last
   period or replace actual checks with a projected-grid pass.

The existing dense runner's score_source, actual_points and gates can be reused
only as evaluation helpers under the bound code identity. Its old real CLI and
preflight token cannot authorize or dispatch the new constructor. Construction
and verification processes must not import this scorer module or references.

## Independence and prior exposure decision

GTZAN annotations/predictions were already used for neural reference comparison,
event representation, learned-piecewise work, dense-v1 rejection and the interval/
tail diagnosis that motivated this hypothesis. This is exposed candidate-design
data even though lambda/boxes/solver were frozen before this candidate's scoring.
The historical model documentation declares GTZAN excluded by dataset name;
recording/composition overlap across training datasets remains unverified. No
new external membership investigation or recording-fingerprint audit occurred.

An independent route must later provide immutable model-training membership,
candidate-design exposure inventory, duplicate/transcode/recording checks,
label/source provenance and a disjoint frozen development/transfer manifest.
Dataset names and hashes alone cannot close it. No justified independent cohort
is established here; it is explicitly UNRESOLVED. Formal ASAP/Ballroom/GiantSteps
holdouts and the fresh acceptance pool remain closed. No acquisition, model
training, inference or holdout access follows automatically.

## Future phase order, limits and stop policy

CT-04 implements generated operational checks first. The real path remains
disabled until the CT-02 component token and this CT-03 registration token match,
CT-04 isolation/preflight passes, and a distinct real-attempt scope/authorization
record is present. It must reject absent/stale/unresolved authorization and expose
no parameter, cohort, identity or resource bypass. A diagnostic authorization
does not resolve independence or authorize transfer/product activation.

For one later authorized exposed attempt, use a new external destination and the
same clean committed/pushed source for all phases. Construct all100 label-blind
candidates, freeze exact bytes/hashes and complete cohort, independently verify
all100 before the separate scoring process may open labels. Labels, scores and
coarse values never reach a pure worker. Generated absent/permuted-label checks
and missing-verification rejection must pass before real dispatch.

Retain CT-00 ceilings:60 seconds/source construction/verification,1800 seconds
per construct/verify/evaluate phase,262144 KiB/process and67108864 bytes for the
whole attempt including logs/partials/reports. Include binding/recheck costs,
parent/worker/descendant accounting and same-source/runtime identity checks.
These are host ceilings, not product/P4 budgets. No retry or increased limits.

Stop immediately on integrity, convergence, resource or incomplete-cohort failure:
preserve INVALID/INCOMPLETE artifacts and publish no musical pass. A complete
valid run with any musical veto is REJECTED. All gates passing on this exposed
cohort is EXPOSED_DIAGNOSTIC_PASS_ONLY, with independent qualification still open.
No tuning, alternate lambda, fallback, topology repair, subset reporting, model
change or repeat attempt after reading results. Original dense-v1 stays REJECTED.
Any later transfer, native/P4 work, confidence, acceptance or release needs its
own evidence and scope. Commit/push each checkpoint before the next.
