# DD-05 tail audit: separate small timing regressions from large errors

Date: 2026-10-23. **Audit PASS; original candidate remains REJECTED.**
[Pre-inspection classification scope](DENSE-DECODER-TAIL-SCOPE.md).
[Exact aggregates](DENSE-DECODER-TAIL-20261023.json).

The same 100 frozen candidates and 600 saved actual scores replay exactly.
No new candidate is decoded, changed or scored; no audio, logits, new inference,
holdout, parameter sweep or threshold change is used. This is a post-hoc audit
of previously observed development labels, with original input/scorer hashes.

## The 95 regressions contain no large-error tail

| Original subset | Known intervals | Period errors >1% | Intervals >5% | Windows with >5% |
|---|---:|---:|---:|---:|
| All sources/windows | 5922 | 3577 | 1041 | Not counted as a separate all-window metric |
| 95 actual joint regression windows | 977 | 452 | **0** | **0/95** |
| 181 nominal-correct, actual-failed windows | 1960 | 942 | 14 | 9/181 |

All 95 regressions therefore contain known period errors in **(1%,5%]**.
Neither half/double-family ratios nor the global near-50%/near-100% tail explains
this regression set. Of the 181 nominal-correct/actual-failed windows, 172 have
only <=5% period errors. Their 14 large-tail intervals in the other nine windows
are all off-family under the predefined relative bands.

The two smaller groups overlap; their counts are not additive. These point
counts do not replace the 600-window denominator or the original musical gates.

## What the global large tail contains

Large tail means original absolute interval/local-period error >5%. Family
bands are inclusive +/-1% relative to the indicated interval/target ratio.

| Interval / local reference period | Tail intervals |
|---|---:|
| 1/3 | 44 |
| 1/2 | 135 |
| 2/3 | 1 |
| 3/2 | 16 |
| 2 | 125 |
| 3 | 0 |
| Off-family | 720 |
| Total | 1041 |

These are arithmetic ratio labels, not a musical tempo diagnosis. Of the 125
double-ratio intervals, 108 have both endpoints within the original 10%-period
phase tolerance of reference events. Across all families, 175 intervals have
two phase-supported endpoints and skip one or more annotated events. Conversely,
82 have two supported endpoints matching adjacent reference events: the local
median target can disagree with the adjacent annotated gap. One half-ratio
interval is such a phase-supported case; most half-ratio endpoints are not both
phase-supported. Eleven successors lie outside reference support and remain
explicitly separate.

999/1041 large errors also exceed 5% in the original row term. Fifty-six large
errors have a target period above 72000 frames, outside the original d10..75
model range; none has a target below 9600. The off-family tail is not completely
causally attributed by these categories. A pure timing regularizer cannot add
missing beat topology, repair unsupported reference targets, or establish the
correct musical tempo family automatically.

## Design decision

The proposed next research axis is **bounded joint timing of decoded neural
events**, with regularity applied to the final times. This targets the small
interval-error mechanism without inheriting the rejected row-lattice topology.
It is a new input/algorithm contract, not a retry or parameter change of the
DD-01 decoder. The old prohibition on legacy events remains in force for v1;
the new proposal explicitly declares raw neural decoded events as observations.

The [implementation plan](COUPLED-TIME-IMPLEMENTATION-PLAN-20261023.md) proposes
one source-wide convex timing objective and deterministic solver, while retaining
beat count and neural downbeat ordinals. It does not claim that this will fix
all existing errors, preserve every baseline score, or pass any musical gate.
Missing/extra beats and large-family mistakes remain an explicit limitation.
No solver or new candidate is implemented or evaluated in this checkpoint.

## Execution and retained evidence

Scope/tool/tests were committed, pushed and remote verified before the audit at
`e452a843cbaae43956a3366050e83d4f24e41687`. The tool reuses the first audit's
binding and scorer replay through a read-only observer; it does not call a
constructor. All 5922/3577/1041 point counts and 95/181 window groups reconcile
with the prior immutable audit before publication.

All **50 focused Python tests pass**: 10 tail, 12 diagnosis, 10 unchanged
event-representation and 18 unchanged piecewise, zero skipped. Generated cases
cover family/threshold boundaries, phase-supported skipped events, irregular
adjacent annotations, model period range, unknown successors, group overlap,
privacy and the 100-source observer path. No C/Rust/P4 matrix is claimed here.

Driver `D:\AI\LIBAPTA\outputs\dense-decoder-tail-20261023\run.py` invokes the
tail-audit CLI over the original candidate/evaluation/prepared directories and
creates new `tail-audit-attempt-1`/`tail-audit-attempt-1-supervision` artifacts.
Linux parent supervision PASS: **51.286 s**, maximum observed process RSS
**28040 KiB**, within 1800 seconds / 262144 KiB / 64 MiB. Per-source IDs,
window details, bindings and phase logs remain outside Git.

Tail aggregate SHA-256:
`a4cf765cf9cb4eeb2467a47fa43696df6e7b2a27c611827ee77db97e4b41623d`.
Parent completion SHA-256:
`0aa9cd5d3b214c4e079f66dba7603dac1c9580dda1209bf59a762ebfbf4bec66`.
Driver SHA-256:
`187e8f9e13386c80e51dd06ef444fdeb063cc33908b757d069ce7368f7d61529`.
Test log SHA-256:
`8714780feafe0d1db10f7f11286b8d2539411485e82e711f41662d7c1b1eea38`.
