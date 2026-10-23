# Coupled neural-event timing: ordered implementation plan

Date: 2026-10-23. **DESIGN_PROPOSAL_NOT_REGISTERED. No new candidate implemented.**
Authority for this proposal: [interval diagnosis](DENSE-DECODER-DIAGNOSIS-20261023.md)
and [large-tail audit](DENSE-DECODER-TAIL-20261023.md).
The [machine-readable plan](COUPLED-TIME-IMPLEMENTATION-PLAN-20261023.json)
records order, proposed constants, gates and unresolved decisions.

## 1. Decision and scope

Use one bounded, source-wide regularizer of raw decoded neural beat times as
the next hypothesis to specify. Preserve the number/order of those events and
their neural downbeat ordinals. Couple final intervals during optimization;
there is no independent peak refinement after the timing solution.

This is separate from rejected `dense-joint-renewal-v1` and its immutable protocol.
Do not feed v1's rejected path into a smoothing rescue. The proposed observations
are the original Beat This! decoded events bound to the existing neural output
report, with exact preprocessing/model identity. The new constructor contract
must explicitly permit those observations; v1's forbidden-input list is unchanged.
Neither coarse/OLS tempo or meter nor truth/IDs/correctness may enter it.

The hypothesis addresses regularity of known-event intervals. It cannot insert
missing beats, delete extras, change neural downbeat ordinals, solve every
tempo-family mistake or resolve annotation ambiguity. Fifty-six original tail
targets outside the old period range do not justify widening any old bound.
Success, no-regression and transfer remain unmeasured. Product budgets/P4,
confidence calibration, model training and consumer integration remain open.

## 2. Proposed mathematical contract to freeze at CT-00

Input observations `a[0..N-1]` are original integer source frames. Unknown PCM
gaps cause whole-source abstention after validation. Proposed pure interface:

```python
construct(beat_frames, downbeat_ordinals, total_frames, windows, known_gaps=()) -> dict
```

Use 48 kHz source coordinates, total 1..1440000 frames, <=3072 events and <=8
plain contiguous windows covering the source. Events are strictly increasing
integers in [0,total); downbeat ordinals are sorted unique integers indexing them.
Reject booleans/malformed geometry, IDs, label-bearing fields and invalid counts.
Empty or fewer than nine observed events abstain; do not invent beats.

Let `D` be the second-difference operator on all interior event triples:

`(Dx)[j] = x[j+1] - 2*x[j] + x[j-1], j=1..N-2`.

Proposed single objective, in source-frame units:

`min J(x) = sum_i (x[i]-a[i])^2 + 16 * sum_j (Dx[j])^2`.

The second term penalizes changes of **final** adjacent intervals. There is no
OLS segmentation, reference-period term, path sweep or alternate winning model.
The data term makes the Hessian `2*(I+16*D.T*D)` positive definite. Thus the
real-valued box-constrained problem has a unique solution. This mathematical
property does not establish musical quality or finite-iteration performance.

For each event, let `g[i]` be the minimum available neighboring observed gap
(use the sole neighbor at source endpoints). Define proposed box radius:

`R[i] = min(960, floor(g[i]/4))`;
`L[i] = max(0, a[i]-R[i])`;
`U[i] = min(total_frames-1, a[i]+R[i])`.

All boxes are fixed from observations, independent of labels or windows. Adjacent
boxes retain positive separation, including after integer rounding. Check this
in code and generated tests. The 960-frame radius is one original feature hop;
it is a proposed research constraint, not a calibrated uncertainty estimate.
The coefficient 16 is one declared design choice, not a grid searched on music:
for the unconstrained alternating perturbation its interior attenuation factor
is 1/(1+16*16). No musical performance follows from that calculation.

## 3. Deterministic numeric algorithm

Propose float64 working coordinates, initial `x=a`, fixed lambda=16, no external
numerical library. Freeze this expression/evaluation order in CT-00 before code.
For coordinate i, collect interior triples containing it, in ascending center j.
Their coefficient b for x[i] is 1,-2,1. With other coordinates fixed:

`A = 1 + 16 * sum(b*b)`;
`B = -a[i] + 16 * sum(b * (triple_value_without_x_i))`;
`u[i] = -B/A`;
`x[i] = min(U[i], max(L[i], u[i]))`.

Each sweep visits i ascending, then descending, using the latest coordinates.
Afterward compute every coordinate's clipped minimizer from the same final x;
residual is `max_i abs(x[i]-clipped_u[i])`. Also check once at initialization.
Proposed stop: residual <=1e-6 source frame. Hard maximum: 4096 sweeps.
Nonfinite values or unmet convergence are technical INVALID, with no raw-event,
OLS or rejected-v1 fallback. Record sweeps, residual, objective and active bounds.

Round exactly once to integer frames by `floor(x+0.5)`. Preserve original ordinals
and event count, then validate bounds/strict order. Downbeat frames are exactly
those rounded beat frames indexed by the original neural downbeat ordinals.
Final frames determine window ownership, each once, with source-global ordinals.
Actual successor is the next source event across windows; final successor None.
No global meter is inferred from truth; any later projection derives meter from
the candidate's own events with the unchanged projection helper.

For each root, initialize A=1.0 and B=-float(a[i]). Iterate triple centers j
ascending; compute its without-i sum starting at0.0, visiting its other variable
indices ascending and adding coefficient-times-latest-coordinate separately.
Then add `16.0*b*b` to A and `16.0*b*without` to B with separate float64
operations. No FMA, reordered reduction or unregistered library solver. Clip u
by ordered comparisons, without rounding intermediate coordinates.

Serialize pre-round coordinates losslessly with float.hex, then specify
floor(x+0.5) as the numerical output rule. Independent verification replays the
fixed arithmetic/order with own loops and requires exact serialized coordinates
and output hashes, in addition to independent box/residual/objective checks.
The tiny rational oracle validates coordinate error <=1e-3 frame, objective
excess <=1e-3*max(1,J_exact), and no below-optimum objective beyond
1e-9*max(1,J_exact). These are numerical tests, not musical tolerances.
At near-half-frame optima, do not demand that a differently rounded approximate
oracle agree on the integer result: test prescribed saved-coordinate rounding
separately. Never claim exact-real optimum rounding from approximate convergence.

## 4. Evidence and resource boundaries

The first step is a synthetic-only registered numerical experiment. No real
candidate run is ready: a new private input/baseline manifest and data-exposure/
independence decision are absent. Do not reuse the old v1 CLI or preflight token
as authorization for this constructor. The old DD-05 rejection stays frozen.

The host engineering ceilings proposed for preflight retain 60 seconds/source,
1800 seconds/phase, 262144 KiB/process and 64 MiB/attempt. They are ceilings,
not measured needs or product limits. Four float64 vectors a/x/L/U have 98304
payload bytes at N=3072; Python objects, inputs, output, stack and runtime cost
are additional. At most `(3*4096+1)*N = 37751808` coordinate-root evaluations
occur at maximum N, including residual checks. Each has at most three triples.
Benchmark that bound on generated inputs before music; failure stops the design
for performance review rather than increasing caps or dropping hard cases.

Contrast with a naive fine-time dense DP: a 60-frame grid over 30 seconds gives
24000 time positions and 1041 incoming periods for 9600..72000 frames, or
99936000 states for meter4 alone. Even nine payload bytes/state is 899424000
bytes before other storage. This static lower estimate rules out that layout
under the current 256-MiB host cap; it is not a P4 measurement. The proposed
regularizer instead retains fixed event topology and linear storage.

## 5. Ordered coding checkpoints

### CT-00 — freeze the synthetic numerical specification

Files: `docs/rust/COUPLED-TIME-SYNTHETIC-PROTOCOL.json` and matching `.md`.
Bind this plan, the closed DD-05/diagnosis/tail identities, allowed constructor
inputs, objective/boxes, float64/ties, iteration/residual rules, caps and failures.
Define the output schema `libapta-coupled-time-candidate-1`, confidence0
uncalibrated, native/physical/promotion false. Freeze the half-frame validation
contract above and precise empty/gap versus technical-error status semantics.

**Exit:** all numerical/schema choices explicit, real candidate CLI disabled,
no new private labels/audio/logits opened. Commit/push before CT-01. This plan
is a proposal, not that completed registration.

### CT-01 — pure solver and independent tiny oracle

Files: `tools/apta_coupled_time.py`, `tests/unit/apta_1_1_coupled_time.py`.
Implement validation/boxes, objective, exact registered coordinate order,
residual, one final rounding and output mapping. Constructor does no I/O,
environment, IDs, randomness, scorer or model calls and never mutates inputs.
For N<=8, independently enumerate lower/free/upper active sets and solve the
remaining dense linear equations using exact Fractions in tests. Do not share
production derivative/root helpers. Test this solver primitive before the
constructor's N>=9 wrapper; it is not a real-input bypass. Compare optimum objective/coordinates/KKT
with declared numerical tolerance; separately cover prescribed rounding cases.

**Exit:** generated tests below pass with no fallback or alternate lambda.
Exact finite-iteration behavior and solver failure remain explicit. Commit/push.

### CT-02 — independent verifier and full generated resource preflight

File: `tools/apta_coupled_time_verify.py` plus tests/preflight report. Implement
own matrix/coordinate arithmetic and saved-coordinate validation; do not call
the production solver, root helper or rounding helper. Verify losslessly saved
coordinates, objective, convergence condition, box/ordinal/window constraints,
final integer rounding and output hashes under the registered replay contract.
Exercise N=3072, flat/ramped/jittered events, bound-active cases and forced
nonconvergence, with actual parent/worker timeout/RSS/output enforcement.

**Exit:** no skips; all generated checks/caps pass; report measured maximum
time/RSS/work/storage. Component-bound real CLI remains disabled. Commit/push.

### CT-03 — decide and register the musical evaluation boundary

Files: `COUPLED-TIME-DEVELOPMENT-PROTOCOL.json/.md`, private manifest outside Git.
Bind the exact raw-neural output, original model/preprocessing/source identity,
labels, known prior use, candidate code and exact baseline metrics/denominators.
This GTZAN set is already exposed; do not call it independent or fresh acceptance.
Document recording/training membership and a justified independent development/
transfer route; if missing, record INDEPENDENCE_UNRESOLVED. No invented manifest,
new acquisition, training or formal holdout opening follows automatically.

Retain separate event F1/CMLt, projected nominal, complete sequence, actual
interval and paired period/phase/joint/meter/downbeat gates. Missing support is
not success. On the same 100/600/401 cohort, if separately authorized as an
exposed diagnostic, the old strict targets 188 nominal joint,179 nominal
complete,98 actual complete and zero paired breaks remain the declared baseline
proposal, with four exact event macros nondecreasing to 1e-12. Do not move those
absolute counts to a new denominator. No threshold is weakened by this plan.

**Exit:** exact evaluation scope/counts/input hashes/gates and one-run stop
policy are registered before measurement, or an honest unrun independence gate.
This checkpoint does not by itself select/acquire a dataset. Commit/push.

### CT-04 — separate label-blind construct, verify, then score

File: `tools/apta_1_1_coupled_time.py` plus generated runner tests. Bind immutable
input/runtime/clean-source identities; construct from decoded neural events
only, freeze every output/hash before references, independently verify, then
evaluate in another process with unchanged canonical adapters. Public CLI needs
both CT-02 preflight and CT-03 completed registration tokens; no override flags.
Test absent/permuted labels, corrupted input, partial cohort, overwrite, late
imports, unsupported schema, missing verification and resource failures.

**Exit:** complete generated operational preflight, same committed source for
all phases, no real run yet. Commit/push.

### CT-05 — one authorized registered attempt and hard decision

Use new external destinations. No sweep, automatic retry, topology fallback,
old-OLS selection or parameter correction after labels. Preserve all partials.
Integrity/convergence/resource failure => INVALID/INCOMPLETE; complete musical
gate failure => REJECTED; all development gates => DEVELOPMENT_PASS_ONLY.
Publish aggregate fixes/breaks/support/genre/resource/identity evidence and
commit/push. Transfer and whole-pipeline/P4 feasibility remain separate future
gates; a host timing solver does not qualify the neural front end or product.

## 6. Required generated fixtures

- Constant source periods both divisible and not divisible by 960, with exact
  affine observations unchanged to the numeric contract; no forced integer-row
  tempo. Use frame periods 24000,25000,30240 as explicit arithmetic fixtures.
- Alternating +/-160-frame observations at period24000, and irregular bounded
  offsets; check final intervals and phase directly, not only OLS projection.
- Smooth tempo ramp, one abrupt tempo change and two distinct constant regions;
  compare to the exact tiny oracle and expose any regularity bias without hiding
  failed cases or selecting a new coefficient.
- A missing beat, an extra beat and half/double-rate topology: event count and
  neural downbeat ordinals must remain unchanged. Demonstrate this limitation.
- Shifted source coordinates, near-end boxes, minimum gaps, one-frame gaps,
  half-frame rounding, symmetric and active-bound solutions, strict ownership
  across a window boundary, endpoint exclusion and final unknown successor.
- Malformed types/labels/IDs/gaps, N<9, gap abstention, maximum capacities,
  input immutability, no-I/O imports, forced convergence failure and cap failures.

Fixtures establish numerical behavior, not music or P4 acceptance. Do not tune
lambda/radius/iteration policy on GTZAN in response to these proposed stages.

## 7. Mathematical source and limits

The proposal is a box-constrained convex quadratic program; the Hessian and
coordinate minimizer above are derived for this objective. General convex-QP
optimality and strict-convexity background: Boyd and Vandenberghe,
[Convex Optimization, author's book page](https://www.seas.ucla.edu/~vandenbe/cvxbook.html).
That source supplies mathematical background, not evidence for this particular
audio hypothesis, iteration cap, numerical fidelity or resource feasibility.
