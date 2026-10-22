# Saved timing failures — measured attribution

Date: 2026-10-22. Exact clean implementation:
`6f480bce5df180261f0b01ac3200f131dae3ab7d`, committed and pushed before the run.
The [registered audit](TIMING-FAILURE-AUDIT.md),
[protocol](TIMING-FAILURE-AUDIT-PROTOCOL.json) and
[preflight](TIMING-FAILURE-AUDIT-PREFLIGHT.md) remain unchanged. The
[machine-readable result](TIMING-FAILURE-AUDIT-RESULT-20261022.json) records all
aggregates, cross-tabs, redacted regressions, resources and evidence hashes.

## Decision

**Separate variable timing representation from learned event/family errors
before implementing another detector.** A single original OLS fit of the
reference events themselves passes only 227/401 complete-context windows,
versus the saved learned-event projection's 187/401. In 106/401 windows, the
reference local periods have no common value within their +/-1% intervals.
This is a real obstacle to a *complete one-to-one constant-period sequence*;
it is not an impossibility theorem for the old precision-only scorer, nor proof
of physical musical tempo change instead of annotation jitter.

There are also substantial independent event/family discrepancies: 123/214
model-failing windows have raw correspondence mismatch indications, and 35/214
match a fixed non-unit period ratio. These overlap with reference variability.
Another small PCM timing shift or uniform context extension cannot be assumed
to address these different dimensions. The previous candidates remain rejected.

**Next: separately register bounded segmented-grid feasibility using reference
paths and the existing native contracts.** First determine whether a piecewise
period representation can preserve source ordinals, same-grid meter and
declared coverage within the eight-segment limit, or must explicitly report
unsupported capacity. Use reference-derived segments solely as representation
witnesses; validate actual native/wire behavior without treating them as model
output. This precedes a future learned-evidence decoder with variable timing
and explicit family ambiguity. No segmented estimator, reference-to-production
path or new model is implemented by this audit.

Follow-up: the separately registered
[segmented reference study is now complete](SEGMENTED-REFERENCE-RESULT-20261022.md).
It validates 47/100 complete anchored-family paths with at most seven segments
and all 135 supported meter bindings. The other 53 have no family path before
capacity is applied. The subsequent
[free-phase diagnostic](FREE-PHASE-RESULT-20261022.md) finds 75/100 continuous
paths and 25 necessary-relaxation contradictions under the same bounds. Next is
free-anchor quantized native construction; this audit's findings are unchanged.

## Fixed evidence and baseline

All 100 previously selected GTZAN sources / 600 original S6 windows remain.
Exactly 401 have complete reference context and 199 remain explicitly unknown.
Only saved Beat This! final0 events, original labels and coarse/OLS records are
read. No PCM, inference, new labels/corpus, formal holdout or reserved ASAP
transfer is opened. Every old tuple and frozen score reproduces exactly.

The unchanged model control still has 192 period-correct, 300 phase-correct
and 187 joint-correct complete-context windows. Its common one-to-one sequence
diagnostic remains 178; precision-only success does not certify all reference
beats are present. No existing score or acceptance threshold is changed.

## Reference-only constant-fit witness

The witness applies the original project function directly to reference events
and their downbeat indices on the same windows, then uses the same frozen scorer.
It is label-derived evidence about this one fit and representation, not detector
accuracy, an optimized oracle or an accuracy ceiling.

| Complete-context measure, denominator 401 | Model OLS | Reference-only OLS witness |
|---|---:|---:|
| Correct period | 192 | 228 |
| Correct phase | 300 | 374 |
| Joint period and phase | 187 | 227 |
| One-to-one phase coverage | 274 | 369 |
| Complete sequence including period | 178 | 227 |

Among the 401 complete-context witnesses, 394 emit a grid, five reject the
projection and two have insufficient events. Unsupported witnesses are retained.
The cross-tab makes its limitations explicit:

| Same-window verdict | Windows |
|---|---:|
| Model and witness both pass | 176 |
| Model fails, witness passes | 51 |
| Model passes, witness fails | 11 |
| Both fail | 163 |

The eleven model-only passes disprove interpreting 227 as an accuracy ceiling
or using witness failure as proof that no constant fit can pass. Of the 51
witness-only passes, sixteen have non-unit model period ratios and 35 have raw
event-correspondence mismatch flags; these flags can overlap. Eighteen have the
same nominal period family, fourteen an unmatched ratio, three no supported
projection and sixteen a recognized non-unit family.

## Reference-period compatibility

At each annotated in-window beat, retain the unchanged nearest-five-interval
local-period helper. Its integer/half-integer values are represented exactly.
Intersect `[0.99*local_period,1.01*local_period]` across all those beats.

| Period compatibility diagnostic | Model pass | Model fail | Total |
|---|---:|---:|---:|
| Nonempty common interval | 187 | 108 | 295 |
| Empty common interval | 0 | 106 | 106 |

An empty interval means one constant period cannot satisfy every annotated
beat's period requirement under complete one-to-one correspondence. It does
not classify the cause of reference variation: expressive timing, annotation
jitter and local-period helper behavior are not separated without additional
evidence. It also does not change the old scorer, which checks represented
points and can omit annotated beats. A nonempty interval is necessary rather
than sufficient for a valid phase-aligned, millibpm/Q32, bounded native grid.

## Family and raw-event observations

| Selected-period family, complete-context windows | All 401 | Model-failing 214 |
|---|---:|---:|
| Same nominal period | 290 | 103 |
| Double period / half BPM | 16 | 16 |
| Half period / double BPM | 13 | 13 |
| One-third period | 4 | 4 |
| Three-halves period | 2 | 2 |
| Unmatched ratio | 69 | 69 |
| Unsupported projection | 7 | 7 |

The other two registered ratios have zero observed windows. Matching a period
ratio is a diagnostic indication, not an independently verified correction.
No tempo, phase or metrical level is changed. All 35 non-unit observations occur
in model failures, but they are not the majority of those failures.

The 401 complete windows contain 4409 raw events and 4420 annotated beats.
Nearest-reference matching at the unchanged 0.10-local-beat tolerance gives
3840 uniquely covered in-window reference beats, 580 uncovered references,
559 phase-mismatched raw events and ten valid matches outside the original
window. Duplicate matches and unknown-context events are zero in these complete
windows. Nearly equal total event counts therefore do not establish sequence
correctness. These are mismatch indications against the existing labels, not
manual proof that an individual musical event or annotation is wrong.

Overlapping flags occur in 138/401 windows for event correspondence, 106/401
for reference-period incompatibility and 35/401 for non-unit families. Among
the 214 model failures their counts are 123, 106 and 35 respectively. Do not
add these flags as independent sources of error.

## Registered triage and original regressions

| First applicable display group, complete-context failures | Windows |
|---|---:|
| Unsupported model projection | 7 |
| Reference-period incompatibility | 102 |
| Non-unit tempo family | 26 |
| Event-correspondence mismatch | 34 |
| Period precision or projection | 45 |
| Phase or projection residual group | 0 |
| Total model failures | 214 |

Together with 187 model passes and 199 incomplete-reference windows, the
exclusive groups account for all 600 windows. Priority affects these counts:
for example, four incompatible windows enter unsupported projection first.
The groups are a display partition, not causal proof; overlapping flags remain
authoritative for coexistence.

Both original coarse-to-OLS regressions remain identified privately:

- The first has a half-period/double-BPM model family and raw correspondence
  mismatch. Its reference-only witness passes joint and complete-sequence
  diagnostics. The saved model fails both phase and period.
- The second has the same nominal period family, no raw correspondence mismatch
  and a nonempty common period interval. The model retains phase but fails
  period; the reference-only OLS witness also fails. This supports examining
  timing/fit precision separately and does not prove another constant fit is
  impossible. The original coarse grid already passed the frozen metric.

No source identity, coordinate sequence or private filename is published.

## Genre diagnostics

Each genre retains ten sources and sixty windows. The table uses only complete
reference contexts; it describes this cohort and is not model-independent
evidence about an entire genre.

| Genre | Complete windows | Model joint | Reference-witness joint |
|---|---:|---:|---:|
| Blues | 40 | 10 | 12 |
| Classical | 40 | 1 | 4 |
| Country | 40 | 17 | 15 |
| Disco | 40 | 37 | 37 |
| Hiphop | 40 | 29 | 33 |
| Jazz | 40 | 7 | 13 |
| Metal | 40 | 9 | 23 |
| Pop | 40 | 37 | 38 |
| Reggae | 40 | 14 | 22 |
| Rock | 41 | 26 | 30 |

## Verification and handoff

Seventeen new synthetic tests and 44 unchanged multi-beat/representation/
neural-reference/annotation tests pass: **61 Python tests**. The new
standard-library suite is in `rust/check.py`. Rust core, frozen scorers, all
previous candidates and consumer sources are byte-identical. No new full
Rust/C/sanitizer, hosted CI or physical P4 qualification is claimed.

The audit binds and rechecks 1228 files. A separate saved-record pass reloads
all 100 source records, reconstructs every diagnostic and reference witness,
replays all 600 old and witness scores, independently recomputes exact period
intervals and reproduces every aggregate/cross-tab and both regressions.
All detail hashes and original source/input bindings are checked again.

Measured audit processing and final recheck take **31.91 s** with peak RSS
**29832 KiB**, within the registered 120 s / 256 MiB host caps. Initial Git/hash
preflight and later independent replay/export are outside that timing. No
device resource or real-time claim follows.

Reproduction from the exact clean implementation requires the retained private
inputs and a new output location; the tool refuses overwrite:

```sh
python3 tools/apta_1_1_timing_failure_audit.py \
  --attempt "$PRIVATE_ROOT/neural-reference-20261022/comparison-source-domain" \
  --prepared "$PRIVATE_ROOT/neural-reference-20261022/prepared-availability" \
  --output "$NEW_PRIVATE_OUTPUT"
```

Private report/details:
`/home/daniel/.local/share/libapta-audio/rust-rewrite/timing-failure-audit-20261022/attempt/`.
Exact source archive, independent replay/export script and aggregate:
`D:/AI/LIBAPTA/outputs/rust-timing-failure-audit-20261022/`.

- Protocol SHA-256:
  `8f47045ccd26983b04085aabb4a58d2868f074528bb2031804aed2a3e6270867`.
- Private report SHA-256:
  `0fce2185c2d5fd4fd93e9809aa78274267e3ae9a58167af90282daaecae39afa`.
- Exact implementation archive SHA-256:
  `fa9252cec9a35b0a1202699b2a5c88c12bfc11f44bf93ea5a14700722c5260ab`.

This is read-only development diagnosis, not a new candidate or acceptance.
Cross-dataset identity remains unresolved; this cohort cannot become fresh
acceptance evidence for a tuned successor. Native adoption, calibrated confidence,
independent transfer, fresh product acceptance and physical P4 gates remain open.
Commit and push this measured audit before registering segmented feasibility.
