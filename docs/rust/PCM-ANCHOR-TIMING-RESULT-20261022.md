# Fixed-anchor PCM timing — measured rejection

Date: 2026-10-22. Exact clean implementation:
`f09263db482268839b2f69fb4309fe7664e4207f`, committed and pushed before processing.
The [experiment](PCM-ANCHOR-TIMING-EXPERIMENT.md),
[protocol](PCM-ANCHOR-TIMING-PROTOCOL.json) and
[preflight](PCM-ANCHOR-TIMING-PREFLIGHT.md) are unchanged. The
[machine-readable result](PCM-ANCHOR-TIMING-RESULT-20261022.json) contains all
scores, gates, movements, resource measurements and evidence hashes.

## Decision and next step

**Reject `fixed-anchor-stereo-energy-rise-v1` and stop before ASAP transfer.**
The unchanged OLS control has 187/401 joint-correct complete-context windows;
PCM-refined projection has 186/401, with two fixes and three breaks. It also
fails period/phase no-regression and complete-sequence improvement. Both old
coarse-to-OLS regressions remain, with one additional coarse joint regression.
No radius, energy, contrast, dominance, fallback or context adjustment follows.
The forty-source ASAP transfer reserved in the protocol is not executed; only
its manifest metadata/hash was inspected during registration. No transfer audio,
labels or inference is opened for this candidate.

Local PCM energy rises do not establish more reliable beat timing under this
fixed hypothesis. The preceding raw-event, longer-context and local-rise results
remain rejected baselines. This does not establish that every PCM or timing
algorithm is ineffective, and does not justify another local-threshold search.

**Next: separately register a read-only failure-attribution audit before choosing
another estimator.** Classify existing errors into reference/boundary support,
missed/extra or wrong-phase events, tempo-family mismatches and within-window
tempo variation. Include reference-only constant-fit diagnostic witnesses to
separate timing representation from learned event selection; a witness is not
an accuracy ceiling or a detector. Preserve the old scorer and both original
regressions, and do not use such diagnoses as an automatic selection oracle.
No new classifier, candidate or corpus run is implemented by this checkpoint.

Follow-up: the separately registered
[failure-attribution audit is now complete](TIMING-FAILURE-AUDIT-RESULT-20261022.md).
It finds 106 reference-period-incompatible windows and a reference-only OLS
witness with 227/401 joint passes, while identifying overlapping event/family
errors. Its selected
[segmented reference study](SEGMENTED-REFERENCE-RESULT-20261022.md) is now
measured: 47/100 full anchored-family paths validate within the cap. The
[free-phase follow-up](FREE-PHASE-RESULT-20261022.md) finds 75/100 continuous
paths and 25 necessary-relaxation contradictions. The
[terminal quantized study](QUANTIZED-GUIDED-RESULT-20261022.md) reaches 60/100
wire-qualified sources and closes reference construction. Next is label-blind
learned-event timing, now
[measured and rejected](LEARNED-PIECEWISE-RESULT-20261023.md). Next is upstream
logit capture/event parity; this PCM rejection is unchanged.

## Fixed execution

All 100 previously selected GTZAN sources and 600 S6 windows are retained,
including the original 401 complete and 199 incomplete reference contexts.
The input is the existing hash-bound 48-kHz stereo PCM16 canonical audio and
saved Beat This! events. There is no new model, inference, audio preparation,
label, dataset selection or formal holdout access.

One source is processed at a time. Energy sums square each stereo channel
separately and remain exact int64. The fixed scan uses 41 positions over +/-20 ms,
at 1-ms spacing, comparing 2-ms before/after energy. Only sufficiently energetic,
contrasting, isolated rises may move an anchor. Unsupported measurements retain
their original coordinate and explicit reason. Known unavailable PCM is tested
synthetically and never converted to silence or a fictitious attack.

All 6095 original events, their global ordinals, downbeat associations and S6
memberships survive refinement. The candidate then uses the unchanged original
OLS and same-grid meter rule. Event identity preservation applies to these
refined input events; the original constant-grid projection still enumerates
its own points to the window end. Its point count changes from 5771 to 5769 and
is reported explicitly, without forcing a count or changing the projection.

## Musical results and frozen vetoes

| Diagnostic | Original OLS | PCM-refined OLS |
|---|---:|---:|
| Emitted windows / all windows | 560/600 | 560/600 |
| Correct period, complete-context windows | 192/401 | 192/401 |
| Correct phase, complete-context windows | 300/401 | 300/401 |
| Joint period and phase | 187/401 | 186/401 |
| One-to-one phase coverage | 274/401 | 271/401 |
| Complete sequence including period | 178/401 | 175/401 |
| Correct meter / emitted records | 195/215 | 195/215 |
| Correct downbeat / reference-scored records | 167/207 | 167/207 |

Unchanged aggregate period or phase counts do not mean no regressions: period
has two fixes and two breaks, phase also two fixes and two breaks. Joint has
two fixes and three breaks. All 387 changed projection tuples remain recorded.
Both old regressions still fail period; one also fails phase, while the other
retains phase. The third coarse joint loss is a new identity, so that gate fails.
Old-correct meter and downbeat breaks remain zero, passing those two vetoes only.
The remaining six gates fail; the conjunction rejects the candidate.

Raw/refined event diagnostics are separate from projection accuracy. Direct
outgoing intervals worsen from 10 to four joint-correct windows, and complete
event sequences from nine to three. The paired point-phase comparison on the
same 5971 reference-scorable original/refined event pairs has fourteen fixes
and fifteen breaks, with 124 unknown pairs. The refined-only aggregate scores
five additional points at annotation boundaries; this explains why its raw
phase-correct point count can rise by four without a paired phase gain.
Incomplete reference context is never inferred correct or removed.

## PCM support and evidence level

| Anchor verdict | Events |
|---|---:|
| Supported energy rise | 797 |
| Ambiguous competing rises | 5116 |
| Insufficient normalized contrast | 94 |
| Search-edge winner | 49 |
| Incomplete source context | 31 |
| Insufficient absolute energy | 5 |
| Window/midpoint ownership rejection | 3 |
| Total original events | 6095 |

Only about 13% of anchors have supported rises. Of these, 774 move and 23
remain at their original coordinate. Absolute movement among moved anchors
has median 528 frames (11 ms), maximum 912 frames (19 ms); search-edge +/-20-ms
winners are not accepted. No original beat is deleted or new beat inserted.
Ambiguous/unsupported musical evidence is not a missing-PCM declaration.

The confidence placeholder remains zero and uncalibrated. Reported zero
high-confidence errors is not confidence-safety acceptance. No candidate native
graph, consumer handoff or P4 implementation is attempted after musical rejection.
Native validators/limits, actual consumer pin/providers, default features and
stable version remain unchanged.

## Verification and reproducibility

All 21 new PCM synthetic tests and 44 unchanged multi-beat/representation/
reference/annotation tests pass: **65 Python tests**. NumPy 2.5.2 is pinned for
this explicitly invoked host experiment; it is not a production/default-CI
dependency. Rust/C/scorers/consumer code remain byte-identical, and no new full
Rust/C/sanitizer/hosted-CI or physical-device qualification is claimed.

The run freezes and rechecks 1749 files, including every full canonical WAV,
saved event/result, labels, tracked source, Python/WAV and installed NumPy code.
An independent saved-record pass rereads every PCM prefix and reproduces every
energy decision, coordinate, ordinal and meter association. It reconstructs all
candidate/control tuples, replays all 600 frozen scores and common diagnostics,
and matches aggregate movements/counts, paired regressions and gate verdicts.
Every per-source detail hash and all original input bindings are checked again.
No label or old coarse/OLS tuple changes.

Measured stage-A processing/scoring and final recheck take **30.02 s**, with
peak RSS **78732 KiB**, within the fixed 180 s / 256 MiB host caps. Initial
input/source/runtime preflight and later independent replay/export are outside
that timing. These measurements establish no physical P4 or real-time bound.

Run command, from the exact clean implementation in WSL with retained private
artifacts and a new output directory (overwrite is refused):

```sh
python3 tools/apta_1_1_pcm_anchor_timing.py \
  --attempt "$PRIVATE_ROOT/neural-reference-20261022/comparison-source-domain" \
  --prepared "$PRIVATE_ROOT/neural-reference-20261022/prepared-availability" \
  --output "$NEW_PRIVATE_OUTPUT"
```

The private report and all per-event/source records remain at
`/home/daniel/.local/share/libapta-audio/rust-rewrite/pcm-anchor-timing-20261022/attempt/`.
Exact source archive, independent replay/export script and aggregate are under
`D:/AI/LIBAPTA/outputs/rust-pcm-anchor-timing-20261022/`.

- Protocol SHA-256:
  `0668dc34481a8c6021ea972866ed02d66f763694c8dc2607475143f5febc72de`.
- Private report SHA-256:
  `8b01dd4e3c5ca7f6ac9886494028604ff132e743eb277ed9d9575e97f9767bea`.
- Exact implementation archive SHA-256:
  `9eee891788f1c32fd91cf5d40d978336a682bfacdd16a42f8f0d7c3c1ea2de44`.

This cohort remains already-open development evidence, with unresolved
cross-dataset recording/composition overlap. Fresh product acceptance,
independent transfer, calibrated confidence, native musical adoption and
physical P4 qualification remain open. Commit and push this measured rejection
before starting a separately registered diagnostic audit.
