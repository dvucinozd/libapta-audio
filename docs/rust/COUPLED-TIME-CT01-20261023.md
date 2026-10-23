# CT-01: pure coupled timing solver and exact tiny oracle

Date: 2026-10-23. **SYNTHETIC_IMPLEMENTATION_PASS; development NOT_RUN.**
Registration parent: `ed5b6d36ce1729c80f2f4497e802650708d5458d`.
The [registered numerical contract](COUPLED-TIME-SYNTHETIC-PROTOCOL.md),
[protocol](COUPLED-TIME-SYNTHETIC-PROTOCOL.json) and
[schema](COUPLED-TIME-CANDIDATE-SCHEMA.json) remain byte-identical.
Protocol SHA-256:
`5f23a96330e7852f9dcb21b6273acf258e7252177da9bef53ceb9e3bea1afced`.
Exact implementation, test, runner and external verification/log identities are
in the [aggregate receipt](COUPLED-TIME-CT01-20261023.json). Measurements identify
new file hashes on the registration parent, not a clean-source real evaluation.

## Implementation

The pure [module](../../tools/apta_coupled_time.py) implements the registered
source-wide box-constrained objective with lambda16, float64 operation order,
ascending/descending coordinate sweeps, initial and post-sweep residual checks,
inclusive 1e-6-frame stopping limit and hard 4096-sweep bound. It saves canonical
hex coordinates, computes the ordered objective, rounds once with the registered
binary64 addition/floor expression, and maps final integer events into windows.

Input validation precedes every abstention. Known PCM gaps abstain for the whole
source before the minimum-nine-events rule. Invalid inputs, unsupported arithmetic,
nonfinite arithmetic, exhausted convergence and invalid outputs raise technical
errors; no raw-event fallback is emitted. Memory/OS failures propagate.
Every event and original neural downbeat ordinal is retained. Missing/extra events
and half/double topology are explicitly not repaired by this timing objective.

The module imports only array/math/sys, with sys used only for float_info.
It performs no file/environment/model/scorer access and does not mutate inputs.
Four double vectors have maximum payload98304 bytes; working storage is linear.
This is a static vector payload, excluding Python objects, inputs/outputs,
runtime, stack and metadata; it is not a peak-RSS or whole-pipeline/P4 estimate.
There is no real-data runner or constructor CLI.

## Executed generated checks

The [stdlib-only unit suite](../../tests/unit/apta_1_1_coupled_time.py) passes
**23 tests, zero failures/errors/skips** on Linux/WSL Python3.12.3:

- G01..G11: affine periods24000/25000/30240, alternating jitter, smooth ramp,
  abrupt regions, unchanged missing/extra/half/double topology, window crossing,
  endpoints, one-frame fixed boxes and shifted coordinates.
- G12: **29 oracle comparisons**, with N1..8, fixed/free/lower/upper active
  patterns, jitter/ramp/step fragments and deterministic irregular boxes.
  The oracle constructs its own dense matrix, enumerates active sets, solves
  free systems with exact Fraction Gaussian elimination and checks exact KKT.
  It shares no production root, derivative or update helper. Coordinates and
  objective satisfy the registered tolerances; exact optimizer rounding is not
  asserted.
- G13: half-frame nextafter neighbors, machine-addition rounding and signed zero.
- G14/G15 arithmetic coverage: three full N3072/window8 cases below, retaining
  every event and checking counters, boxes, ownership, schema and objective.
- G16: empty/short/gap support, gap precedence, malformed types/geometry/ordinals,
  capacities and validation before abstention.
- G17 numeric portion: injected nonfinite roots during residual/update, actual
  4096-sweep exhaustion, inclusive residual stopping, invalid rounded output,
  unsupported float runtime and propagated MemoryError/OSError. No public
  tuning/bypass flag is added.
- G18: import/access checks, no-I/O execution, deterministic output and fresh
  containers/input immutability.

| Full generated case | Sweeps | Roots | Updates | Residual frames |
|---|---:|---:|---:|---:|
| a[i]=1000+400*i+(-80 if even else80) | 1 | 12288 | 6144 | 0 |
| a[i]=1000+400*i | 0 | 3072 | 0 | 0 |
| a[i]=1000+100*i+i*(i-1)//10 | 163 | 1505280 | 1001472 | 9.899320048134541e-7 |

All three use total1440000 and eight equal180000-frame windows. The jitter case
has1535 active lower and1535 active upper bounds. Its rapid convergence reflects
those active bounds; it does not establish worst-case solver runtime.

Reproduce the routine suite from the repository root:

```text
python3 tests/unit/apta_1_1_coupled_time.py -v
```

An external generated-only harness also validates **45 returned instances** with
Draft2020-12 jsonschema4.10.3, strict finite JSON and the registered schema. Its
instrumented test suite takes7.550s as a whole; this includes oracle and schema
work, not a supervised per-source resource measurement. Syntax, protocol/bound
hashes and the old dense component-bound preflight token pass unchanged.
The new suite is wired into [the combined runner](../../rust/check.py); the full
C/Rust matrix was not executed for this isolated Python module.

## Remaining gates and next checkpoint

**Next: CT-02 independent verifier and full generated resource preflight.**
Implement `tools/apta_coupled_time_verify.py` with its own root, update,
objective and rounding loops; it must not call/share production arithmetic.
Require exact saved-coordinate/candidate-byte replay, independent semantic
checks and corruption rejection. Retain all registered capacity shapes and
measure actual parent/worker time/RSS/output accounting. G17 watchdog timeout,
RSS and output-limit failures remain NOT_RUN here, as do independent replay and
the supervised resource gates. Do not treat arithmetic capacity coverage as
that preflight. Commit/push the verified checkpoint before CT-03.

No private events, audio, logits or reference labels were opened. There was no
new inference, music scoring, parameter change or retry of rejected dense v1.
Real input/baseline/label binding and independence remain unresolved. Transfer,
confidence, native/C/Rust default behavior, consumer activation, P4, acceptance
and release remain unqualified. The prior rejection and its artifacts stay frozen.
