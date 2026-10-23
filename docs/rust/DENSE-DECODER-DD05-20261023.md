# Dense decoder DD-05: registered development verdict

Date: 2026-10-23. **REJECTED. Integrity and resource checks PASS.**
The single registered attempt constructed, independently verified and scored
all **100 sources / 600 windows**, retaining **401 complete / 199 incomplete**
reference contexts. All candidates were frozen before reference-label loading.
No new audio read, neural inference, model change, parameter sweep or retry.

The [aggregate result](DENSE-DECODER-DD05-20261023.json) contains exact metrics,
all paired changes, genre aggregates, gates, input identities and resource records.
The [experiment](DENSE-DECODER-EXPERIMENT.md) and
[registered protocol](DENSE-DECODER-PROTOCOL.json) remain immutable.

## Decision and evidence level

Five of sixteen registered musical gates pass; **eleven fail**. The candidate
does not proceed to DD-06 transfer or a native Rust port. Preserve this failed
candidate and its evidence. No retuning on the observed labels, fallback,
smoothing rescue, segment-cap increase or automatic retry is performed.

This is already-open GTZAN development evidence. Recording/training independence
remains unresolved, confidence is uncalibrated, and native/P4/acceptance/product
qualification remains open. No formal holdout or fresh final corpus was opened.
Stable package version, C/default Rust behavior, consumer pin/provider and wire
capacities remain unchanged.

## Grid and sequence results

All counts retain the original 600-window denominator and reference support.
Nominal and actual rows have their separately registered scoring meanings.

| Measure | Legacy OLS | Candidate | Registered condition | Result |
|---|---:|---:|---|---|
| Projected nominal joint windows | 187 | 190 | >=188; zero OLS joint breaks | Count passes; 7 breaks fail |
| Nominal complete-sequence windows | 178 | 182 | >=179 | PASS |
| Actual-interval joint windows | 101 | 9 | Zero OLS actual joint breaks | FAIL: 95 breaks |
| Actual complete-sequence windows | 97 | 8 | >=98 | FAIL |

Paired changes, measured on the registered matched window comparisons:

| Comparison | Period fixes / breaks | Phase fixes / breaks | Joint fixes / breaks | Meter breaks | Downbeat breaks |
|---|---:|---:|---:|---:|---:|
| OLS nominal -> candidate nominal | 10 / 7 | 13 / 5 | 10 / 7 | 0 | 1 |
| OLS actual -> candidate actual | 3 / 97 | 13 / 11 | 3 / 95 | 0 | 1 |
| Coarse -> candidate nominal | 97 / 9 | 229 / 8 | 146 / 3 | 148 | 14 |

The coarse joint no-regression gate also fails (3 breaks, maximum 0).
Coarse meter/downbeat and actual downbeat counts are retained diagnostics;
the registered vetoes are not silently expanded or relaxed after measurement.
Neither of the two original coarse-to-OLS joint regressions becomes joint-correct
in candidate projected, nominal or actual scoring.

Actual period-correct windows fall from 103 to 9. Actual-scored point support is
5680 legacy versus 5977 candidate; recorded unknown-successor points are 88
versus 55. The candidate preserves actual cross-window successors and the final
unknown successor. These are the unchanged scorer's support counts, not a claim
that the two representations contain identical points or successor availability.
The report retains all support counters and incomplete contexts.

The small improvement after OLS projection does not establish a usable variable
grid. Actual intervals fail decisively. The aggregate evidence establishes this
failure; a causal attribution to quantization, local refinement or a particular
timing regime has not been independently established here.

## Event metrics

Values below are percentages for display. Gates use the exact fractions in the
JSON and the registered absolute epsilon **1e-12**, without rounded baselines.

| Macro metric | Legacy | Candidate | Change, percentage points | Gate |
|---|---:|---:|---:|---|
| Beat F1 | 89.827788 | 89.188395 | -0.639393 | FAIL |
| Downbeat F1 | 78.552393 | 77.926524 | -0.625869 | FAIL |
| Beat CMLt | 81.649079 | 82.399784 | +0.750705 | PASS |
| Downbeat CMLt | 67.438893 | 76.381945 | +8.943052 | PASS |

Genre means and all private per-source metric changes are retained. For example,
blues downbeat F1 falls from 70.542621% to 55.562771%; classical beat F1 falls
from 71.924634% to 65.186327%. The CMLt gains do not waive the F1 or paired grid
vetoes. The registered projected OLS meter no-break gate passes, without making
a general meter, confidence or genre qualification claim.

99 sources emit candidate events; one abstains with `nonpositive_path`.
Selected meter counts are 3 sources in 3/4 and 97 in 4/4, including the abstaining
source's diagnostic winner. No abstention or unknown result is counted as success.

## Execution, resources and identity

All three measured phases used clean, already committed/pushed source
**`8e27cf1495cc5d496a3c251b0e5c825c56dc81d6`**, without source/document edits.
Protocol SHA-256:
`5dc1241087a08663b06766f91faade5a5758f9791fd85e0077c62e78d5a186fd`.
Component-bound preflight SHA-256:
`b00c031c58c793bf46f7cc74bb09c9fd26bfa4edd49e4560f2d4ef43f4208e93`.

| Phase | Parent-supervised seconds | Maximum observed process RSS KiB | Outcome |
|---|---:|---:|---|
| Construct | 205.062 | 27452 | 100 frozen, PASS |
| Independent verify | 390.360 | 29976 | 100 replayed, PASS |
| Evaluate | 215.071 | 120096 | Integrity PASS, musical REJECTED |

Each phase is below 1800 seconds and each observed process below 262144 KiB.
The slowest source worker reports 1.569010 seconds against its 60-second cap.
Pure source workers use a hard 268435456-byte address-space ceiling; the
verification CLI also applies this ceiling to its scalar worker. Maximum live
score/parent payload is 3564000 bytes. Combined six phase/output directories
occupy **7678934 bytes**, below the 67108864-byte attempt cap.
RSS is Linux per-process monitoring at 20-ms intervals, plus reported final
self/waited-child high-water marks; descendants and supervisors are accounted
for. These are host decoder/evaluator costs, without new model inference or P4
measurements.

The actual three CLI commands are those in the DD-05 experiment and DD-04
handoff, executed once by the external driver
`D:\AI\LIBAPTA\outputs\dense-decoder-dd05-20261023\run.sh`.
Driver SHA-256:
`f4975314ad1f663b4334bf4a51c0e32fb1a8ae116006a1bd4f599c0d9e76207b`.
Its separate construct/verify/evaluate stdout/stderr logs and aggregate export
are retained alongside it. All source-level payloads, candidates, paths, labels,
scores, bindings and resource reports remain outside Git under the registered
`dense-decoder-v1` attempt directories. Destinations were unused and no output
was overwritten.

| Completion artifact | SHA-256 |
|---|---|
| Candidate freeze | `26c1d190a089421b2364c57ece5d2a3d36d55c18c187fbf300f7baf3fa0880f6` |
| Independent verification | `053e7247b5b4ca38723d5952f009953fc609fea838e836fa9dd8697d5725810f` |
| Private evaluation report | `60043349433671b42533fbb48d19c0a8d91b870de17a834bc3fa8a87b0d607ac` |

The aggregate JSON additionally binds all successful parent envelopes and the
original capture/legacy/reference hashes. The export independently checks
source/protocol identities, all completion hashes, resource limits, full counts,
candidate hashes and the exact gate recomputation before publication.
DD-04's 226 Python and 37 Rust tests per debug/release profile are prior evidence
for this unchanged implementation; this documentation-only checkpoint does not
claim another execution of those suites.

## Next step

DD-05 is complete with a frozen rejection. DD-06 through the product-directed
port remain closed for `dense-joint-renewal-v1`. The next work is a separately
scoped failure diagnosis and decision about a new hypothesis/protocol. Reuse of
these now-observed development labels must be disclosed. Preserve the existing
baseline and this failed attempt; do not start a new real run, model selection,
transfer, training or native implementation as an automatic continuation.
