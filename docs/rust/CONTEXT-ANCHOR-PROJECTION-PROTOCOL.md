# P0 context-anchor projection registration

Date: 2026-10-24. **REGISTERED, GENERATED ONLY; implementation and execution disabled.**
Decision baseline: `9d5e2ea1e4342dd29abb412dc1049ed0c74dcf45`, `rust-rewrite`.
Authority: [C3 metadata qualification](COUPLED-TIME-METADATA-C3-20261024.md),
[R5 diagnosis](COUPLED-TIME-R5-AUDIT-20261024.md) and
[separate repair plan](COUPLED-TIME-R5-REPAIR-PLAN-20261024.md).
The [machine registration](CONTEXT-ANCHOR-PROJECTION-PROTOCOL.json) binds the
reviewed source/specification/consumer snapshot and [new record schema](CONTEXT-ANCHOR-PROJECTION-SCHEMA.json).
The [P0 receipt](CONTEXT-ANCHOR-PROJECTION-P0-20261024.json) records registration
and existing native-coordinate controls only. No new projector is implemented.

## Decision and evidence

Select the **projection/representation axis**, keeping the current coupled-time
candidate and every old scorer/result unchanged. The hypothesis is mechanical:
a nonnegative source-valid OLS context anchor can describe an owned APTA lattice
even when its origin precedes a window. Publishing only that lattice's owned
points can preserve phase and model ordinals without moving the anchor or raw
events. This hypothesis asserts neither musical improvement nor product readiness.

The failure already diagnosed by R5 returns before meter extraction when a
rounded OLS intercept precedes the window. It is not evidence that the candidate
lost a beat/downbeat. P0 reads that aggregate diagnosis; it does not reopen its
case coordinates, labels, scoring output or old candidate files.

| Reviewed layer | Actual contract / consequence |
|---|---|
| `specification/beatgrid.md`, sections4/5 | The anchor is a mathematical phase origin. A context beat before applicability is allowed; only owned beats count as coverage. |
| `rust/src/grid.rs`, `GridSegment::beat_at` | Uses exact Q32 ceiling to skip context points and advances the ordinal by the same integer. Source/ordinal overflow fails. |
| `rust/src/analysis.rs`, internal `beat_count` | Uses a rounded-up integer period for its internal count. It is not the exact Q32 ownership oracle for this registration. |
| C/Rust generic builders and GGRD representation | Segment applicability is bounded; the model anchor need not equal the first owned beat. This is representability evidence, not blanket validation of arbitrary counts or source-invalid anchors. |
| C/Rust MTRD/grid matching | A published meter downbeat must be owned and identify the same encoded grid point's whole frame and signed ordinal. MTRD carries no fractional frame. |
| `history_grid::prepare` | This producer still requires an in-window estimate anchor. P1 does not change this producer or claim it accepts the new diagnostic model. |
| Frozen Python projector and scorers | Reject the anchor before extraction; their point expansion starts at the anchor. Removing that guard in place would publish unowned context points and change old scores. They remain frozen. |
| Current product adapter snapshot | Uses `beat_at`, but requires continuous final full-source coverage, consecutive ordinals and one full-source meter. Independent projected windows do not satisfy that product contract automatically. |

Consumer snapshot was fetched read-only from Ruzina `origin/main` at
`df7f8f4aea3852cafb660ea67dae26f1610287ec`. The crate is now
`ruzina-apta-adapter`; the earlier checkout/probe name was `pajoniiir-apta-adapter`.
Its libapta dependency remains pinned to
`25d09de364e9507532c0508c838a8fecf0772d13`; that revision's `beat_at` also has the
context-skip arithmetic. The local consumer checkout stays at its older revision;
unrelated local files, branches, pins and firmware are preserved. This is source
inspection, not a build of the current consumer or hardware qualification.

## Pure input and fit

Future file: `tools/apta_context_projection.py`.
Interface: `project_window(beat_frames, downbeat_ordinals, total_frames, first_frame, end_frame)`.
There is no CLI, source ID, reference, old score, label, confidence selection,
candidate-construction callback, environment override or file access.

Validate the entire input before abstention: exact JSON lists and integers;
0..3072 strictly increasing beats in `[0,total_frames)`; strictly increasing
downbeat ordinal indices into that list; `1 <= total_frames <= 1440000` and
`0 <= first_frame < end_frame <= total_frames`. Booleans/floats are invalid.
Empty global beats/downbeats are allowed; fewer than4 owned observed beats is
`INSUFFICIENT_EVENTS`. The caller retains original source/known-gap validation.
This window model establishes no coverage or continuity across a source gap.
Any later serialized input/output must use finite, duplicate-free JSON with
lossless integer parsing before schema and semantic checks. Binary64 conversion
of integer fit numerators or Q32 coordinates is not an allowed intermediate.

Let `y[i]` be the n observed beats in `[F,E)`, and g their first original source
event index. Never reset source coordinates or change those indices. For n>=4:

```text
sx = n*(n-1)/2; sxx = n*(n-1)*(2*n-1)/6
b = (n*sum(i*y[i]) - sx*sum(y[i])) / (n*sxx - sx*sx)
a = (sum(y[i]) - b*sx) / n
round(v) = floor(v + 1/2), using exact rational arithmetic
A = round(a); T = round(48000*60000/b)
P = floor((48000*60000 * 2^32) / T)
```

These are the unchanged frozen fit/round/tempo/period formulas. Nonpositive b
after valid ordered input is a technical error. Out-of-range tempo returns
`UNSUPPORTED_TEMPO` at the same inclusive40000..300000 limits. A<0 or A>=source
end returns `SOURCE_ANCHOR_OUT_OF_DOMAIN`; no signed-source ABI, clamp or rebase
is introduced. Preserve A exactly, including A<F. Save reduced rational fit
numerators/positive denominators for independent verification.

## Owned Q32 lattice and ordinals

For a valid model use only integer arithmetic:

```text
Aq = A * 2^32
j_first = max(0, ceil((F*2^32 - Aq) / P))
j_last = floor((E*2^32 - 1 - Aq) / P)
position(j) = Aq + j*P
model_ordinal(j) = g + j
```

Emit precisely j_first..j_last; E is exclusive, F inclusive, and no earlier or
later point is owned. The minus1 is one Q32 unit, not one source frame. Emit no
point when the interval is empty (`NO_OWNED_POINTS`). At most3072 points are
permitted per output; overflow/capacity is an explicit technical error, not a
partial output. P1 must also reject output positions outside the source domain.
Native conversion is `(whole=position>>32, fraction=position & (2^32-1))`.

g+j names a model ordinal. It does not reassign any raw event/downbeat ordinal
or establish a bijection with every observed beat. The raw candidate bytes and
topology stay unchanged. In particular, an observed first beat can be owned
while its fitted model point lies outside: report that distinction, never insert
the raw beat into the model or assert one-to-one completeness by construction.

Each window is independent. Adjacent windows can have ordinal gaps/conflicts or
different phases. P1 reports per-window models only; it does not join them into
a final continuous GGRD or renumber ordinals. A future join/product hypothesis
must explicitly handle those conflicts under the existing consumer contract.

## Meter witness and publication ownership

Use only downbeat events from this input. Scan consecutive downbeat triples whose
raw frames are inside `[F,E)`, in original order. A triple (l,m,r) supplies two
equal observed bars only when `m-l == r-m` is3 or4. Use the same first eligible
left-witness arithmetic as the frozen projector:

```text
j = round(((beat_frames[l] - A) * 2^32) / P)
q = Aq + j*P
eligible = j>=0 and abs(q/2^32 - beat_frames[l]) <= b/10
```

Select the first eligible triple. If its left witness maps to an owned point,
publish that exact point and numerator. If that model point is outside ownership,
try only the middle then right witness of that same triple using the identical
nearest-index/tolerance test and require ownership. This names a later observed
downbeat of the same evidenced bars; it does not move a point, borrow baseline
meter, change raw downbeat indices or relax the1/10 tolerance. If none is owned,
publish no meter and report `NO_OWNED_WITNESS`. If no triple is eligible, report
`NO_ELIGIBLE_BARS`. Do not search later triples after selecting one merely to
obtain a preferred meter or phase.

Publish integer whole frame, Q32 remainder and `downbeat_ordinal=g+j`, with the
same original witness indices for audit. Require the exact lattice point inside
the same applicability range. The future MTRD bridge uses that point's whole
component and ordinal; retain the fractional remainder in diagnostics. Ordinal
zero is not an inferred downbeat. Confidence remains zero and uncalibrated.

When an old accepted window has an owned meter, require its old anchor/tempo,
all old nominal points and old numerator/denominator/downbeat whole/fraction to
match exactly. Previously native-unowned meter must be reported as a legacy
representation conflict, not silently counted as a correct new prediction.

## Verification, resources and stop policy

P0 checks documentation, bindings, strict schemas and public contract arithmetic
only. Five existing native-coordinate Rust controls pass on WSL using the locked
offline dependencies. Windows Cargo was unavailable; no Windows test PASS is
claimed. Those controls establish current arithmetic, not a new projector.

P1 implements only this pure generated projector and an independent oracle.
Use independently assembled rational normal equations/Gaussian elimination for
the fit; enumerate bounded candidate lattice indices and filter ownership rather
than reusing the implementation's ceiling formula. Verify exact witness/ordinal
mapping, half ties, Q32 carries and endpoints, positive context and source-negative
anchors, insufficient input, empty downbeats, changed downbeats with invariant
grid points, invalid types/ordering, capacity and per-window independence. Check
the legacy accepted intersection using only public synthetic events. Import/I/O
isolation and no input mutation are mandatory. No saved-source scoring occurs.

The fixed registration examples are public arithmetic independent of the private
R5 case. They are contract illustrations, not a measurement or tuning bank.
Their all-zero registration hash is a template placeholder; checks substitute
the actual registration digest before validation. Such a template is never an
authorized implementation output. Lattice-only examples exercise integer
ownership arithmetic without asserting that each arbitrary model is an OLS fit.
Test budget:60s per generated source,1800s per suite,262144KiB per process and
67108864bytes of external evidence, without increasing original host caps.
Maximum3072 events,8 windows,1440000 source frames and48000Hz remain fixed.
Bound work by observations+owned points per window; a source wrapper may make at
most8 calls and must enforce its total3072-point budget. These are host budgets;
no Rust/P4 allocation, latency or throughput budget is inferred.
Defensive states may be proved unreachable under the constrained fit domain;
do not fabricate a mismatched fit merely to force branch coverage. The separate
lattice-only examples cover ownership boundaries without making that claim.

Any changed raw candidate, shifted/refitted anchor, unowned point/meter,
ordinal/fit/legacy-intersection disagreement, arithmetic/capacity/isolation error
or resource failure vetoes P1. Preserve evidence and stop; no threshold rescue,
partial result, corpus retry or parameter sweep follows. A counterexample may
require a separately registered contract amendment before implementation resumes.

P2 separately prepares C/Rust generic-builder/GGRD/MTRD interoperability and
consumer-policy probes. It does not change the consumer pin/provider or history
producer. P3 must separately register a matched projection evaluation: apply one
new representation to both original-event control and fixed coupled candidate,
keep grid/meter/event/support denominators and unsupported sets explicit, and
separate new support from correctness on the shared supported intersection.
Every old scorer and16-gate verdict remains a frozen parallel control. No new
scientific acceptance thresholds or evaluation are activated by this registration.

## Frozen and unrun gates

All37 C3 components,25 old R4 files,13 scientific dependencies, C0-C3 authorities,
old preflights/results and VERSION1.0.1 remain unchanged. C3's REJECTED14/16 is not
rescored or promoted. No raw audio, labels, per-source stored outputs or model
inference is opened here. There is no new candidate, transfer, confidence
qualification, native activation, physical P4, final acceptance or release.
H0's separately registered real-runtime metadata audit and an explicit real-scope
decision are still required. The real authorization file stays absent. The new
decision is a generated representation hypothesis, not permission for real replay.
