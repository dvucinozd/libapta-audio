# CT-02: independent replay and supervised generated preflight

Date: 2026-10-23. **GENERATED_PREFLIGHT_PASS; musical development NOT_RUN.**
Implementation parent: `ac64c8bf23dbce6ebd820e3eb40e51e46f905557`.
[Aggregate evidence](COUPLED-TIME-CT02-20261023.json) binds exact implementation,
verifier, generated-preflight tool, supervisor, tests, combined runner and
external verification script/log/report hashes. This is component-bound generated
evidence on the CT-01 parent, before checkpoint publication, not a clean-source
real evaluation.

The [CT-00 contract](COUPLED-TIME-SYNTHETIC-PROTOCOL.md),
[registered protocol](COUPLED-TIME-SYNTHETIC-PROTOCOL.json), schema, CT-01 solver
and all rejected dense-v1 evidence remain unchanged. Protocol SHA-256:
`5f23a96330e7852f9dcb21b6273acf258e7252177da9bef53ceb9e3bea1afced`.

## Independent verification

The [memory-only verifier](../../tools/apta_coupled_time_verify.py) validates
inputs and independently constructs boxes, clipped coordinate roots, forward/
backward updates, residual, ordered objective, rounding and source/window maps.
It imports no production module and calls/shares no solver arithmetic helper.
Exact replay covers canonical coordinate hex, feasibility, residual, work counts,
active bounds, final frames, downbeat ordinals, windows, ownership changes and
abstention metadata. Recursive comparison requires exact Python types and finite
float.hex identity, so bool/int/float lookalikes and signed-zero changes fail.

The byte API checks the frozen SHA-256, rejects duplicate JSON keys and nonfinite
values, and requires exact independently replayed bytes. The generated harness
uses the CT-01 candidate-hash convention: UTF-8 compact sorted JSON with
separators(',', ':'), allow_nan=False, no trailing newline. This fixes a host
artifact convention; it does not change solver arithmetic or a product wire ABI.
Neither verifier API reads files, labels or model data. There is no real-data CLI.

## Generated process supervision

The [generated-only preflight tool](../../tools/apta_coupled_time_preflight.py)
accepts only a new external output directory. All observations come from its
fixed public case registry; there is no audio/event/label input, scoring command,
tuning flag or resource override. It binds the registered protocol/documents,
exact CT-01 constructor, component files and interpreter, and rechecks identities.
Workers capture/recheck loaded runtime bindings beside their external reports.

Construction and verification run as separate phases, with one fresh process per
source. All 21 constructor candidates and their hashes freeze before any verifier
worker starts. Verification receives each saved candidate and frozen hash while
regenerating observations independently from the fixed registry. Every source
report confirms that only its own numerical implementation was imported.

The unchanged [neutral supervisor](../../tools/apta_dense_runner_support.py)
enforces 60 seconds/source, 1800 seconds/phase, 262144 KiB/process and 67108864 bytes
for the combined attempt tree. The outer phase group contains all source workers;
inner supervisors inherit that group so a phase kill reaches the complete group.
RSS/HWM are sampled per PID, including supervisor, phase and source processes.
Source workers also impose RLIMIT_AS at the registered ceiling and report self
high-water RSS. Sampling targets 20ms, with actual interval affected by procfs/
artifact checks; it is not continuous RSS tracing. Self reports and address-space
limits supplement observation. Final byte checks include source/phase logs,
resource reports, freezes, candidates, verification and completion output.

On timeout/RSS/output/process failure, partial artifacts and INVALID diagnostics
remain; completion is not published. Existing destinations are rejected.

## Executed checks and resources

**37 tests PASS: 23 unchanged CT-01 controls plus 14 new CT-02 tests; zero skips,
failures or errors.** The [new suite](../../tests/unit/apta_1_1_coupled_time_verify.py)
covers exact replay, separate Fraction oracle comparisons for N1..8, 141 corrupted
saved leaves/containers, additional numeric/type/abstention corruptions, duplicate
JSON fields, NaN/overflow, wrong hashes, noncanonical bytes and byte capacities.
Purity, immutability and poisoned production helpers confirm the arithmetic
boundary. Rounding nextafter/signed-zero and technical convergence failures pass.

Actual subprocess tests reject lowered test-only timeout/RSS/output ceilings,
observe a descendant exceeding RSS, terminate its group and reject an otherwise
successful root leaving a live descendant. These deliberately INVALID probes are
successful failure-detection checks. SIGKILL delivery is asynchronous; the test
waits at most two seconds for disappearance/zombie state. An initial immediate
state assertion was repaired without changing supervisor, solver or limits.

All **21 cases** freeze and replay byte-for-byte: three affine periods, jitter,
ramp, abrupt regions, missing/extra events, half/double rates, window crossing,
source endpoints, fixed boxes, translated coordinates, three full-capacity shapes,
empty/short support and two gap statuses. Every candidate also passes the bound
Draft2020-12 schema using external jsonschema 4.10.3. Unit suites themselves use
only the standard library. All three full-capacity candidate hashes still match
CT-01; the old dense component-bound preflight token remains unchanged.

| Observed host quantity | Construct | Verify | Registered ceiling |
|---|---:|---:|---:|
| Whole phase wall seconds | 60.928 | 116.442 | 1800 per phase |
| Maximum supervised source seconds | 6.624 | 11.216 | 60 per source |
| Maximum source call seconds | 5.231 | 9.089 | included in supervised source |
| Maximum source self high-water RSS KiB | 19516 | 20308 | 262144 per process |
| Maximum supervised process RSS/HWM KiB | 36176 | 36176 | 262144 per process |

The complete generated attempt contains 1359576 bytes, below 67108864. The largest
source call is the full N3072 ramp, with 163 sweeps, 1505280 roots and 1001472 updates.
Phase totals include process startup, artifact/hash checks and supervision over
21 sources. They are not per-source solver times. The supervised RSS maximum
includes the parent; source self RSS is reported separately. Four constructor
double vectors remain 98304 payload bytes, excluding Python and output objects.
None of these measurements is a whole neural-pipeline or ESP32-P4 budget.

Reproduce focused checks on Linux/WSL from the repository root:

```text
python3 tests/unit/apta_1_1_coupled_time.py -v
python3 tests/unit/apta_1_1_coupled_time_verify.py -v
python3 tools/apta_coupled_time_preflight.py --output /tmp/libapta-ct02-new
```

Use a fresh output path. The second suite already performs the complete generated
process preflight; the third command independently retains its artifacts. Both
suites are wired into [the combined runner](../../rust/check.py). Syntax checks
pass; the broader C/Rust build/test matrix was not rerun for these host modules.

## Next checkpoint and remaining gates

**Next: CT-03 decide and register the musical evaluation boundary.** Bind exact
neural observations/model/preprocessing, new baseline/output identities, labels,
denominators, paired gates and one-run stop policy before any measurement. Record
prior exposure and recording/model-training independence. Existing GTZAN labels
are exposed; do not call that cohort fresh or independent. Unknown independence
remains UNRESOLVED. Do not invent a manifest, acquire/train a model, open holdouts,
or enable a real run to close that gate automatically.

CT-04 must still implement the separately gated construct/verify/score runner
with real-input registration and label isolation. CT-05 remains a separately
registered/authorized attempt. The generated preflight is not that permission
or a musical result. No private inputs, new inference, scoring, threshold change,
native/consumer activation or release occurred. Transfer, calibration, physical
P4 and fresh acceptance remain unqualified; dense-v1 stays REJECTED.
