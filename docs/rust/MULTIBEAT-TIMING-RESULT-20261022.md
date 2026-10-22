# Bounded multi-beat timing — measured rejection

Date: 2026-10-22. Exact clean implementation:
`7503e837af2457eaa8730d1ddac2e6377db28fc6`, committed and pushed before scoring.
The [experiment](MULTIBEAT-TIMING-EXPERIMENT.md),
[fixed protocol](MULTIBEAT-TIMING-PROTOCOL.json) and
[synthetic preflight](MULTIBEAT-TIMING-PREFLIGHT.md) are unchanged. The
[machine-readable result](MULTIBEAT-TIMING-RESULT-20261022.json) contains all
aggregates, verdicts, break causes, resources and evidence hashes.

## Decision

**Reject `s6-four-neighbor-ols-v1`; do not implement it in native DSP.**
Joint correctness rises only 187 -> 188/401 complete-context windows, with
thirteen fixes and twelve breaks. The candidate fails every registered
no-regression veto. Both original coarse-to-OLS joint regressions still fail.
Keep the exact code and outputs as a rejected development experiment. No
parameter/context search, fallback, phase rescue or same-cohort rerun follows.

This closes the proposed first multi-beat reconstruction experiment. It does
not show that all possible timing reconstruction is impossible. The specific
longer-context fit introduces both accepted timing errors and support loss;
removing rejection guards alone would not repair those accepted errors.

The next proposed unit is a separately registered **anchor-guided PCM timing
evidence experiment**, before another event-position-only fit or model/backend
port. Keep the learned musical anchors fixed and investigate independent,
finer timing evidence in the PCM around them, without claiming every musical
beat is an attack or silently correcting half/double tempo. Its evidence,
algorithm, resource caps, ambiguous/no-attack behavior, untouched transfer
partition and veto must be fixed before implementation or inference. No such
refiner is implemented or measured by this checkpoint.

## Fixed run and musical metrics

The single registered cohort contains all 100 previously selected GTZAN
development sources, 600 unchanged S6 windows, 401 complete reference contexts
and 199 incomplete contexts. Only saved Beat This! final0 events and existing
labels/coarse/OLS records are read. No audio decoding, model execution, new
annotation, holdout access or production publication occurs.

The candidate fits original consecutive event indices using the in-window
events and up to four neighbors on each side, requires nine support events,
uses exact rational OLS and the existing millibpm/Q32 convention, and retains
the original event count and ordinals. Unsupported context, tempo, residual,
anchor or count fails explicitly without fallback. Same-grid meter uses the
original two-complete-equal-bars rule and reconstructed downbeat positions.

| Diagnostic | Unchanged OLS | Candidate |
|---|---:|---:|
| Emitted windows, all 600 retained | 560 | 445 |
| Correct period, complete-context windows | 192/401 | 193/401 |
| Correct phase, complete-context windows | 300/401 | 275/401 |
| Correct period and phase | 187/401 | 188/401 |
| One-to-one phase coverage | 274/401 | 254/401 |
| Complete sequence including period | 178/401 | 179/401 |
| Correct meter / emitted meter records | 195/215 | 179/193 |
| Correct downbeat / reference-scored downbeats | 167/207 | 155/186 |

The old constant-grid scorer is unchanged. Its candidate period/phase/joint
verdicts agree with the unchanged common point diagnostic. One-to-one sequence
coverage is an additional diagnostic; it does not replace the frozen scorer.
Conditional meter/downbeat fractions are not whole-corpus acceptance rates.

## Registered gates and paired regressions

| Gate | Measured | Verdict |
|---|---:|---|
| At least 188 joint-correct windows | 188 | pass |
| At least 179 complete-sequence windows | 179 | pass |
| Zero OLS period breaks | 13 breaks / 14 fixes | fail |
| Zero OLS phase breaks | 28 breaks / 3 fixes | fail |
| Zero OLS joint breaks | 12 breaks / 13 fixes | fail |
| Zero coarse joint breaks | 4 breaks / 145 fixes | fail |
| Zero old-correct meter breaks | 16 | fail |
| Zero old-correct downbeat breaks | 12 | fail |

Abstention counts as a lost old-correct result. Of the twelve OLS joint breaks,
six have an accepted but incorrect candidate fit, three reject inconsistent
events and three reject changed beat counts. Period breaks contain seven
accepted wrong fits and six abstentions. Phase breaks contain two accepted
wrong fits and 26 abstentions: twenty inconsistent-event, four count and two
anchor-boundary rejections. All sixteen meter and twelve downbeat breaks occur
with lost timing support; they remain failures rather than being excluded.

Both original coarse-to-OLS regressions produce accepted candidate fits. One
still fails both period and phase; the other retains phase but fails period.
Their private identities and per-source details remain outside Git. They cannot
be removed or rescued by changing the evaluation.

## Coverage, confidence and native boundary

All 600 windows are retained: 445 accepted, 85 inconsistent-event, 38
insufficient-event, 21 out-of-window-anchor, eight changed-count and three
unsupported-tempo outcomes. Only 35/100 sources emit all six window segments.
An accepted source partition is not evidence that its musical result is right.
Unknown support is kept separate from known unavailable PCM; no beat is created
to bridge an abstention. Accepted neighboring windows retain strictly ordered
positions and consecutive original ordinals.

Confidence remains uncalibrated with placeholder zero. The reported zero
high-confidence errors is not confidence-safety qualification. Since musical
gates fail, no candidate native graph, wire adoption or consumer activation is
attempted. Existing validators, eight-segment limits, production M1 dependency
pin and providers are unchanged. The earlier native/consumer audit remains the
contract evidence, not evidence that this reconstruction is acceptable.

## Reproduction and evidence

All 18 new synthetic tests and 26 unchanged Python reference/representation/
annotation tests pass. `rust/check.py` includes the new lightweight suite.
Rust core, native validators/examples, frozen scorers and consumer sources are
unchanged; full Rust/C/sanitizer/P4 matrices are not rerun or claimed for this
Python host experiment.

The run binds 1213 files before execution, including exact tracked code, old
freeze and scores, prepared labels and saved events; it rechecks them afterward.
An independent saved-record pass reconstructs every candidate from its event
inputs, replays all old/candidate scores and common diagnostics, verifies
partition/count/meter ordinals, hashes all 100 details, and reproduces aggregate
counts, paired changes, original regressions and gate verdicts. No baseline
tuple, input or scorer changed. All 100 records independently reproduce.

The measured run takes **27.41 s**, with peak RSS **26520 KiB**, within the
registered 120 s / 256 MiB host caps. Initial source/input preflight and later
independent export/reverification are outside that timing. These are host
measurements with no physical or real-time P4 implication.

Example reproduction, from the exact clean source in WSL, requires the retained
private artifacts and a new output location; the tool refuses overwrite:

```sh
python3 tools/apta_1_1_multibeat_timing.py \
  --attempt "$PRIVATE_ROOT/neural-reference-20261022/comparison-source-domain" \
  --prepared "$PRIVATE_ROOT/neural-reference-20261022/prepared-availability" \
  --output "$NEW_PRIVATE_OUTPUT"
```

Private report and details:
`/home/daniel/.local/share/libapta-audio/rust-rewrite/multibeat-timing-20261022/attempt/`.
Exact source archive, verification log, saved-record export script and aggregate:
`D:/AI/LIBAPTA/outputs/rust-multibeat-timing-20261022/`.

- Protocol SHA-256:
  `66ab4e3a5fa05dd5b4eb120609ecfa79d900e8876a131005e147275e02b67bd9`.
- Private report SHA-256:
  `904d268c4ad897d4c4db8163605ad2ea58670ed733d847617cc28807b77f126c`.
- Exact implementation archive SHA-256:
  `8a5dc2c0ddf5704788178bb9ea2fa4499813fe3c2ac62585e91df41e99337913`.

This benchmark remains development evidence with unresolved cross-dataset
recording/composition overlap. It cannot become fresh acceptance evidence for
a tuned successor. Independent transfer, calibrated confidence, fresh product
acceptance, native integration and physical P4 gates remain open. Commit and
push this rejection before registering the next independent evidence axis.
