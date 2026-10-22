# Event-supported beat-lattice development oracle

Pre-registered 2026-10-22 before candidate code or label scoring. Base is
`1876eb34a9169299ece0d908927850e6f6e79670` plus the exact dirty baseline snapshot
and inventory in external `beat-events-20261022/baseline-source`. Preserve the
previous novelty 0/360 result and all thresholds. This is a host oracle, not a
new native grid, publication, production option or FINAL acceptance exporter.

## Recovered reference and scope

The ignored `build-wp0-private/labels.csv` matches the independently verified
60-row export SHA-256 recorded in `APTA-1.1-FINAL-DJ-CORPUS-STATUS.md`:
`e7eac4ab8a80019b3da558c347d36242b827e485c7c66dff27c72fa8c25abbb8`.
Validate its canonical manifest and exact opaque IDs against the already frozen
60 audio rows and every diagnostic output. Do not trust pre-review workbook
hints. This recovery changes availability of labels, not freshness of the set.
The set is spent development, never a new qualification attempt. All previous
reports saying labels were unavailable remain valid descriptions of those runs.

Use precisely the same first 30 seconds of each source at original 48 kHz stereo
PCM16 geometry. Reuse all three immutable previous-mode outputs. Add an explicit
trace of the actual native novelty peaks (source frame plus novelty strength);
do not reconstruct peaks from Python audio or change the front end. No new
source selection, fresh pool, formal holdout or external download is involved.

## Single changed hypothesis: beat support versus acoustic events

Not every onset must be a beat. Search periodic grids supported by multiple
events, allowing extra onsets and absent/weak beat support. Freeze this oracle:

1. For every pair of the <=128 native peak events and each integer step 1..8,
   propose `(right-left)/step`. Keep periods corresponding to 40..300 BPM,
   quantize to the nearest 64 source frames (half up), and accumulate vote
   `min(left_strength,right_strength)/step`. Keep the top 24 distinct periods
   by descending vote, ties by smaller period. No reference-label proposals.
2. For each period, consider each observed event as phase. Enumerate the exact
   grid beats inside this independent window. Events within 0.10 period support
   their nearest beat. For each beat take the largest supporting strength.
   Weighted coverage = sum of those maxima / (beat count * maximum event
   strength). Explained mass = sum of all supporting event strengths / total
   event strength. Score = their harmonic mean. Require >=8 grid beats.
3. Keep each period's best phase, ties by earliest source anchor. Sort by score,
   then smaller period, then earlier anchor. Treat alternatives with <=1%
   period difference AND <=0.10 winning-period cyclic phase difference as the
   same hypothesis; the first distinct alternative is the runner-up.
4. Abstain unless weighted coverage >=0.75, explained mass >=0.50, and winning
   score exceeds the distinct runner-up by >0.05 (absent runner-up scores zero).
   Confidence is fixed at 49. No default tempo, half/double exception or fallback.
5. Convert the winner to nearest integer milli-BPM and a first source-local
   whole-frame anchor using the proposed period; score the resulting exact Q32
   grid, including round-trip effects. Never borrow a meter or downbeat from a
   different grid. The oracle proposes timing only; it does not synthesize meter.

All numbers and tie rules are frozen before inspecting scored results. This
point-event support model differs from the rejected C WP2-I3 path over existing
top-three broadband-flux candidates. It creates proposals only from the new
energy-novelty events; it does not promote that old rejected experiment.

## Metrics and stop rules

Use canonical label loading, period tolerance (1%) and cyclic downbeat tolerance
(0.10 beat) from the existing frozen evaluator, without modifying it. Separately
report diagnostic beat-phase correctness over **every** represented grid beat,
joint period/all-beat-phase correctness, coverage, meter/downbeat correctness,
high-confidence wrong emissions and fixes/breaks against the retained coarse
windows. Abstentions are unsupported failures in unconditional denominators.
Window-level metrics are not track-level FINAL acceptance metrics. Constant
reference period/bar phase is the existing DJ-label model, not dense new manual
annotations; state this limitation explicitly.

Reject before native implementation if there is no joint-correct improvement
against coarse, any previously joint-correct coarse window becomes wrong or
unsupported, any high-confidence safety regression, or zero support. Reject or
invalidate on source/label/output hash mismatch, mismatched cohort, missing
events, incomplete capture, changed retained outputs or processing allocation.
Do not change formula, candidates, thresholds or labels after the run. Even a
passing spent-set result requires independently labeled external development
transfer before native selection or product adoption; formal holdouts stay shut.

## Resource and verification boundary

Add a separate diagnostic trace feature: caller-owned <=128 events per history
window, each at most 16 host bytes, plus small count/completeness metadata.
It may grow experimental trace records only; default/coarse/sparse/novelty
output bytes and non-trace layouts must remain unchanged. No core heap or unsafe
code. Capture peaks in the existing bounded extraction pass, without rerunning
PCM or inventing lost evidence. Trace capacity loss is explicit and fatal to
this oracle run. Host JSON/Python work may allocate.

The host selector has <=128*127/2*8 pair proposals, <=24 period states and <=128
phase states per window. Its processing/storage are host-only and must not be
claimed as P4 cost. Verify native trace/source coordinates and old output bytes,
synthetic extra/weak/missing-event behavior, ties, silence, input corruption,
source/label hash binding and core zero allocations before the recorded run.
Run relevant warnings-as-errors, no-default-library and sanitizer checks.
Keep all private labels/manifests/traces outside Git and record exact source,
binary, inventory, report hashes, runtime and Git state.

## Implementation and software checks

`experimental-s6-onset-trace` adds a complete/incomplete caller-owned capture to
SelectionRecord. Actual host sizes: event 16 bytes, OnsetTrace 2056 bytes,
SelectionRecord 2120 bytes with trace / 64 bytes without it. Peak extraction
calls the capture observer in its existing pass; its arithmetic, thresholds and
grid outcome remain unchanged. Capture overflow/lost context is explicit.
The actual Session allocation counter covers enabled trace capture.

Build `apta-history-diagnostic` with runtime `onset-trace-diagnostics` and use
`--mode=novelty-trace`, or invoke the canonical development runner with
`run --candidate novelty --trace`. The extra mode includes native source-frame
events; normal JSON omits trace fields. The runner checks the stripped trace
output equals the ordinary novelty output and keeps every source/window.

`tools/apta_1_1_native_event_lattice.py` loads hash-bound diagnostics and the
recovered verified labels through canonical helpers, then produces source-bound
window metrics and frozen selector proposals. It uses no audio decoder and does
not run the FINAL-only acceptance evaluator. Labels are not passed to `select`.
It checks cohort, full input identity, scopes, native output and label hashes,
complete event traces, exact retained baselines and overwrite protection.

Rust verification passes 289 default / 323 all-feature workspace tests in each
debug and release profile, plus 45 sparse-only focused tests. Clippy for all
targets/features, rustfmt and all-feature no-default-library compilation pass.
Seven protocol and five host-oracle tests pass with the release trace binary.
AddressSanitizer with leak detection also passes 61 focused core/history tests,
all seven protocol tests and all five host-oracle tests invoking the instrumented
native binary. Python/prebuilt std are uninstrumented. The non-trace onset build
passes 47 focused tests and six applicable protocol cases (trace case skipped).
Synthetic tests cover weak extra events, a missing beat, source-local offsets,
all-represented-beat drift, beat versus bar phase, silence, competing periods,
hash/cohort rejection and unchanged output files on overwrite attempts.

An initial synthetic assertion expected ambiguity between two phases of the
same period. The frozen model keeps only one best phase per period, so that
assertion contradicted the registered rule. The test now records this design
limitation explicitly and separately exercises ambiguity across distinct
periods. No selector formula, threshold or recorded-data choice changed. Same-
period phase alternatives are not protected by the runner-up margin; the oracle
must not be presented as a general phase-ambiguity solution.

## Recorded result: rejected before native selector implementation

All 60 verified opaque label IDs match the frozen canonical audio rows. All
four native modes complete the same 86400000 source frames / 360 windows without
lost or incomplete evidence. All 180 ordinary coarse/sparse/novelty JSON files
are byte-identical to the prior frozen run. Trace-stripped JSON also matches
ordinary novelty in every window. The canonical evaluator is unchanged.

| Window measure (denominator 360) | Coarse S6 | Sparse | Novelty | Host event oracle |
|---|---:|---:|---:|---:|
| Emitted timing | 360 | 0 | 0 | 65 |
| Correct period | 235 | 0 | 0 | 21 |
| Every represented beat has correct phase | 106 | 0 | 0 | 16 |
| Joint period / all-beat phase correct | 95 | 0 | 0 | 16 |
| Emitted wrong lattice | 265 | 0 | 0 | 49 |
| High-confidence wrong grid | 116 | 0 | 0 | 0 |

Unconditional joint correctness falls from 95/360 (26.4%) to 16/360 (4.4%).
There are six fixes and 85 breaks: ten previously correct windows become wrong
emissions, and 75 become unsupported. All 360 timing verdicts change. Among the
65 emitted proposals, 49 (75.4%) are wrong, compared with 265/360 (73.6%) coarse
emissions. Forty-four oracle emissions have a wrong period; another five have a
correct period but wrong all-beat phase. This is not a coverage-only rejection.

The oracle emits 65 proposals and abstains in 103 ambiguous and 192 insufficient-
support windows. Sparse and ordinary novelty retain zero support; their
conditional false-lattice rates are null, not perfect precision. Oracle
confidence stays at 49, so zero high-confidence errors do not establish accuracy
or a successful safety qualification. Coarse meter is available in 288 windows,
correct in 232; its downbeat is correct in 25. These are window diagnostics
against the verified constant-period/bar-phase reference, not the historical
full-track FINAL acceptance metrics and not dense manual beat annotations.

Both the joint-improvement and zero-break gates fail. The candidate is rejected
before native selector implementation. Do not adjust its pair voting, beam,
phase collapse, support thresholds or margin to rescue this set. No algorithm
or label changed after scoring. The host oracle and native trace remain useful
as frozen diagnostic/regression evidence, with production selection unchanged.

Next: obtain the already-defined external ASAP/Ballroom **development** inputs
and their source-aligned beat/downbeat coordinates for this native diagnostic
path, preserving holdouts. Use the recovered DJ labels as spent regression
evidence. A subsequent experiment must explicitly handle phase alternatives
and metrical/tempo-family ambiguity and be pre-registered independently; this
failed oracle does not authorize native publication or product adoption.

## Reproduction evidence and cost

The serial native 240-process run (all four modes, full-file hashing and I/O)
took 174.90 seconds wall time, 29.70 user / 25.16 system seconds, maximum RSS
264676 KiB under WSL. Host oracle scoring took 1.61 seconds internally;
comparison plus scoring took 1.81 seconds wall, maximum RSS 24168 KiB. These
figures do not measure P4 execution, stack placement or audio coexistence.
The full C/oracle and actual product-consumer matrices were not rerun here.

External artifacts are under `D:/AI/LIBAPTA/outputs/rust-s6-beat-events-20261022/`
and `/home/daniel/.local/share/libapta-audio/rust-rewrite/beat-events-20261022/`.
Private labels, audio, mappings and per-source traces remain outside Git.

- Dirty baseline inventory SHA-256:
  `9cfad36edd3dca96373cc9cfac874fdd16c38eaba5ace1f6ec64a1b9b8e6961c`.
- Frozen candidate source inventory SHA-256:
  `48d4957ad359a8d4b0ded739c16e4dad066ee992cc7b14fdcd68bbcb0ba0832f`.
- Native coverage report SHA-256:
  `0046ebe743082aac206b4e938b38fd207564f19d19cb328520277d141e9e5d8a`.
- Label-scored event-lattice report SHA-256:
  `79066822c3f4d760b7f1b8a7da0e79e0b96f098ef15b1ff47f671a9d0ba8f784`.
