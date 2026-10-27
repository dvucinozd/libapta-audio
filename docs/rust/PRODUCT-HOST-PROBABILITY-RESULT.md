# HS-06 probability/density/support bridge: measured host result

Date: 2026-10-27. Verdict: **HS06_SYNTHETIC_HOST_PASS_ONLY**.
Continuation package: RC-02, complete.
[Protocol](PRODUCT-HOST-PROBABILITY-PROTOCOL.md),
[implementation](PRODUCT-HOST-PROBABILITY-IMPLEMENTATION.md),
[machine result and artifact identities](PRODUCT-HOST-PROBABILITY-RESULT.json).

## Source and executed scope

All four registered cases ran once in order from clean, pushed source
`d6644460b1a19dd77d80abad81e52a2a8b7f0b8d`. Original implementation source is
`9d33af765ed8b5fd4f6d581f5d361ea9108bf3a0`; the intervening commits changed
documentation only. All six implementation hashes and all395 frozen protocol
bindings remain identical. No repair, retry, subset, cap, tolerance or backend
change occurred during the measured attempt.

A new external release build used `--locked --offline`, Rust/Cargo1.95.0 and
`experimental-compact-probabilities`, then an exclusive binary copy. Its SHA-256
is `cf4af69082f418b3f20cc3be0482ba3df5ba4c96b3ff38cc43607c2e904010ce`, equal to
the original implementation binary. Declared Rust minimum1.81 was not compiled.
Build logs and complete tool versions are retained externally. One prelaunch
shell quoting error happened before build/output/candidate execution; it was
corrected by passing validation through stdin Python. It was not a measured retry.

```text
python3 tools/apta_compact_probabilities_synthetic.py --executable /home/daniel/libapta-hs06-measured-20261027-rc02-01-bin/apta-compact-probabilities-synthetic --output /mnt/d/AI/LIBAPTA/outputs/hs06-measured-20261027-rc02-01
```

## Results and clock scopes

| Case | Observations | Certified rows | Worker wall s | Entire supervised wall s | Complete waited worker+descendants CPU s | Session.step total s |
|---|---:|---:|---:|---:|---:|---:|
| fixtures |35|850|14.832905|18.816029|2.962112|0.000059548|
| full-mixed |1|30000|0.954459|4.802168|1.144056|0.004543635|
| full-no-support |1|30000|0.663757|4.421421|0.885235|0.006349328|
| cancel |1|0|0.598402|4.748432|0.783911|0.000054987|

Total:38 observations and60850 certified final-view rows. The fixtures include
11 selector checks and8 small compositions. Invalid-case prefixes add14 committed
rows but emit no certified density view. Both full cases process all30000 rows
and150001 units each; full-mixed is Ready with support[0,30000), full-no-support
is NoEvidence with no decoder invocation. Full cases test the bridge only; the
HMM/grid composition runs only on the eight registered small cases.

Each emitted probability retains exact input bits; the separate worker checks
each density against the frozen scalar reference and short frozen values at
absolute4e-13, with exact -Inf for zero. Support, status, units, source geometry,
global event correction and selector decisions pass. Both meter scores and the
winning Trace use the same retained evidence, including the shifted-origin
composition controls. No new musical input, checkpoint or trained inference ran.

Session.step clocks exclude generation/hashing and transport. Whole supervised
wall includes process startup, allocations, runtime snapshots, receipt work and
exit. Worker wall starts later, after worker imports/setup; the two are distinct.
Full-case pipe write totals are0.255444s and0.174602s. Fixture composition totals
0.393474s. Largest observed Session.step call is0.116018ms; this single host
observation includes scheduling and is not a worst-case or P4 bound.

The cancel case sets a pending flag at686 units,138 accepted/137 committed rows,
NonBeatLog. Request-to-return is0.000421ms (0.421 microseconds); the bounded call
charges0 additional units, exposes no result and cannot resume after flag clear.
This measures a pending flag on the host, not asynchronous cancellation, final
cleanup or a product publication fence. Its diagnostic density-prefix hash is
not independently numerically certified because no density view was emitted.

## Resource and integrity audit

All per-case gates pass unchanged:CPU30s for worker and waited descendants,
wall45s for the complete supervised process lifetime,RSS64MiB/process and inherited
AS128MiB. Largest observed process HWM/envelope is20288KiB. This includes
process/library/fork effects and is not isolated Rust or device RAM usage.
Session144B and Driver960B remain host type-layout facts; caller output arrays
remain1080000B at30000 rows, separate from those values.

Three valid checker probes are accepted;17 malformed transport/number/JSON/event
variants are refused. All four wall/RSS/address-space/output violation probes
are refused. No live orphan is accepted by the supervisor. Final receipt audit
rechecks537 parent bindings and540 loaded-runtime entries across four workers,
all prebound. It verifies stream headers/trailers, record counts, hashes, exact
counters/status/capacities, source/probability prefixes, full reference identities,
resource limits and the clean source. Per-row numeric checking happened during
the registered worker run; the post-run audit does not reconstruct an unretained
raw tensor or claim a second independent numeric execution.

Transport records:859/30001/30001/1; transient bytes:67340/1291137/1291147/1133.
These streams are not retained tensor files. All retained run files, audit,
manifest, build logs/metadata and post-run audit script total **52 files/464481B**,
below the1MiB cap:46 run files plus6 build-metadata files. The manifest lists45
run files before itself and all6 build files; its own bytes are counted in the
final total and its hash is bound by the machine result.

External run:
`D:/AI/LIBAPTA/outputs/hs06-measured-20261027-rc02-01/`.
External build metadata and audit script:
`D:/AI/LIBAPTA/outputs/hs06-build-20261027-rc02-01/`.
Independent receipt audit:run `audit.json`; artifact manifest:run
`artifact-manifest.json`. Both are exclusive new files; all earlier evidence
remains unchanged. The receipt audit script only reads evidence and public math;
it does not reexecute a candidate.

Registration verifier was executed before build:35 cases,2550 short density
scalars,60000 full reference rows and395 bindings pass. Its historical
`candidate=NOT_RUN` describes registration verification, not this result.
Implementation tests, lifetime doctests and old-component/default controls are
the existing hash-bound evidence; they were not rerun for this measurement-only
checkpoint. The absent C context oracle remains IGNORED/NOT_RUN.

## Handoff

RC-02 closes the registered synthetic host bridge experiment. PF04 musical
experiment, full-size neural graph, independent musical transfer, exact-board
budgets and physical5s/60s/50ms remain open. VERSION1.0.1, default/core/wire,
consumer pins/providers, frozen protocols/assets/old verdicts are unchanged.

Next: **RC-03**, prepare the complete owner/lifetime/resource decision and bounded
target-probe protocol from the [continuation plan](PRODUCT-CONTINUATION-PLAN-20261027.md).
Record rights/provenance evidence or exact missing inputs and keep unavailable
hardware explicit. No further small synthetic stage, full-model port, model
change or product activation follows automatically from HS06 PASS.
