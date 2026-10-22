# Segmented reference-path feasibility — measured result

Date: 2026-10-22. Exact clean implementation:
`42c9827d91c1f86203193d4d8b3acef0327a359d`, committed and pushed before measuring.
The [experiment](SEGMENTED-REFERENCE-FEASIBILITY.md),
[protocol](SEGMENTED-REFERENCE-FEASIBILITY-PROTOCOL.json) and
[preflight](SEGMENTED-REFERENCE-PREFLIGHT.md) remain unchanged. The
[machine-readable aggregate](SEGMENTED-REFERENCE-RESULT-20261022.json) contains
all source/window outcomes, approximation, matched-source comparisons and hashes.

## Decision

**The existing native/wire representation works for 47/100 complete reference
paths in the registered anchored family; all fit within seven segments.**
Every one of these 47 graphs passes actual native validation, strict/canonical
GGRD roundtrip and external-builder validation with same-grid meter included.
All 135 supported MTRD records retain their ordinal and whole-frame binding.
No observed graph is rejected by the eight-segment wire limit.

The other 53 sources have **no complete path in this specific anchored family**,
even before the eight-segment cap is applied. This does not prove the wire format
or every segmented fit cannot represent them. Each piece starts at an exact
reference timestamp, and source/window/ownership, guarded period/phase, natural
point counts and actual junction constraints all remain mandatory. No failed
source is truncated, partially scored as a complete path or used to retune them.

**Next: separately register a reference timing reachability diagnostic with
free phase inside the same bounds, before changing native limits or training
another model.** Preserve this exact anchored family. Test a continuous
per-event position/interval relaxation first: an empty reachable set would
identify incompatible timing/reference constraints, while a nonempty set only
justifies later quantized segmented construction. It would not establish an
eight-segment native solution or detector accuracy. No such alternative fit is
implemented or selected by this checkpoint.

## Fixed evidence and structural scope

All 100 existing GTZAN sources, 600 S6 windows, 401 complete and 199 incomplete
reference contexts remain. Only saved labels, learned events and prior audit/
coarse/OLS records are read. No PCM, inference, labels, formal holdout or reserved
transfer is opened. Old coarse/OLS tuples and the reference-only constant witness
reproduce exactly, including 187/401 model and 227/401 constant-reference joint
windows. Both original regressions remain unchanged.

This study uses reference labels to constrain each piece. It is representation
feasibility, not a new detector or reference-to-production importer. Dynamic
programming minimizes count only within the fixed first-reference-anchored
family, with longest-first ties. Native integer tempos and Q32 periods must
satisfy guarded constraints and the actual outgoing interval at each junction.
Declared counts cannot hide an additional natural line point within a segment.

Coverage is `[first reference beat,last reference beat+1)`, not assumed whole
audio. Every original ordinal and S6 membership is retained. Known PCM gaps
are explicitly refused by this family in synthetic tests, rather than bridged;
the real label cohort provides no declared unavailable PCM inside its envelope.
All states remain provisional, REVN pending, confidence zero and uncalibrated.
Selected TEMP is the first actual segment's nominal tempo under unchanged
dynamic-grid semantics.

## Native and capacity outcomes

| Complete-source outcome | Sources |
|---|---:|
| Full anchored-family witness | 47 |
| Actual complete native graph accepted | 47 |
| Strict/canonical grid payload roundtrip | 47 |
| External builder validation with meter | 47 |
| Over-eight-segment witness rejection | 0 |
| No complete anchored-family path | 53 |

| Minimum pieces within the registered family | Sources |
|---|---:|
| 1 | 25 |
| 2 | 12 |
| 3 | 2 |
| 4 | 4 |
| 5 | 2 |
| 6 | 1 |
| 7 | 1 |

The 47 successful paths use 94 segments in total; min/median/max pieces per
supported source are **1/1/7**. This histogram is conditional on a complete
family witness, not all 100 sources. A synthetic nine-piece graph independently
passes native validation and fails wire/builder capacity without truncation;
no real source exercises that rejection in this run. The 53 unsupported-family
sources have no graph constructed, so they are not claimed native-validator
failures or wire-cap failures.

Native input includes actual grid/TEMP/MTRD relationships. The external builder
fixture additionally supplies one synthetic overview column for required
structural fields; this is not waveform analysis. Checks cover payload roundtrip
and complete structural validation, not an owned FINAL result, full container
export or trusted product-owner handoff. The production M1 converter, dependency
pin, providers and limits remain unchanged.

## Window diagnostics and like-for-like comparison

All 600 windows are retained. Native/wire actual-successor diagnostics have
188/401 joint and complete-sequence successes. The 53 sources without a complete
family path contribute empty unsupported witness results, never a successful
partial prefix. This source-level all-or-nothing family test has a different
support policy from the original independent per-window constant fit.

Use the matched 47-source subset to assess representation behavior:

| Same 47 sources / 282 windows / 188 complete contexts | Joint-correct windows |
|---|---:|
| Saved learned-event OLS control | 138/188 |
| Reference-only constant OLS witness | 161/188 |
| Segmented reference witness, own nominal periods | 188/188 |
| Segmented reference witness, actual successors/junctions | 188/188 |

These are label-derived witnesses; 188/188 is not a musical detection result or
an independently accepted corpus. Among all 106 previously constant-period-
incompatible windows, only four obtain a complete-source segmented witness
under this family; the other 102 remain unsupported by it. The capacity limit
does not explain their status. Final source successors stay unknown; across
all output windows 38 reference-scorable points have no successor and nine
fractional end points lie beyond the last annotation coordinate. No incomplete
context is inferred correct or dropped.

## Approximation and meter

Python and Rust expansions agree exactly on **2795 Q32 points and original
ordinals**. This is lossless agreement with the computed witness, not exact
preservation of all original reference coordinates: 2701 points move. Absolute
reference displacement has median **391.27 frames (8.15 ms)** and maximum
**3949.21 frames (82.28 ms)**. Maximum normalized phase error is
**0.0999513 beat**, inside the fixed 0.10-beat tolerance with the registered
one-frame guard. No 70-ms event-F1 acceptance is inferred from that different
phase contract.

The unchanged two-complete-equal-bars rule supports 135 meter records on these
47 paths. All map their original reference downbeat ordinal onto the segmented
Q32 position, preserve its whole-frame component through MTRD serialization,
and pass same-grid lookup plus complete native/builder cross-feature validation.
Their ranges are clipped to the reference envelope; unsupported prefix/suffix
or meter context is not filled. These counts are binding evidence, not fresh
meter/downbeat accuracy or calibrated confidence qualification.

## Verification and reproduction

All **76 Python tests**, **33 Rust tests per debug/release profile**, three
generated Python-to-Rust/MTRD fixtures, Rustfmt, focused warnings-as-errors
Clippy and no-default-features checks pass. Rust core, native validators/limits,
prior candidate/scoring tools and production consumer sources are unchanged.
Full Rust/C/sanitizer, hosted CI, actual product-owner and physical P4 matrices
are not rerun or claimed for this host-only diagnostic.

The run binds 1337 files including source, old reports/events/labels, per-source
audit records and the release native binary. Every input and detail hash is
rechecked. A separate replay reconstructs all 100 plans, independently computes
minimum counts by forward shortest paths versus the original reverse dynamic
program, re-executes all 47 actual Rust graphs, and reproduces all 600 old/new
scores, meter relationships, capacity outcomes, approximation and matched-source
aggregates. The 53 no-path outcomes reproduce without a fallback or parameter
change.

Measured processing/scoring and final recheck take **24.26 s**, with peak RSS
**27452 KiB**, inside the fixed 180 s / 256 MiB host caps. Initial source/input
preflight, compilation and independent replay/export are outside that timing.
No physical P4 or real-time capacity claim follows.

From the exact clean source in WSL, with retained private inputs and a new
output path (overwrite is refused):

```sh
python3 tools/apta_1_1_segmented_reference.py \
  --attempt "$PRIVATE_ROOT/neural-reference-20261022/comparison-source-domain" \
  --prepared "$PRIVATE_ROOT/neural-reference-20261022/prepared-availability" \
  --prior-audit "$PRIVATE_ROOT/timing-failure-audit-20261022/attempt" \
  --analyzer "$NATIVE_SEGMENT_PROBE" --output "$NEW_PRIVATE_OUTPUT"
```

Private report and records:
`/home/daniel/.local/share/libapta-audio/rust-rewrite/segmented-reference-20261022/attempt/`.
Exact source archive, verification log, synthetic bridge and independent replay/
export script: `D:/AI/LIBAPTA/outputs/rust-segmented-reference-20261022/`.

- Protocol SHA-256:
  `483fca380e70810d793ca02c739a342f3c720ad370823e28a53f557b40e991c1`.
- Private report SHA-256:
  `0a0f152ae3f504188eace23e8a806b91bf87d33b7e0423da1f7bdc1d172e71ae`.
- Native probe SHA-256:
  `0e3404e22be6e0748031e626be094803ece73f5f6a3e6fd5dc74ee2fad85f3ed`.
- Exact source archive SHA-256:
  `c64556835a2df15cee415af306d325296d2b20ecdec25ca0799cbbb9728a4409`.

This already-open cohort remains development-only with unresolved cross-dataset
identity. Reference feasibility does not close learned event/family selection,
independent transfer, confidence, fresh product acceptance or physical P4 gates.
Commit and push this fixed-family result before registering free-phase
reachability; the current family and product defaults remain unchanged.
