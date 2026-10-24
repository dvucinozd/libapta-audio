# P4 pure matched ledger and meter adapter

Date: 2026-10-24. **PUBLIC GENERATED IMPLEMENTATION PASS; no campaign enabled.**
Baseline: `724db83c8d26c1799c3ec38321c33cae0047c1a1`, `rust-rewrite`.
Authority: unchanged [P3 registration](CONTEXT-ANCHOR-MATCHED-EVALUATION-PROTOCOL.md)
and [machine contract](CONTEXT-ANCHOR-MATCHED-EVALUATION-PROTOCOL.json), SHA-256
`c8b17d5bda059ec96260180485f18ce59712db47c9d4037c9180e1917480c6e8`.
The [P4 receipt](CONTEXT-ANCHOR-MATCHED-EVALUATION-P4-20261024.json) binds the three
new components and external final logs. P3's implementation-disabled fields remain
the historical registration snapshot; this receipt owns the P4 implementation state.

## Implemented API and boundaries

`tools/apta_context_matched.py` is an import-free, pure evaluation helper, with
no CLI, file access, environment selection, projection, callback or solver call:

- `meter_evidence(meter, points, first_frame, end_frame, truth, representation)`
  validates the entire normalized input before abstention. Meter is None or a
  closed record containing numerator, denominator, downbeat_frame,
  downbeat_fraction_q32 and downbeat_ordinal. Points contain exact position_q32
  and ordinal, in strictly increasing order. A published meter must identify
  that exact owned point, including fraction and ordinal. A legacy meter outside
  the window is an explicit conflict with zero matched meter scores; a context
  meter outside the window or any in-window binding mismatch is an error.
- `make_cell(grid_score, evidence)` accepts the complete canonical nominal
  score_points counter record and checked meter evidence. It checks point counts,
  reference-context agreement, counter consistency and G/M/D nesting. It does
  not rerun canonical grid scoring. Nominal periods are known; a nonzero scored
  unknown-successor counter is rejected here. Raw actual scores/masks stay on
  their separate registered path.
- `make_ledger(left_cells, right_cells, comparison, family, stratum, level)`
  validates all cells and pair geometry/context/representation before filtering.
  It partitions support, counts shared correctness separately from one-sided
  successes, counts conflict windows once and emits exact conditional fractions
  or null for an empty shared domain. Source level is capped at8 cells, group/
  cohort at600. Combine raw cells to aggregate counts; never average group rates.

Normalized truth is a closed reference beat/downbeat/meter record. At least two
ordered beats, one ordered downbeat, downbeat membership and known meter are
required even when meter is absent. Frame/point/event capacities remain bounded.
The reference arithmetic helper allows the registered maximum frame endpoint;
the future runner must still validate references against the actual source
domain. These APIs validate normalized facts, not source identity, raw event
topology, an OLS fit, complete waveform coverage or the canonical grid score's
mathematical origin. The future runner must bind source/order/geometry, verify
P1 projections independently and call the registered scorer before normalization.
None of these evaluation helpers belongs in PROJECT or candidate construction.

The meter adapter retains canonical binary64 evaluation operations: exact Q32
whole-plus-fraction conversion, stable nearest-downbeat ties, the median of up
to five intervals selected by proximity of their start to that downbeat, and
the unchanged0.10 tolerance. It returns only the four registered meter fields.
It never walks an unowned grid to obtain meter scores. The independent test
oracle instead uses exact rational coordinates/distances, explicit source-index
tie ordering, statistics.median and the exact value of the canonical binary64
tolerance. Its ledger uses finite set partitions rather than production counter
updates. It imports no production or registration helpers.

## Executed verification

WSL Ubuntu, Python3.12.3, jsonschema4.10.3:

```text
/usr/bin/python3 tests/unit/apta_1_1_context_matched.py -v
/usr/bin/python3 tests/unit/apta_1_1_context_matched_registration.py -v
```

**15 new +10 unchanged registration tests pass**, zero failures/errors/skips.
Exact value/type comparisons and the unchanged closed P3 schema establish:

| Check | Final count |
|---|---:|
| Meter evidence versus independent rational oracle | 824 |
| Normalized cells versus independent oracle | 810 |
| Paired ledgers versus independent set oracle/schema/conservation checks | 3498 |
| Meter-only output versus unchanged canonical meter fields | 816 |
| Public projection input pairs | 405 |
| Within-arm support preservation / shared-correctness parity pairs | 405 |
| Support/correctness/context truth states over all comparisons and strata | 88 |

The 405 pairs reuse only P1's public arithmetic generator, not saved source cases.
Both legacy/context projectors and canonical scoring helpers run only on those
generated integers. Canonical meter comparisons consume only the four meter
fields; their old grid fields never become context grid diagnostics.

Controls cover absent/unsupported meter, owned fraction/ordinal binding,
legacy-unowned conflicts, incomplete window context with a supported downbeat,
published meter outside reference span, exclusive ownership versus inclusive
reference endpoints, exact Q32 tolerance edges, even medians with half frames,
stable interval-cutoff ties, all four comparisons/six families/three strata,
all three aggregation levels, empty rates, unequal-size group pooling, maximum
3072-event/point inputs, invalid types/containers, validation before abstention/
filtering, capacity errors, and no input/output aliasing. A public600-cell
400/200 counter construction checks conservation only; it is not C3 case scoring.
An unowned legacy-meter example is a normalized conflict control, not a claim
that a new real OLS conflict was observed. Incorrect cells/records are rejected.
Fresh-interpreter checks block imports and I/O; the production module has no
imports at all. This is a tested function boundary, not an OS sandbox claim.

Final supervised P4 wall time is **2.131179s**, maximum observed process RSS/HWM
**34728 KiB**. Largest measured public matrix pair including checks is
**0.004118s**. P3 controls take0.845531s /29292 KiB. Each whole suite is supervised
at60s, stricter than the1800s phase cap, with262144 KiB/process and67108864
external bytes. Actual evidence bytes/hashes are in the receipt, under
`D:/AI/LIBAPTA/outputs/context-anchor-matched-p4-20261024/checked/`.
Earlier passing generated runs remain preserved; the final run also explicitly
requires no within-arm G/M/D support loss and unchanged shared correctness, and
checks that a forged legacy conflict without a grid cannot pass normalization.

P3's23 bindings, all14 P0 document bindings,17 reviewed sources,37 frozen C3
components and all four P2 probe components remain unchanged. P1 code, canonical
scorers, old results, protocols and preflights are preserved byte-for-byte. Tests
ran on the named baseline plus SHA-bound new P4 files; this is not a clean-source
corpus campaign. No C/Rust/P4 matrix or physical measurement is rerun here.

## Next boundary

**P5: prepare the generated-only PROJECT -> VERIFY -> EVALUATE runner and
component-bound operational preflight.** Bind the fixed C3 leaf inputs and a
separately qualified derived generated runtime; enforce full cohort/order,
phase isolation, complete projection freeze before labels, independent replay,
strict records, resources and stop rules. P4 implements no runner, full evaluator,
pair-screen verdict, preflight, dispatch token or saved-generated campaign.

The eventual P6 comparison is still disabled. No saved case/audio/label, neural
inference/training, real authorization, consumer/provider/pin, native producer,
acceptance or release changes. Scientific C3 remains REJECTED14/16, real-runtime
metadata NOT_AUDITED, independence UNRESOLVED and confidence unqualified.
Stable VERSION is1.0.1. Host arithmetic does not qualify physical ESP32-P4.
