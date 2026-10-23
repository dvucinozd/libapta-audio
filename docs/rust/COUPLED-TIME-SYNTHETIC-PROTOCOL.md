# CT-00: coupled neural-event timing, synthetic numerical registration

Date: 2026-10-23. **REGISTERED_SYNTHETIC_ONLY / implementation NOT_RUN.**
Machine authority: [protocol JSON](COUPLED-TIME-SYNTHETIC-PROTOCOL.json).
Structural output contract: [candidate schema](COUPLED-TIME-CANDIDATE-SCHEMA.json).
This registration instantiates the immutable
[implementation plan](COUPLED-TIME-IMPLEMENTATION-PLAN-20261023.md) at parent
`cb37a832242fd3f2c70307f384c60027f2bd1671`. The protocol JSON binds the plan,
this normative text, schema and closed DD-05/diagnosis/tail aggregate hashes.
Its own exact file SHA-256 is the future constructor's protocol token.

## Scope and unrun gates

Hypothesis: `coupled-neural-event-time-v1`. Jointly regularize final event times
around decoded neural observations, preserving event topology and neural
downbeat ordinals. This is a new hypothesis with an explicit new input contract;
the rejected `dense-joint-renewal-v1` protocol and REJECTED verdict remain frozen.
This document does not authorize a real-corpus run or implement a solver.

Only generated arithmetic, schema, oracle and resource fixtures are registered
here. CT-01 implements the pure solver; CT-02 independently verifies/preflights
it; CT-03 separately binds actual neural input/baseline/label identities and
exposure/independence; CT-04 tests process isolation; CT-05 is a separately
registered/authorized attempt. Existing dense-decoder CLI/preflight tokens do
not enable this constructor. No real constructor/score CLI exists at CT-00.

Real input manifest, exact new baseline and independent development/transfer
membership are unresolved. Existing GTZAN labels are already exposed. No new
audio, logits, private labels, model/inference, training, dataset acquisition,
holdout, C/Rust default, consumer pin/provider or package version is changed.
P4, whole neural-pipeline budgets, confidence and release remain unqualified.

## Input interface and validation order

```python
construct(beat_frames, downbeat_ordinals, total_frames, windows, known_gaps=()) -> dict
```

No other argument or geometry field is accepted. Event observations and neural
downbeat ordinals are allowed; rejected-v1 paths, coarse/OLS tempo or meter,
truth, IDs, source names and saved correctness are forbidden. A future runner
must bind the original model/decoded-event identity before passing plain arrays.
The pure constructor performs no I/O, environment, randomness, subprocess,
scoring/model calls, stdout or input mutation.

Validate every input before any abstention:

1. Integer means Python `type(value) is int`; reject booleans and floats even
   when numerically integral. `total_frames` is 1..1440000 at source rate48000.
2. Event and ordinal sequences accept list, tuple or stdlib array containers;
   values still obey the exact integer rule. Events have length0..3072 and are
   strictly increasing in [0,total). Downbeat ordinals are strictly increasing,
   unique and in [0,N), so are empty when N=0. No event insertion/deletion.
3. Windows accept list/tuple length1..8. Each is a dict with exactly
   `first_frame,end_frame`, integer endpoints, first<end, contiguous and covering
   [0,total). No labels/meter/tempo or extra keys. Preserve their original order.
4. Gaps accept list/tuple of two-element list/tuple ranges with integer
   0<=first<end<=total. Ranges are ordered and nonoverlapping; adjacency is
   allowed. Preserve every range without merging. Their count is geometrically
   bounded by total_frames, not a new tuned metadata cap.

Valid nonempty gaps give whole-source `known_pcm_gap`, taking precedence over
minimum event support. Otherwise N<9 gives `insufficient_events`. These are
legitimate support statuses, never musical successes. Only valid no-gap N>=9
enters the solver. Invalid shapes/capacities raise `ValueError('invalid_input')`;
do not replace invalid data with abstention.

Internal primitive: `solve_times(observations, lower_bounds, upper_bounds)`
accepts three equal-length list/tuple/stdlib-array sequences, N=1..3072.
Observations are strictly increasing actual ints in0..1439999; each supplied
box has actual int0<=L[i]<=a[i]<=U[i]<=1439999. Toy boxes may overlap; this
primitive does not emit candidate events or claim event-order/window semantics.
It returns exactly the solution block described below, with fresh lists.
Its N<9 use is not exposed as a real constructor bypass. Constructor input always
follows the policy above and its own derived boxes must be strictly separated.

## Boxes and objective

Copy integer observations a; do not reset source coordinates at window boundaries.
For each i, g is the minimum available adjacent observed gap (sole neighbor at
the two endpoints). Define integer radius R=min(960,g//4), L=max(0,a-R),
U=min(total-1,a+R). The constructor's N>=9 guarantees a neighbor. The toy solver
receives explicit finite integer boxes L<=a<=U when N=1.

For every adjacent pair, assert U[i]<L[i+1]. Rounded points must remain within
these integer boxes; their strict ordering follows from this separation.
Failure is technical `invalid_output`, with no coordinate repair/deduplication.

For all interior centers j=1..N-2 let c[j]=x[j-1]-2*x[j]+x[j+1]. Minimize

`J(x) = sum_i (x[i]-a[i])**2 + 16 * sum_j c[j]**2`, subject to L<=x<=U.

All weights are1, lambda is16, and initialization is x=float(a). There is one
coefficient/radius/solver policy and no candidate sweep. The real-valued Hessian
is 2*(I+16*D.T*D), positive definite. This property establishes a unique
box-constrained optimum, not a musical pass or exact optimum after finite work.

## Float64 order and coordinate minimizer

Require binary64 with53 significand bits, max exponent1024, and 8-byte stdlib
double arrays. Unsupported arithmetic raises `unsupported_float64_runtime`.
All constants/operations below are finite binary64, rounded after each operation;
no FMA, reordered/vectorized reduction or substitute numerical library solver.

For a root at coordinate i, without mutating x:

1. Set A=1.0; B=-float(a[i]).
2. Visit centers j from max(1,i-1) through min(N-2,i+1), ascending. The coefficient
   b is -2.0 when i==j, otherwise1.0.
3. Set without=0.0. Visit k=j-1,j,j+1 ascending, skip i; coefficient is -2.0
   for k==j and1.0 otherwise. Compute term=coefficient*x[k], then
   without=without+term, as separate operations.
4. Compute t=16.0*b, aa=t*b, A=A+aa, bb=t*without, B=B+bb, separately.
5. Compute u=(-B)/A. Reject nonfinite intermediates. If u<float(L[i]), return
   float(L[i]); else if u>float(U[i]), return float(U[i]); else return u.
   Equality retains u, including its signed zero. Do not skip a root when L==U.

Every root call counts once, whether for residual or an update. No hidden root
calls are allowed for reporting. A forward pass updates i=0..N-1; a backward
pass updates i=N-1..0, including both endpoints again. These two passes form
one sweep and use the latest values. There is no per-window restart.

For a residual pass, hold x unchanged and visit all i ascending. Compute
r[i]=abs(x[i]-clipped_root_i); residual is their maximum, initialized0.0 and
replaced only if larger. Check once before any sweep, then after each full
forward/backward sweep. Stop at residual<=1e-6 frame, inclusive; allow4096
sweeps. Report sweeps S, root_evaluations=N*(3*S+1), updates=2*N*S.
Initial convergence is S=0 and roots=N. Exhausted nonconvergence raises
`ValueError('nonconvergence')`; no candidate or fallback is returned.

After convergence, compute objective separately: data=0.0; ascending i,
d=x[i]-float(a[i]), square=d*d, data=data+square. Then smooth=0.0;
ascending interior j, t=2.0*x[j], c=x[j-1]-t, c=c+x[j+1], square=c*c,
smooth=smooth+square. Finally penalty=16.0*smooth, J=data+penalty.
Check finite, nonnegative output. Nonfinite arithmetic raises
`ValueError('nonfinite_numeric')`, never abstention or an emitted raw-event grid.

## Serialization, rounding and independent verification

Store every continuous x as canonical `x.hex()`; parsing it must recover the
identical binary64 value and round-trip to the same string. Finite signed zero
is permitted. The final integer event is **math.floor(x+0.5)** with a separate
binary64 addition before floor, exactly once per event. This is the registered
machine expression: do not substitute round(), Decimal or rational rounding.
Include nextafter neighbors of half-frames in tests; binary64 addition can itself
round at that boundary. Exact-real optimizer rounding is not claimed.

Independent verifier: own loops/root/objective/rounding implementations, same
registered operation order, exact serialized-coordinate and candidate-byte
replay; also validate feasibility, residual, counters, objective and mappings.
Do not call or share production update/derivative/rounding helpers.

Tiny oracle: N<=8, independently enumerate lower/free/upper active sets; solve
free-coordinate dense systems by exact Fraction elimination and check feasible
KKT conditions. Do not share production roots or derivatives. Compare continuous
coordinates within1e-3 frame, objective excess <=1e-3*max(1,J_exact), and no
below-optimum objective beyond1e-9*max(1,J_exact). Oracle comparison is before
the constructor support wrapper; rounding is tested against saved coordinates.
Near a half-frame, a differently approximated optimizer need not round identically.
Numerical tolerances do not change any musical tolerance or gate.

For the oracle, gradient is 2*(x-a)+32*D.T*D*x. A free coordinate has zero
gradient; a lower-only active coordinate has gradient>=0 and upper-only has
gradient<=0. A fixed L==U coordinate needs no one-sided sign test. Enumerating
duplicate active descriptions may yield the same unique coordinate vector;
select the feasible exact minimum, not a label-based tie. Dense Fraction systems
exist only in the tiny test oracle, never in the production solver.

## Output schema and cross-field invariants

All returned containers are new dicts/lists, without aliases to inputs. Exact
keys/types/structural capacities are in the bound schema; JSON cannot encode all
semantic relationships. Reject nonfinite JSON numbers and extra keys. Python
integer output fields are actual ints, not bool or float lookalikes. The digest
field must equal this registered protocol JSON's actual SHA-256, not just any
64-hex string. Never add truth, ID, tempo or selected/global-meter fields.

Common fields include format, hypothesis_name, protocol_sha256, total_frames,
input_event_count, status, confidence0 and confidence_calibrated=false;
native_graph_constructed, physical_p4, formal_acceptance and production_promotion
are all false. These false fields do not qualify future products or confidence.

For `candidate_events`:

- All N original events remain in order. `events.beat_frames` are rounded x;
  `events.beat_ordinals` is list(range(N)). `downbeat_ordinals` is the original
  neural ordinal list; downbeat_frames are exactly beat_frames at those indices.
- Solution coordinates_hex/lower_bounds/upper_bounds all have length N. Bounds
  are integer L/U. sweeps, roots, updates follow the exact formulas. Residual and
  objective are floats computed above, never null. Active-lower/upper ordinal
  lists contain exactly indices with x[i]==float(L[i])/float(U[i]); L==U indices
  occur in both lists. Lists are sorted, unique and within0..N-1.
- Every source window remains, with exact original endpoints. Its beat_ordinals
  are exactly the indices whose final integer frames lie in [first,end).
  Nonempty windows have candidate_events status; empty windows have no_events.
  Concatenated ordinal lists equal range(N), each once.
- `window_ownership_changes` lists only moved events in ascending ordinal order,
  with ordinal, observed_window and final_window. Membership uses original a
  versus final integer frames, never continuous x or a reference label.
- There are no known gaps. No additional post-solve peak refinement, event
  insertion/deletion, smoothing pass, source clamp, truncation or OLS fit occurs.

For either abstention: preserve total/input count, windows/endpoints and validated
gaps; all event/solution arrays and ownership changes are empty. Solution sweeps,
roots and updates are0; residual/objective are null. Every window carries the
same source abstention status. Bounds and active lists are uncomputed, not a
fake converged solution. Insufficient support has no gaps and input N<=8;
known_pcm_gap has at least one validated gap, irrespective of N.

Future actual-interval scoring uses the next integer source event, including
across windows, with final successor None. This is derived at scoring time,
not stored as a fabricated last period. Future meter/projected scoring uses
candidate-owned events and unchanged adapters under CT-03/04 registration.

## Technical failures and limits

Normative ValueError reason tags: invalid_input, unsupported_float64_runtime,
nonfinite_numeric, nonconvergence, invalid_output. MemoryError/OS/timeout/RSS/
output violations are technical failures too. No candidate result represents
these failures; a future runner preserves INVALID/INCOMPLETE partial artifacts
with no fallback or musical verdict. Do not expose tuning/bypass flags.

Maximum N3072, source1440000, windows8, lambda16, radius960, neighbor divisor4,
minimum support9, sweeps4096, residual1e-6 are fixed. At maximum, roots<=37751808
and updates<=25165824. Four double vectors a/x/L/U total98304 payload bytes;
this excludes input/output objects, stack, runtime and metadata. Working storage
must remain linear; do not allocate dense N*N matrices in the production solver.

Future generated preflight ceilings:60 seconds/source,1800 seconds/phase,
262144 KiB/process,67108864 output bytes/attempt, actual parent/worker supervision
and explicit child accounting. Resource data are NOT_RUN at CT-00. No limit is
a measured product/P4 budget or grounds for increasing wire/event capacities.

## Registered generated fixture families

All formulas below are generated source-frame observations, never real data.
Unless noted, downbeat ordinals are every fourth ordinal starting0 and windows
are one full source window. All actual tests remain NOT_RUN until CT-01/02.

| ID | Generated definition | Required check |
|---|---|---|
| G01 | N16, a[i]=1000+P*i, P in24000/25000/30240, total=2000+16*P | Initial convergence, S0, objective0, unchanged observations; no row-tempo quantization |
| G02 | N16, a[i]=1000+24000*i+(-160 if even else160), total400000 | Compare oracle where tiny; final intervals within1% of24000 and phases within10%, preserved topology |
| G03 | N16, a[i]=1000+23000*i+20*i*(i-1), total400000 | Box/objective/residual and tempo-ramp bias exposed, no hidden OLS-only pass |
| G04 | N16, a[0]=1000, following gaps23000 for indices1..7 then26000, total500000 | Abrupt change/regions, oracle agreement on tiny versions, no claimed musical success |
| G05 | Generated affine17-event sequence with ordinal8 removed, downbeats empty | Event cardinality remains16; never repair missing topology |
| G06 | Affine16-event sequence with an extra midpoint event, downbeats empty | Cardinality remains17; no deletion |
| G07 | N16 affine P48000 or12000, total=2000+16*P | Half/double topology remains unchanged; no learned family correction |
| G08 | N17, a[i]=1000+24000*i plus320 at i8, total500000; split193319 | Ordinal8 crosses from observed window1 to final window0 exactly once, downbeat ordinal preserved |
| G09 | N9, a[i]=24000*i, total192001 | Source endpoints L/U active, no endpoint emission at total |
| G10 | N9, a[i]=i, total9 | One-frame gaps, R0, active lower and upper lists overlap completely |
| G11 | Shift interior observations and interior window boundaries by524288; retain first_frame0 and increase total by524288, keep boxes unclipped | Translation within1e-3-frame coordinate accuracy and each case's own exact float replay; use affine P25000 for exact integer translation assertion |
| G12 | Toy N1..8, explicit feasible boxes, active lower/free/upper patterns | Exact rational active-set oracle before min9 wrapper |
| G13 | Saved rounding values k+0.5 and nextafter neighbors, signed zero | Exact registered machine addition/floor; no optimum-rounding claim |
| G14 | N3072, a[i]=1000+400*i+(-80 if even else80), total1440000 | Full capacity, bounded loops/storage/time/RSS, expose any nonconvergence |
| G15 | Same capacities with affine, ramped and bound-active generated inputs | Resource report retains all cases; no dropping hard fixtures |
| G16 | Empty/1..8 events, valid gap(s), both gap and short support; malformed types/windows/gaps | Validation before abstention, gap precedence, every window retained |
| G17 | Test-only monkeypatch for nonfinite root or exhausted sweep bound; watchdog timeout/RSS/output failure | Technical INVALID, no returned raw grid, no public override |
| G18 | Input deep-copy comparison and no-I/O/import test | Purity, no scorer/model/files/env access, no mutation |

Acceptance here is synthetic numerical/schema/resource evidence only. A failed
fixture stops before music; any design repair must be documented and re-registered
before a later candidate measurement, without claiming the old protocol passed.

## CT-00 exit and CT-01 handoff

Validate JSON/schema structure, document links, fixed constants and exact bound
hashes; check that no solver/verifier/real runner was introduced. Schema examples
are hand-constructed generated contract checks, not solver or oracle results.
Record this registration checkpoint and commit/push before CT-01.

Next files: `tools/apta_coupled_time.py`,
`tests/unit/apta_1_1_coupled_time.py`, exactly this registered pure contract and
independent tiny oracle. No real audio/event/logit/reference files are needed.
The broader existing Rust/C test matrix is not a CT-00 documentation result.
