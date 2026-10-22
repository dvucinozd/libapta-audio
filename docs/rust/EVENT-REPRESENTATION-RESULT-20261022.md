# Saved Beat This! events — representation audit result

Date: 2026-10-22. Exact clean source:
`5a78304693d84b0914b4142b6a5dda2689f7ae3b`, committed and pushed before scoring.
The [registered protocol](EVENT-REPRESENTATION-PROTOCOL.json),
[experiment](EVENT-REPRESENTATION-EXPERIMENT.md) and
[preflight](EVENT-REPRESENTATION-PREFLIGHT.md) remain unchanged. The
[machine-readable result](EVENT-REPRESENTATION-RESULT-20261022.json) records
aggregate metrics, native/consumer outcomes, resources and evidence hashes.

## Decision

**Changing the grid representation alone does not solve timing accuracy.**
The existing explicit payload preserves every learned event, but raw adjacent
intervals fail the 1% period diagnostic far more often than the previous
constant-period fit. Exact segment compression usually exceeds the wire limit.
Do not adopt the raw-event grid, increase capacity limits, weaken validation or
promote the offline model on this evidence.

Keep Beat This! as the fixed host musical reference. The next unit is a
separately registered **bounded multi-beat timing reconstruction**: estimate
period and phase across multiple learned beats rather than publishing each
20-ms-quantized adjacent interval, with explicit segmentation/gap/ambiguity
rules. Define the algorithm and caps before scoring, preserve both original
regressions and the immutable event/scorer baselines, and compare against the
existing OLS control. Do not start another model/backend port or change the
wire/consumer contract before that timing path demonstrates transfer.

This is the result of the approved audit, not an implementation of that next
timing estimator. Standalone P4 inference, calibrated confidence, independent
transfer, fresh acceptance and native product adoption remain separate gates.

## Frozen inputs and execution

Reuse the completed neural comparison's saved Beat This! final0 event outputs,
frozen development labels and all native/coarse/OLS score records: **100 GTZAN
sources, 600 S6 windows, 401 complete reference contexts and 199 incomplete**.
No audio is decoded, model executed, checkpoint changed, label added or holdout
opened. Exact source coordinate and original sequence ordinal are retained.

The unchanged scorer exactly reproduces every old coarse/OLS tuple, per-window
verdict and aggregate, including 187 OLS joint-correct windows and both
coarse-to-OLS joint regressions. Its constant-grid contract remains authoritative
for that old result. A separately named common diagnostic compares point phase
and outgoing interval against the same local reference helper and tolerances.
The OLS control reproduces the old period/phase/joint verdicts under that common
diagnostic. A further one-to-one diagnostic requires all annotated beats in the
window, preventing apparent success by omitting difficult events.

## Musical timing observations

| Diagnostic, complete-reference windows | OLS control | Exact raw events |
|---|---:|---:|
| All point phases within 0.10 local beat | 300/401 | 290/401 |
| All outgoing periods within 1% | 192/401 | 10/401 |
| Joint period and phase | 187/401 | 10/401 |
| One-to-one phase coverage of all reference beats | 274/401 | 263/401 |
| Complete sequence including the period criterion | 178/401 | 9/401 |

Raw versus OLS common diagnostics have **zero joint fixes and 177 joint breaks**;
period has zero fixes and 182 breaks; phase has four fixes and fourteen breaks.
The outgoing raw successor is retained even when it lies outside the current
window. Missing successors are unknown, never correct; 58 such points remain
explicit across the whole source/window set. All 600 windows remain in reports.

The raw sequence has **6095 events, all exactly on a 960-source-frame lattice**
at 48 kHz, corresponding to the model's 20 ms feature hop. Of 5913 applicable
reference intervals at those lattice points, **2771 (46.86%) have no integer
multiple of 960 frames within 1% of the local annotated period**. This is a
diagnostic constraint at these reference coordinates, not a newly introduced
acceptance gate or a bound on every possible timing estimator.

The 50-Hz event positions remain useful musical anchors; their adjacent
differences are inadequate as precise instantaneous beat periods. At 120 BPM,
1% of one beat is 5 ms, smaller than one 20 ms hop. Estimating a period over
multiple beats can average timing quantization, whereas copying adjacent
intervals preserves it. The audit does not establish that every remaining
error is quantization or that any particular reconstruction will pass.

The OLS grid contains 5771 points. Its displacement to the nearest raw event
has median 274.56 source frames (5.72 ms), maximum 40896.30 frames (852.01 ms).
The maximum includes substantial disagreement; it must not be characterized
as rounding noise alone. Both original coarse-to-OLS joint regressions also
fail raw joint timing. In one, raw phase remains wrong; the other retains phase
but fails the period criterion. These regressions cannot be blamed solely on
the OLS representation.

## Existing native representation and validation

| Actual native check, full 100-source graphs | Outcome |
|---|---:|
| Explicit payload write/strict read/canonical roundtrip | 100/100 preserve all coordinates and ordinals |
| Exact equal-interval segments needed per source | min 8, median 33, max 114 |
| Graphs exceeding eight wire segments | 98/100 |
| Segmented wire roundtrips within all caps | 2/100 |
| Out-of-range exact segment periods | 76 segments; rejected, never clamped |
| Explicit complete native-result validation | 0/100 accepted |
| Explicit complete external-builder validation | 0/100 accepted |
| Segmented complete native-result / builder validation | 0/100 / 0/100 accepted |

Python compression and independent Rust expansion preserve all source events
without duplicating shared segment boundaries, including graphs that are too
large to serialize. A capacity failure leaves the full graph identified; no
partial/truncated grid is counted as a successful roundtrip.

Payload preservation is distinct from complete-result acceptance. Existing
native and external-builder validation couples explicit adjacent positions to
one selected TEMP with <=1 millibpm arithmetic tolerance. A post-run structural
explanation verifies that all 100 event sequences have disjoint feasible
single-tempo intervals under that rule: choosing another convenience TEMP
would not make those explicit sequences pass. This is an explanation, not a
new candidate selection or gate. The unchanged whole-source OLS also abstains
on 24 sources, retained without fallback. Segmented graphs retain each exact
period but face supported-tempo, selected-tempo and wire-capacity constraints.
No validator was modified or bypassed.

The event envelope is `[first event,last event+1)`, rather than asserted
full-source coverage. Continuous input PCM provides no known missing-input
gap in this cohort. Missing/extra musical events must not be reclassified as
PCM gaps or silently filled. The synthetic consumer probe separately exercises
known unavailable ranges and preserves their positions and nonconsecutive
ordinals without inventing beats in the gap.

## Same-grid meter and actual consumer

The unchanged two-equal-complete-bars support rule yields 223 raw meter records,
202 with matching reference numerator/denominator. Their exact event coordinates
and source ordinals survive MTRD write/read and grid lookup in **223/223** cases.
Downbeat reference context permits 211 coordinate checks, of which 170 pass
0.10 local beat. These conditional counts are not full-corpus acceptance rates;
unsupported windows and boundary context remain present. The old OLS result
retains its original 215 meter records / 195 matching labels and 167/207 correct
scored downbeats. No meter record is borrowed onto another grid.

The actual M1 adapter at `0f2fb1425ef76f2541a8c76841cfa52178dca589` and production
libapta pin `25d09de364e9507532c0508c838a8fecf0772d13` are unchanged. Eight
synthetic policy cases pass in an isolated host dependency override:

- a structurally validated final constant explicit control converts to beats;
- a provisional graph produces tempo-only output with an untouched beat bank;
- known gaps and multiple meters pass native structure validation but are
  rejected by the consumer's full-source/single-meter subset;
- capacity, pending revision and duplicate-coordinate failures are transactional;
- variable explicit intervals fail native validation before adoption.

This probes the actual subset converter and its validation boundary, not a
trusted production-owner handoff. No real graph is marked final to gain
acceptance; no playback grid, firmware provider or dependency pin is activated.
Unknown confidence remains uncalibrated; placeholder zero cannot qualify safety.

## Reproducibility and next handoff

Verification passes ten representation Python tests plus sixteen unchanged
projection/annotation tests. Four Rust example tests and 26 focused existing
builder/grid/native-validator tests pass in debug and release with warnings as
errors. Rustfmt, focused Clippy and no-default-features compilation pass. Core
DSP, the frozen scorer, production native validators and consumer source are
unchanged. Full DSP/C/P4 matrices and sanitizer reruns are not claimed.

The audit binds 1223 files including 902 project source files, prior results,
saved events, truth, native binary, and actual adapter/dependency source. Every
binding is rechecked after the run. A separate saved-record pass reloads all
100 native/detail records, reproduces the OLS/raw point diagnostics and meter
bindings, and reproduces both old coarse/OLS aggregates. All old inputs remain
byte-identical.

Measured scoring/audit wall time including its final recheck is **16.92 s**;
process peak RSS is **27216 KiB**. Initial Git/hash preflight and later independent
export/reverification are outside that timing. The registered 120 s / 256 MiB
host caps pass; no device resource or real-time claim follows.

The initial invocation stopped in consumer preflight because WSL Git used
different CRLF clean-filter semantics than the owning Windows Git. It created
no output directory and scored no events. The committed operational correction
uses equivalent per-command Git settings while retaining exact consumer byte
hashes; repository configuration and files were not changed.

Private evidence:
`/home/daniel/.local/share/libapta-audio/rust-rewrite/event-representation-20261022/attempt/`.
The complete report is `report.private.json`; per-source native/raw/meter scores
remain outside Git. Exported aggregate, exact source archive, consumer probe
manifest/lockfile and verification log are under
`D:/AI/LIBAPTA/outputs/rust-event-representation-20261022/`.

- Protocol SHA-256:
  `86cc8fec3ff7488c613b555248b6bb7a70bb8eeeca52a8e22f01bf9e4cc21b8c`.
- Private audit report SHA-256:
  `18a48d79fb05a63d52828c2e8b429cddb407f2e444291eac128741479553ddc5`.
- Exact source archive SHA-256:
  `d87438ad4322f19036b6917f1a6125d4ee1c9a74a5efda0bf346249daf36faa2`.

This cohort remains benchmark development evidence with unresolved cross-dataset
recording/composition overlap. Formal holdouts and fresh product acceptance
stay closed. Commit and push this completed audit before registering the next
timing reconstruction; stable version, release and production defaults remain
unchanged.
