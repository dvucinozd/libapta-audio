# MC-04: publication witness, phase and partial references

Date: 2026-10-25. Review baseline
`efcbc8318b16553856aabdb8485cb799f73cc0b6`, clean pushed `rust-rewrite`.
**CONTRACT_REVIEW_COMPLETE. MC-03 remains rejected.**
[Machine decision, source bindings and public proof examples](METER-CONTEXT-MC04-CONTRACT-REVIEW-20261025.json).

Decision: stop expanding the MC-00 fallback as a response to the remaining veto.
Select a separately registered public phase-equivalence evidence diagnostic as
the next measurement/interpretation hypothesis. Preserve publication, the original
P3 channel, its support veto and every closed verdict. This review registers no
new experiment, implements no algorithm and authorizes no cohort/real scoring.

## Contract findings

| Concept | Actual meaning and authority |
| --- | --- |
| Published MTRD witness | An exact owned grid point identified by whole frame and signed ordinal; header equals first segment. [MTRD v1](../../specification/APTA-1.1-DJ-SECTIONS.md#4-mtrd-version-1), C/Rust validators. No rule requires the earliest observed witness. |
| Encoded Q32 point | Fraction remains in the grid, not MTRD. Whole-frame/ordinal matching identifies the point; geometry and equivalence reasoning must retain its full grid position. |
| Ordinal | A continuity coordinate, not an implicit downbeat. Ordinal zero is arbitrary. [Beatgrid contract](../../specification/beatgrid.md#3-beat-ordinal). |
| Meter phase on one fixed grid | For beat ordinal b and meter witness d, the inspected consumer emits `(b-d).rem_euclid(m)`. |
| Published-point reference support D | Frozen P3 supports downbeat scoring only when the particular published point lies inside the inclusive reference beat span. It is distinct from complete-window context. |
| Existing P1 tuple retention | MC-00's experiment invariant. It is stricter than selecting any phase-equivalent valid witness and cannot be silently removed. |

Ruzina inspection uses fetched `origin/main`
`d000572c15b370702345f36d4dcc11a410b2f1de`, its adapter/neutral-view source and
README/Cargo manifest. Its libapta pin remains
`25d09de364e9507532c0508c838a8fecf0772d13`; pinned grid/MTRD sources were also
inspected. Local consumer checkout and existing untracked work are preserved.

The adapter requires one final full-source quarter-note meter, a continuous final
grid, consecutive ordinals, strict converted times and a matching witness. It
converts time/tempo independently of the meter witness and phase by modulo m.
It rejects unsupported sparse/hybrid/changing-meter data. Independent window
models do not automatically meet this whole-source contract. No consumer build,
pin/provider change, target test or physical qualification is performed here.

## Exact equivalence and its limits

For one identical grid, numerator m, denominator, revision and applicability,
replace witness d with another owned bound witness `d'=d+k*m`. Then:

```text
(b-d') mod m = (b-d-k*m) mod m = (b-d) mod m
```

Both points must actually exist on that same grid and satisfy all MTRD range,
ordering/header/binding requirements. The inspected consumer's neutral beat
time/tempo/phase content is therefore invariant under those assumptions.
Native/wire bytes and witness identity differ; this permits no mutation of an
already published immutable generation or claim of compatibility for every
possible client. Same ordinal residue across different grids is insufficient:
equal ordinals with anchors0 and6000frames can describe different physical phase.

Public counterexample: use a fixed120-BPM grid with points every24000frames,
window `[0,240000)`, meter4/4 and reference span `[48000,216000]`.

| Valid witness ordinal | Position / frames | Neutral phase pattern | Published point in reference span |
| --- | ---: | --- | --- |
| 0 | 0 | 0,1,2,3,... | no |
| 4 | 96000 | 0,1,2,3,... | yes |

The phase pattern is identical but D differs. The evidence JSON supplies the
analogous3/4 example and a fractional Q32 carry example. A one-frame prediction
change47999 ->48000 at a reference start48000 also changes D discontinuously.
These examples demonstrate that phase equivalence does not imply equal point-
coverage masks; no witness selector can infer unknown annotation boundaries.
They do not invalidate the registered empirical veto or score unknown points.

The review checks990 public modular identities, including signed algebraic
ordinals, and three fixed-grid geometry/support counterexamples. These are
source-derived arithmetic checks, not Rust/native execution, full wire acceptance,
new detector tests or an expansion of MC-00's nonnegative prototype domain.

## Consequence for MC-03

The bound saved diagnosis retains25 losses in reference-incomplete boundary/
window0. Original uses CONTEXT and its point is in reference span; coupled uses
P1 and its point is before that span. Integer phase classes agree, but that alone
establishes neither cross-grid physical equivalence nor correctness outside the
annotation interval. Unsupported remains UNKNOWN.

MC-00 returns every existing P1-owned meter before fallback. All25 coupled
witnesses are on that immutable path. Increasing neighboring context cannot
alter them. Moving a marker to a label-supported point would violate the current
contract; limiting the veto to complete windows after this result would amend
P3. Both are excluded. A new central/later witness convention can be separately
researched if a product requirement warrants it, but it is not selected as a cure
for this coverage-dependent veto.

## Selected next public specification: MC-05

Separate three domains explicitly: M (meter emitted), D (published witness
reference support), and E (reference-supported owned phase-point evidence).
For a fixed independently verified grid and meter, define the reference-free set:

```text
K = {owned grid point p : (p.ordinal - meter.downbeat_ordinal) mod m == 0}
E = K intersect inclusive reference beat span     # evaluation only
```

K is invariant to a phase-equivalent witness on that same grid. Publication and
the original D/scores stay byte-identical. E is a separate diagnostic domain,
not an alternate P3 pass or evidence that the detector improved. Across different
grids, positions and reference coverage still need actual evaluation.

The next implementing checkpoint must first register a closed public contract:

1. Freeze fixed-grid identity, exact Q32 inputs and new record/schema fields.
   Retain MC-00's3/4 or4/4, denominator4,3072-point/8-window/1440000-frame/48000Hz
   scope and original caps. Publish point/window/source/group/cohort denominators
   and complete/incomplete strata separately; never overwrite M/D counters or
   add per-point counts to window counts. Keep gap-free provenance and window
   boundaries; do not stitch independent grids or fill missing beats.
2. Implement pure owned phase-point extraction without references or I/O, plus an
   evaluation-only companion. Use unchanged canonical per-point meter/downbeat
   arithmetic for **every** eligible point. Report all counts; an all-correct
   flag requires all; window support is nonempty E, with separate point counts.
   No best-point selection, new tolerances or label-driven
   publication. Empty E is unsupported/UNKNOWN with null conditional rates.
3. Independently verify public equivalent witnesses, Q32 carries, wrong meter/
   phase, cross-grid non-equivalence, no/partial/full reference coverage, invalid
   inputs and input/publication invariance. Keep meter correctness distinct from
   phase correctness. Signed algebra does not authorize widening native inputs.
4. Review the public diagnostic before any separate saved-projection interpretation
   audit. Reuse completed proof chains and canonical routines; no new scoring
   stack or projector/worker framework is needed for the public prototype.
   Any later private audit needs its own source/runtime/input/output registration.
   It cannot relabel MC-03 or grant musical/product acceptance.

This selects a measurement-semantics prototype, not another candidate campaign.
There is no new acceptance threshold or automatic transfer/native progression.

## Evidence and remaining gates

The review binds16 repository sources, four consumer blobs and two pinned-library
blobs, verifies all83 qualified component hashes and rehashes all415 MC-03 attempt
files unchanged. The bound MC-03 diagnosis is reused without opening labels or
projecting/scoring again. Review supervision passes in20.303s with22388KiB
observed process RSS. No fresh unit suite, C/Rust/consumer build, model, corpus or
device run occurs. External audit/proof evidence remains under
`D:\AI\LIBAPTA\outputs\meter-context-mc04-20261025`.

MC-03 stays rejected. Real-runtime H0 is NOT_AUDITED; independence UNRESOLVED,
confidence unqualified and whole-pipeline product budgets UNKNOWN. Full-source
continuity, real development/independent transfer, physical P4, fresh acceptance
and release remain separate unfinished gates. VERSION remains1.0.1.
