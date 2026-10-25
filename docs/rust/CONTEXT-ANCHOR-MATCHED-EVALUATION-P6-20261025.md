# P6: fixed generated matched comparison

Date: 2026-10-25. Branch: `rust-rewrite`.
**Operational integrity PASS; PAIR_REJECTED_ON_GENERATED_DIAGNOSTICS.**
The single registered comparison completed on clean pushed source
`2a5fa3a129fb55b5058adc8753fec2223ae76d0f`. Local, origin and remote SHA matched
before dispatch. All100 sources /600 windows,400 complete /200 incomplete
reference contexts,1200 independently verified context projections and2400 cells
are retained. No subset, retry, solver invocation or candidate reconstruction ran.

The immutable [P6 protocol](CONTEXT-ANCHOR-MATCHED-EVALUATION-P6-PROTOCOL.md),
[generated-scope registration](CONTEXT-ANCHOR-MATCHED-EVALUATION-GENERATED-SCOPE.json)
and [P3 metric/decision contract](CONTEXT-ANCHOR-MATCHED-EVALUATION-PROTOCOL.md)
own the execution and verdict. The [aggregate receipt](CONTEXT-ANCHOR-MATCHED-EVALUATION-P6-20261025.json)
contains all four comparisons, six families and three strata at cohort and four
generated-family group levels, canonical subtotals, raw controls and exact hashes.
Source-level labels, coordinates, IDs and records remain outside Git.

## Result and denominators

The coupled candidate fails the registered zero-support-loss veto for meter and
downbeat. Meter support totals382 ->382, but357 windows are shared,25 lose
support and25 gain it. Those transitions do not cancel. Downbeat support falls
345 ->320:320 shared,25 losses and no gains. The meter losses occur in complete
reference contexts (277 ->252), while gains occur in incomplete contexts
(105 ->130); pooling would hide that distinction.

The primary context candidate-versus-original ledger is:

| Family / stratum | Both supported | Left only | Right only | Neither | Shared correct original -> coupled | Fixes | Breaks |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Grid phase / complete | 390 | 0 | 0 | 10 | 390 ->390 | 0 | 0 |
| Grid period / complete | 390 | 0 | 0 | 10 | 347 ->347 | 0 | 0 |
| Grid joint / complete | 390 | 0 | 0 | 10 | 347 ->347 | 0 | 0 |
| Complete sequence / complete | 390 | 0 | 0 | 10 | 333 ->339 | 6 | 0 |
| Meter / all | 357 | 25 | 25 | 193 | 357 ->357 | 0 | 0 |
| Downbeat / all | 320 | 25 | 0 | 255 | 320 ->320 | 0 | 0 |

Grid rows retain all400 complete contexts; meter/downbeat rows retain all600
windows. Shared correctness is conditional on the respective shared G/M/D domain.
There are no shared-domain correctness breaks, raw-actual breaks or event-metric
decreases. The two exact faults are `meter_support_loss` and
`downbeat_support_loss`; six complete-sequence fixes do not override them.

All-window support and complete-reference correctness across the four cells:

| Cell | G | M | D | Joint correct /400 | Complete sequence correct /400 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Original legacy | 551 | 348 | 311 | 318 | 305 |
| Coupled legacy | 578 | 375 | 313 | 346 | 339 |
| Original context | 585 | 382 | 345 | 347 | 333 |
| Coupled context | 585 | 382 | 320 | 347 | 339 |

Within each raw arm, context preserves every previously accepted owned grid/meter
tuple and loses no G/M/D support. It adds34 supported windows per domain to the
original arm and7 to the coupled arm. These are representation support gains,
not shared-domain correctness fixes. The cross-arm support veto remains failed.

Raw actual-interval controls are separate: complete-context joint correctness
improves291 ->400 and complete-sequence287 ->395, with zero raw phase/period/
joint/complete-sequence breaks. Raw event macro metrics are unchanged in both arms:
beat F1 `0.9999465240641712`, beat CMLt `0.9998936170212765`, downbeat F1/CMLt
`1.0`. Each arm has7794 raw points,7694 globally known successors and100 unknown
successors; the canonical in-reference unknown counter is88. Independently fitted
projected windows are never stitched to replace these raw controls.

## Integrity, isolation and resources

P6 reuses exactly the73 P5-qualified component bytes and the derived runtime
`19484314cdb245e1ca81197057a989afb7d2786df1f38f88f91e8d1679262e9c`.
The scope remains
`2e45d042ea7e8906fb8eb42c19623c491b913259c2be47f6d87240d5f65cefdf`;
the generated-scope proposal is
`734603a080dfd34b918ec6c2108bed1096eeeb9f22e82633b5811955482a2e0e`.
Exact interpreter/runtime/source bytes are checked in the read-only namespace
before/after phases. Parent snapshot bytes remain unchanged.

PROJECT and VERIFY complete before EVALUATE opens labels. Both raw arms'1200
projections independently replay. EVALUATE reproduces every frozen legacy source
diagnostic before matched scoring. All7560 source/group/cohort ledgers pass the
registered closed schema and an independent set-partition oracle; all1260 canonical
subtotals and complete/incomplete conservation checks pass. Four groups are
`boundary`, `constant`, `jitter` and `ramp`. All3716 input bindings rehash unchanged,
and all414 retained attempt files are unchanged across read-only closeout.

| Phase | Supervised wall seconds | Worker self RSS/HWM KiB | Labels loaded |
| --- | ---: | ---: | --- |
| PROJECT | 23.607 | 24060 | false |
| VERIFY | 24.829 | 24120 | false |
| EVALUATE | 65.689 | 192312 | true |

Each phase's parent supervisor observes162068KiB; worker peaks are reported
separately. Whole launcher execution takes271.834s, including boundary checks;
its outer supervisor observes18680KiB and does not replace inner worker peaks.
Attempt output is19758218bytes; external scripts/logs/receipts at publication add
39444bytes. All remain below the unchanged60s/source,1800s/phase,262144KiB/process
and67108864-byte attempt limits. The outer7200s bookkeeping bound does not alter
phase caps. These are generated host costs, not isolated DSP or physical P4 costs.

There are no fresh unit-suite runs because source code is unchanged from the
39-test P5 preflight. P6 adds actual full-cohort oracle replay, canonical evaluation
and the fresh independent schema/ledger/subtotal closeout checks above. The final
read-only closeout supervisor passes in45.099s with63084KiB observed RSS.

The first read-only checker failed because its cell loop reused the variable
holding its file-hash ledger. Its source/hash and failure record are retained.
Renaming those bookkeeping variables fixes the checker; the final audit passes.
No measured source, attempt file, projection, scorer or threshold changed, and no
comparison or scoring was repeated. This checker error is distinct from the valid
pair rejection.

Artifacts remain under
`D:\AI\LIBAPTA\outputs\context-anchor-matched-p6-20261025` and the registered WSL
attempt destination. The full matched report SHA-256 is
`53e7489a7121456950a65c8ef0dc4a7e274182d4dac2c8cb291c54d7bfd0dee2`.

## Next boundary

**P7: inspect the saved paired support transitions read-only to explain the25
complete-context losses and25 incomplete-context meter gains.** Use existing
records without reprojecting, rescoring or altering this rejected candidate. Then
review whether a separately registered new hypothesis is justified; do not relax
the zero-loss veto or promote the coupled candidate from pooled totals.

The old C3 scientific verdict remains frozen. Independence is UNRESOLVED,
confidence unqualified, historical real-runtime metadata NOT_AUDITED and real
authorization absent. This generated rejection does not open real/GTZAN scoring,
inference/training, transfer, native/product activation, consumer pin changes,
physical P4, fresh acceptance or release. VERSION remains1.0.1.
