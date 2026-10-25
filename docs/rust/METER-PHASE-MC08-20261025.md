# MC-08: one saved-generated phase-evidence interpretation audit

Date: 2026-10-25. **SAVED_GENERATED_INTERPRETATION_COMPLETE**, with independent
count/parity closeout PASS. This is interpretation of exposed synthetic development
evidence, not an improved detector, new musical acceptance or promotion.
[Aggregate receipt and bindings](METER-PHASE-MC08-20261025.json).

Clean measurement source `115ff67265973f1299e913bc93910ff8d7f7fc9f` on `rust-rewrite`
matched local/origin/remote before launch. The unchanged
[MC-06 scope](METER-PHASE-MC06-AUDIT-PROTOCOL.md) and
[MC-07 readiness](METER-PHASE-MC07-PREFLIGHT.json) bind the exact adapter, tests,
source and loaded runtime. Launch SHA-256:
`d5c01fbe44dfdfe700a40b4ec0490346f739c6990ba95000352205d90bd49d17`.
The fixed runtime remains
`3fb1343c8fb181684303d9e2e7af7320e393250591613009dcf519df003073e4`.

Exactly one audit processes all 100 sources / 600 windows: 400 reference-complete,
200 incomplete, four groups and 1200 saved projections. There is no retry,
candidate reconstruction, projector/model call or new grid/event/legacy scoring.
EXTRACT freezes all K before labels/correctness; INTERPRET evaluates the new
phase evidence once with unchanged MC-05/canonical arithmetic and reproduces
original published M/D. No old artifact or rejected verdict is overwritten.

## Measured result

M means an emitted owned meter, D support for its particular published point,
and E support for at least one owned phase point within the reference beat span.
E correctness requires **all** supported phase points, independently of meter
correctness. Missing E remains UNKNOWN/null, not a correctness result.

| Count, all 600 windows | Original | Coupled |
| --- | ---: | ---: |
| M supported windows | 585 | 585 |
| D supported windows, unchanged | 538 | 513 |
| E supported windows | 583 | 583 |
| E all-supported-points-correct windows | 583 | 583 |
| E unsupported windows | 17 | 17 |
| K phase points | 1941 | 1940 |
| Reference-supported E points | 1891 | 1865 |
| Correct E points | 1891 | 1865 |

The E window masks coincide: 583 both-supported, 17 neither, zero one-sided
support, fixes or breaks. Conditional phase correctness is 583/583 per arm;
this is not 600/600 correctness. Of the 17 unknown windows, 15 have no M and
two have M but no supported phase point. All are retained in denominators.
Complete windows have 390 E-supported / 10 unknown; incomplete have 193 / 7.

The pre-existing 25 D-loss windows remain in the boundary/incomplete stratum.
Their transition is exactly `D10_E11_C11`: original publication supported,
coupled publication unsupported, but E supported and all its supported points
correct in both arms. This describes the coverage sensitivity of the old
published-point channel. It does not erase those D losses or relax the frozen
support veto. **MC-03 remains PAIR_REJECTED_ON_GENERATED_DIAGNOSTICS.**

Point denominators differ (1891 vs 1865); they are not paired by ordinal or
combined into a shared-point accuracy. There are 293 pairs with identical recorded
model/point geometry and 307 different-or-model-absent pairs. K equality is checked
only on identical geometry. Equal E coverage/correctness does not establish exact
physical grid equivalence, interval accuracy or full-source continuity.

## Independent verification and resources

Read-only closeout verifies all 1200 original cells, 3881 saved phase-point counter
rows, 630 canonical M/D ledgers against the old report and independent ledger
oracle, 315 E ledgers by independent finite sets, 630 per-arm subtotals and 315
transition tables. Source/group/cohort and complete/incomplete sums conserve.
All 415 old attempt files and 3716 C3 bindings rehash unchanged, as do the 210
new attempt files before/after closeout. Full parent runtime verification and
clean measurement-source checks pass again. Closeout calls no projector/scorer.

EXTRACT/INTERPRET wall time is 51.065 / 62.185 s; worker self RSS/HWM is
77392 / 77536 KiB. Their supervisor observes at most 175664 KiB, including its
own process; it is not a complete census of bwrap worker sessions. Both workers
retain the hard 268435456-byte AS limit and 60 s/source deadlines.
The complete API execution takes 454.799 s, self peak 192836 KiB; outer supervision
494.620 s. Independent closeout takes 105.928 s, self peak 198772 KiB; its outer
supervision takes 123.968 s. These include binding, namespace, metadata and file
I/O; they are not DSP throughput or P4 measurements. No cap is raised.

Concrete launch is the bound external `launch.py` invoking `execute.py` with the
launch hash, using the retained interpreter, `-B -X pycache_prefix=/libapta-no-cache`,
the existing namespace and `io.supervise`. It calls `prepare_saved` then `run_saved`
once. The bound `closeout.py` independently counts saved outputs. Scripts,
launch/runtime/input/output identities, logs and resource receipts stay external
under `D:\AI\LIBAPTA\outputs\meter-phase-mc08-20261025`; case data stays in the
registered WSL destination. Publish only aggregates, never per-source rows/IDs.
The unchanged 67108864-byte cap includes the attempt and MC-06/07/08 external
preparation/preflight/launcher/log/receipt trees; old input/runtime images remain
dependencies. Exact byte accounting is in the receipt.

## Decision and next step

Close this bounded meter-support investigation. Preserve MC-00 through MC-08,
the original publication, P3 rules and every old rejection. The diagnostic shows
equal supported generated phase evidence; it supplies no detector improvement
and no basis for moving a marker or reclassifying an old result as a pass.
Any new primary metric/publication experiment still needs its own hypothesis,
contract and untouched evidence; E is not a new acceptance threshold.

Next: H0, following the historical-real-runtime section of the
[existing repair plan](COUPLED-TIME-R5-REPAIR-PLAN-20261024.md). First freeze a
metadata-only scope, identities, resource limits and stop conditions for the
retained real-runtime closure. Then compare actual dependency bytes and the
known system-library delta, without opening audio, labels or logits and without
inference/scoring. Do not rewrite that freeze or assume this generated runtime
qualifies it. This checkpoint neither registers H0 nor authorizes a real replay.

Real-runtime H0 remains NOT_AUDITED, independence UNRESOLVED, confidence unqualified
and whole-pipeline product budgets UNKNOWN. No native/consumer activation, C/Rust
build, physical P4, fresh acceptance or release occurs. VERSION remains 1.0.1.
