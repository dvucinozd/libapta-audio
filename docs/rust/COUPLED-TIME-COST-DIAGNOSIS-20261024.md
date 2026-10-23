# Coupled-time verification cost diagnosis

Date: 2026-10-24. **READ_ONLY_DIAGNOSIS_COMPLETE; no candidate replay or scoring.**
Audit source: `67a509c284ab31a6734458f75056f05db845ba15`, clean before/after audit.
Measured attempt source remains `362b6fa54ca3903794a34729d66bb5b7b202a446`.
The [CT-05 attempt](COUPLED-TIME-CT05-20261023.md) remains INVALID with incomplete
verification; its authorization is consumed. The
[aggregate diagnosis](COUPLED-TIME-COST-DIAGNOSIS-20261024.json) binds the script,
original public result and exact counts/timings. The
[repair plan](COUPLED-TIME-RUNNER-REPAIR-PLAN-20261024.md) is future work.

## Finding

The runner repeatedly performs whole-cohort provenance work inside individual
source workers. This is a demonstrated scaling problem. A read-only microprofile
shows that filesystem metadata/path resolution is expensive on the current
WSL/Windows-mounted source tree. The numerical verifier was not profiled or
replayed; this audit does not establish its isolated CPU cost.

The frozen construction completion binds **1,524 files**: 1,022 tracked source
files, 301 attempt artifacts, 200 event/capture files and the label-free manifest.
In [source_worker](../../tools/apta_1_1_coupled_time.py), each verify worker:

1. Calls inputs(), which opens and validates every source's event/capture pair.
2. Calls frozen(), which rechecks all 1,524 construction bindings, checks the Git
   source, calls inputs() again, then checks every candidate hash.
3. Runs the independent numerical verifier for the selected source.
4. Rechecks runtime and its own bindings; the parent rechecks those bindings again.

Each _read() binds before and after parsing. For a complete 100-source verify
phase, these named call paths alone require 201 full input loads, 80,400 input
file bind calls, 101 whole-frozen-binding rechecks (153,924 file checks) and
10,100 candidate hash checks. These are planned static counts, not claims that
the interrupted attempt completed them. Settings, runtime, finish/parent rechecks
and the interrupted source are additional work. The 56 completed verify workers
alone execute 112 full input loads and 44,800 input-file bind calls.

Construction also performs 101 full input loads, accounting for 40,400 input-file
bind calls. A fresh worker per source is intentional isolation; full-cohort file
validation inside every worker is the avoidable duplication.

## Retained timings and one read-only microprofile

| Historical CT-05 quantity | Construct | Verify |
|---|---:|---:|
| Completed reports | 100 | 56 |
| Supervised source mean seconds | 15.408 | 30.377 |
| Supervised source median seconds | 14.287 | 29.858 |
| Sum of completed supervised source seconds | 1540.825 | 1701.112 |
| Mean worker inner span seconds | 5.036 | 26.896 |
| Bindings in every completed worker report | 365 | 468 |
| Whole phase seconds | 1699.345 | 1800.045, INVALID |

These sample sets differ (100 construction sources versus 56 verified sources).
They are operational spans, not a matched algorithm benchmark. The worker's
inner timer includes runtime/provenance/finish work; it must never be presented
as pure solver/replay latency. Phase-minus-completed-worker time also includes
startup, parent work and, for verify, the interrupted worker.

The separate audit calls only neutral io.recheck() once on the retained freeze
paths, using their **current** bytes. It takes **19.615 s** for 1,524 files.
Current MIGRATION.md legitimately differs from the old execution revision; that
single expected documentation drift is recorded. The sample is not a claim that
the current checkout equals the old measurement source.

cProfile records 1,524 bind()/resolve()/SHA calls, 4,572 stat calls and 13,066
lstat calls. Exclusive stat/lstat time is about 14.52 s, roughly 74% of this
instrumented pass. SHA wrapper cumulative time is 4.681 s. Cumulative resolve,
stat and SHA times overlap; do not add them. This single current warm-filesystem
sample supports the repeated-provenance explanation but is neither an additive
decomposition of CT-05 nor a prediction that a repair will pass 100 sources.

Do not infer neural-inference speed, P4 feasibility or musical quality from this
metadata profile. Moving the checkout, increasing caps, parallel workers or a
stat-only hash cache are not the proposed fix.

## Preservation and evidence boundary

The audit hashes all 836 retained attempt files before and after; every byte and
the file set are unchanged. It reads saved completion/resource/worker metadata
and hashes label-free provenance bytes. It never imports the constructor,
independent verifier, scorer or mir_eval; it reads no labels or audio and creates
no candidate. No remaining verification or evaluation phase is resumed.

Code, tests, numerical/scoring modules, old protocols/preflight/authorization and
old result documents remain unchanged. Only new diagnosis/plan documents and the
current migration handoff are updated. There is no new test-suite, musical,
independence or product PASS. All original gates and resource caps stay fixed.

External script, private profile and aggregate are retained under
`D:/AI/LIBAPTA/outputs/coupled-time-diagnosis-20261024/`. Source IDs, bound paths,
coordinates and per-source detail remain outside Git. Next is the separately
checkpointed runner repair and generated preflight described in the plan; it
does not itself authorize another real-data attempt.

Private cProfile artifact SHA-256:
`69afcd86d2ccb6690274f86923ec025168ae3cccea11b72aeab39861edbf4c54`.
