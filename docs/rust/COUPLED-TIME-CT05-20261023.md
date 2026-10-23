# CT-05: complete construction, incomplete timed verification

Scope date: 2026-10-23; closeout: 2026-10-24 Europe/Zagreb.
**INVALID: verification phase wall-time cap exceeded. Musical evaluation NOT_RUN.**
Measured clean source: `362b6fa54ca3903794a34729d66bb5b7b202a446`, committed and
pushed before construction. The same clean local/remote SHA was verified again
after termination, before editing these result documents.

The [one-attempt scope](COUPLED-TIME-CT05-SCOPE.md),
[authorization](COUPLED-TIME-ATTEMPT-AUTHORIZATION.json),
[CT-03 registration](COUPLED-TIME-DEVELOPMENT-PROTOCOL.json) and
[CT-04 preflight](COUPLED-TIME-CT04-20261023.json) remain unchanged.
The [aggregate evidence](COUPLED-TIME-CT05-20261023.json) binds their hashes,
components, manifests, frozen candidates, phase receipts and external driver.
Its INVALID_OR_INCOMPLETE status denotes an INVALID operational attempt with an
INCOMPLETE whole-cohort verification, not an unresolved musical result.

## Executed sequence and outcome

Read-only scope checks rehashed all643 original bound artifacts and confirmed
the unchanged100-source/600-window cohort. The existing Python3.12.3 experiment
environment invoked these public roles once each, without overrides:

```text
python tools/apta_1_1_coupled_time.py construct --attempt ATTEMPT --inputs INPUTS
python tools/apta_1_1_coupled_time.py verify --attempt ATTEMPT
```

ATTEMPT is the single hash-bound external destination; INPUTS is the registered
label-free CT-03 manifest. Exact executable/arguments/invocation records remain
external. The driver stopped on verify's return code2. The evaluate role was
never invoked; no real labels, baseline score values or musical metrics were
parsed by this attempt. There was no new inference, audio decoding or acquisition.

Construction completes **100/100**, freezes all candidate bytes/hashes and writes
a successful supervised completion. All100 candidates have candidate_events
status and cover600 windows; this status means output availability, not musical
correctness. All6095 input beats are retained as6095 output beats. Construction
reports at most340 sweeps and residual9.994073479902e-7frames. Eleven events change
window ownership under the prescribed final-coordinate mapping. These are
constructor diagnostics;44 candidates lack independent verification.

Independent verification completes **56/100** source reports, all PASS, then the
phase supervisor reaches its unchanged1800-second ceiling. The retained
verify.INVALID.json states wall-time cap exceeded. There is no verify completion
or supervised-success envelope. The remaining44 are unverified; partial success
cannot enable scoring or qualify the cohort. There are **no musical scores,
paired fixes/breaks, genre comparisons or new baseline reproduction claim**.

## Host resource evidence

| Phase | Supervised wall seconds | CLI wall seconds | Maximum observed process RSS/HWM KiB | Result |
|---|---:|---:|---:|---|
| Construct | 1699.345 | 1733.858 | 21212 | PASS,100/100 |
| Verify | 1800.045 | 1823.165 | 22204 | INVALID,56/100 |
| Evaluate | NOT_RUN | NOT_RUN | NOT_RUN | NOT_RUN |

The fixed ceiling applies to the supervised phase. CLI time also includes the
outer identity checks and dispatch/teardown; both are reported rather than
concealing that overhead. Binding/rechecks inside the phase are included in its
supervised time. These are host operational measurements, not pure solver,
neural-inference or physical P4 latency measurements.

The maximum completed-source supervision time is29.793s for construction and
46.432s for verification, below60s/source. Maximum completed-worker self RSS is
20392/21576KiB. Memory stays below262144KiB/process. The attempt directory contains
11934333bytes, below67108864; retained external driver/evidence adds22551bytes
before aggregate publication. Timeout is the observed failing resource gate.

The generated two-source CT-04 run did not establish100-source throughput.
This real attempt exposes that scaling gap. These aggregate timings do not
establish whether numerical replay, repeated provenance work, filesystem cost
or another component dominates; that requires a separate read-only diagnosis.
Do not attribute this timeout to musical quality or model-inference feasibility.

## Closed attempt and next step

A read-only closeout rehashes all100 frozen candidates and1653 unique retained
bindings, verifies all56 successful partial reports, confirms scoring never
started and finds zero remaining runner processes. Candidate bytes and bound
inputs remain intact. There is no retry, changed coefficient, larger resource
limit, alternate topology, subset score or implementation repair in this attempt.

The authorization is consumed. Keep its destination, partial reports, INVALID
receipt and original checkpoints intact. **Next: a separate read-only diagnosis
of verification cost and a plan for cohort-scale generated operational preflight.**
Any later repair needs its own checkpoint, updated component-bound preflight
and explicitly registered authorization before another real attempt. This result
does not authorize automatic replay/scoring of the saved cohort.

Independence remains UNRESOLVED on the already-exposed GTZAN set. No independent
transfer, confidence calibration, native integration, whole neural-pipeline/P4
feasibility, fresh acceptance, consumer adoption or release gate is closed.
Dense-v1 stays REJECTED. Stable version and production behavior are unchanged.

Private evidence remains outside Git in the fixed WSL destination and
`D:/AI/LIBAPTA/outputs/coupled-time-ct05-20261023/`, including precheck, execution,
closeout and aggregate scripts/receipts. All source IDs, coordinates, job inputs,
candidate bytes, private paths and process-level detail stay external. This
checkpoint changes only scope/result documentation and public aggregate evidence;
the CT-04 implementation/tests are byte-identical. The full C/Rust matrix was
not rerun, and the historical53-test generated result is not a new test claim.
