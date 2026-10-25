# MC-03: fixed generated meter-context comparison

Date: 2026-10-25. Branch: `rust-rewrite`.
**Operational integrity PASS; PAIR_REJECTED_ON_GENERATED_DIAGNOSTICS.**
The single [registered comparison](METER-CONTEXT-MC03-PROTOCOL.md) completed on
clean pushed source `ccb872183f507e7c3734b359b507faf37f816178`, with verified
matching local/origin/remote SHA. All100 sources /600 windows,400 complete /200
incomplete references,1200 independent projection replays and2400 cells complete.
There is one comparison attempt and zero retries, candidate reconstructions,
solver calls or model inference/training runs.

[Aggregate evidence](METER-CONTEXT-MC03-20261025.json) retains all four comparisons,
six families and three strata at cohort/four generated-group levels, canonical
subtotals, raw controls, runtime/proof identities and read-only diagnosis.
Source IDs, labels, coordinates and source-level records remain external.

## Result

MC-01 eliminates meter support loss: both arms now support585/600 windows,
including390/400 complete and195/200 incomplete windows. The15 remaining grid/
meter-unsupported windows are shared. There are no one-sided meter losses/gains.
Each arm preserves382 P1-owned meters exactly and adds203 context meters.

Downbeat support is538 original versus513 coupled. All25 losses now occur in
reference-incomplete window0 of the boundary family. Complete-reference downbeat
support is390 in both arms; the earlier P6 complete-context losses are resolved.
The sole screen fault is `downbeat_support_loss`, so the new comparison remains
rejected despite broader support.

| Context candidate vs original / stratum | Shared support | Left only | Right only | Neither | Shared correct original -> coupled | Fixes | Breaks |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Grid phase / complete | 390 | 0 | 0 | 10 | 390 ->390 | 0 | 0 |
| Grid period / complete | 390 | 0 | 0 | 10 | 347 ->347 | 0 | 0 |
| Grid joint / complete | 390 | 0 | 0 | 10 | 347 ->347 | 0 | 0 |
| Complete sequence / complete | 390 | 0 | 0 | 10 | 333 ->339 | 6 | 0 |
| Meter / all | 585 | 0 | 0 | 15 | 585 ->585 | 0 | 0 |
| Downbeat / all | 513 | 25 | 0 | 62 | 513 ->513 | 0 | 0 |

Grid rows retain400 complete contexts; meter/downbeat rows retain all600 windows.
No shared-domain correctness break, raw-actual break or event-metric decrease
occurs. The six complete-sequence fixes do not override the support veto.

| Context support / stratum | P6 original -> coupled | MC-03 original -> coupled |
| --- | --- | --- |
| Meter / all | 382 ->382, with25 losses and25 gains | 585 ->585, zero losses/gains |
| Downbeat / all | 345 ->320 | 538 ->513 |
| Downbeat / complete | 277 ->252 | 390 ->390 |
| Downbeat / incomplete | 68 ->68 | 148 ->123 |

Legacy cells and every context grid counter are exactly unchanged from P6.
Raw event means, actual-interval counters and all successor masks are also
unchanged. Raw actual joint remains291 ->400 and complete-sequence287 ->395.
Event macro beat F1/CMLt remain0.9999465240641712/0.9998936170212765 in both arms;
downbeat F1/CMLt remain1.0. These generated values establish no real-music quality.
Old C3/P6 scientific verdicts are preserved without relabeling.

## Read-only diagnosis of the remaining veto

The saved records reproduce all1200 downbeat-support masks from the existing
definition: an emitted meter's published point must lie inside the inclusive
reference beat span. All25 one-sided losses have the same pattern:

- generated family `boundary`, window0, reference_complete=0;
- original meter origin CONTEXT, with its published point inside reference span;
- coupled meter origin P1, with its preserved published point BEFORE_REFERENCE;
- equal numerator and model-ordinal meter-phase class.

The matching phase class does not supply reference truth outside the annotation
span. Candidate support remains unavailable there; correctness is not imputed.
No new tolerance, alternate witness or narrower veto is applied after the result.
This diagnosis reads saved projections, scores and references; it invokes no
projector or scorer and changes no data.

MC-00 explicitly freezes each existing P1-owned meter tuple before fallback.
Therefore extending fallback context cannot change the coupled P1 witness in
these25 cases. The local two-bar bottleneck is addressed, but this preserved-
publication/reference-coverage boundary needs a contract decision before further
implementation. The MC-00/P3 rejection remains closed and authoritative.

## Integrity and resources

The [scope registration](METER-CONTEXT-GENERATED-SCOPE.json) SHA-256 is
`1116502f637d79d9181857081acb8659632123a8c09788403ad6022517513424`.
Qualified83 components/runtime/preflight remain byte-identical to MC-02.
Runtime SHA-256:
`bda8cd2d8254cf810d6d54caa3eba83ee3611839f0b18c541c025bbe3d15a22e`.
Fixed scope SHA-256:
`2e45d042ea7e8906fb8eb42c19623c491b913259c2be47f6d87240d5f65cefdf`.

PROJECT/VERIFY complete before label access. EVALUATE requires typed job/scope
equality and exact frozen legacy diagnostic parity. The independent closeout
validates7560 ledgers against the closed schema and separate oracle, checks1260
subtotals across105 tables, rehashes all3716 input bindings and preserves all415
attempt files identically. Actual profiles match the MC-02 source/parent closure
with zero unbound files; parent inventory verifies after execution. There is no
fresh unit-suite rerun because all83 qualified components are unchanged;
the33 MC-02 tests are inherited evidence, while the full-cohort checks are fresh.

| Phase | Supervised seconds | Worker self RSS/HWM KiB | Parent-observed KiB |
| --- | ---: | ---: | ---: |
| PROJECT | 42.779 | 22888 | 162160 |
| VERIFY | 45.367 | 22920 | 162160 |
| EVALUATE | 87.825 | 192360 | 162160 |

Whole execution including boundary work takes335.760 supervised seconds; its
outer supervisor observes19000KiB and does not replace worker measurements.
Attempt output is20458830bytes; external launcher/log/receipt artifacts at
publication add38096bytes. All fit unchanged60s/source,1800s/phase,
262144KiB/process and67108864-byte limits. The outer7200s bookkeeping ceiling
raises no source/phase limit. These times include provenance/namespace/scoring
overhead and are not isolated DSP or physical P4 measurements.

Read-only closeout passes in83.685 supervised seconds with201180KiB observed
RSS and zero constructor/scoring calls. A pre-registration auxiliary JSON write
initially used a byte-count helper rather than a serializer; it failed before
opening the file and was corrected before registration/dispatch. Its note is
retained. No scientific code, runtime, threshold or measured attempt changed.

The full report SHA-256 is
`8a91e9612322716909e314f439010d40769b5c17fd44f2ec11fc6724625c4841`.
Artifacts remain under `D:\AI\LIBAPTA\outputs\meter-context-mc03-20261025` and
the registered WSL attempt destination.

## Next boundary

MC-04: review publication-witness semantics, meter-phase equivalence and partial
reference coverage read-only, using the fixed evidence above. Decide explicitly
whether a separate representation/publication hypothesis is justified before
any code or measurement. Further fallback expansion cannot alter the retained
candidate P1 marker. Do not pick a witness using labels, silently narrow the
support veto, reopen this attempt or promote from pooled totals.

Real-runtime H0 remains NOT_AUDITED, independence UNRESOLVED, confidence and
whole-pipeline product budgets unqualified. No real/GTZAN scoring, independent
transfer, native/provider/pin activation, physical P4, fresh acceptance, release,
full C/Rust matrix or VERSION1.0.1 change ran at MC-03.
