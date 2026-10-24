# Runtime stabilization checkpoint before a new R4 campaign

Date: 2026-10-24. **Generated preparation PASS; full new R4 NOT_RUN.**
Implementation parent: `cdcc60741cddddc56de26eab23de520bbdfc877c`.
The [component-bound preparation receipt](COUPLED-TIME-RUNNER-V2-RUNTIME-PREP-20261024.json)
records the exact prepared implementation, runtime image and fresh generated checks.
The prior [R4 failure](COUPLED-TIME-RUNNER-V2-R4-20261024.md) remains INVALID;
CT-05 remains closed. No old result, freeze, protocol, threshold or cap is amended.

## Hypothesis and implementation

The failed R4 parent correctly rejected an Ubuntu replacement of a bound system
library. A virtualenv alone retains dependencies on Ubuntu's shared libraries.
The operational hypothesis here is that a separately copied runtime, mounted
read-only for the whole generated command, prevents that package replacement
from changing the bytes used by Python or its worker processes. All original
runtime/source checks remain active; a changed identity still vetoes completion.

The [test-only runtime helper](../../tests/unit/apta_coupled_time_v2_runtime.py)
copies the retained environment, complete standard-library and package trees,
shared-library directory, interpreter, Git/helpers/templates and required Python/
loader/alternative configuration. It records every regular-file digest and
symlink target, compares the copied inventory with the initial host inventory,
and rechecks the host inventory after copying. Creation refuses overlapping,
unsafe or existing destinations and preserves an INVALID copy on failure.

`bubblewrap` supplies unprivileged read-only bind mounts for the copied runtime
roots and the snapshot itself. The generated source/observation/output paths
retain their original filesystem classes. Execution verifies snapshot identities
before and after the child. The in-namespace probe imports the canonical metric
stack, verifies the actual RO mount flags, and requires every loaded non-project
runtime file to match the copied inventory under a frozen root. Python and metric
package versions must also match the retained experiment's versions.

This protects runtime bytes for the registered generated workflow. It is not a
complete OS image or a private-data access sandbox. Security-update timers,
package configuration and host runtime files are not changed or disabled. New
uncovered dependencies or invalid mount/binding identities fail closed.

The [generated campaign driver](../../tests/unit/apta_coupled_time_v2_preflight.py)
now requires an explicit runtime snapshot, validates actual mounted execution,
and binds this new preparation receipt and the snapshot digest. A host argument
cannot spoof the RO mounts. Component bindings now include both new runtime
helper/test files (25 code/test components in total). The old preparation receipt
cannot authorize this implementation. No production override, scorer, numerical
constructor/verifier or real execution token is added.

## Fresh verification and cost

All 74 unit-test executions pass with zero errors/failures/skips inside the copied
runtime: seven runtime regressions, 37 CT01/CT02 numerical/oracle/verifier controls,
seven driver checks, and 23 v2 regressions using canonical event metrics. The
combined runner includes the nonmetric driver contracts and new runtime tests;
the canonical groups still use the retained experiment interpreter separately.
C/Rust/native/P4 matrices are outside this host-only change and were not rerun.

The tests exercise an atomic host-file replacement while an actual namespace
process is waiting: it still reads the copied value and gets EROFS when trying
to write to that runtime mount. Other checks reject tampered copy bytes/links/
inventories and changes during copying; child failure releases the namespace
and leaves host files writable. A generated bound-runtime-file mutation after
a worker makes the unchanged v2 parent retain INVALID without cohort completion,
success envelope or evaluation. This does not modify a real system library.

Canonical generated construct/verify/evaluate, label withholding, baseline
reproduction, independent record validation, mutation and resource/orphan probes
pass. Their small generated fixtures do not establish full100-source throughput
or musical quality. The separate actual-host probe is rejected because the
required RO mounts are absent. All 994 retained artifacts from the failed R4
match their saved hashes; no subset was resumed, verified or scored.

The prepared snapshot contains 45154 regular files / 2682779859 bytes (about
2.50 GiB) and 13 mounted roots. Coherent creation took 45.220 s. The canonical
probe binds 1627 loaded runtime files. The receipt records per-check CLI wall
times including full snapshot validation; those costs are not candidate-phase
measurements. The image is a separately bound runtime input, not attempt output.
The 64 MiB attempt-output cap and every source/phase/RSS cap remain unchanged.

Python 3.12.3, NumPy 1.26.4, scipy 1.17.1 and mir_eval 0.8.2 match the prior version
identities. Among common non-project runtime bindings from the failed R4, only
`libz.so.1.3` has changed. Both digests are retained in the receipt. This is a new
runtime identity with fresh generated compatibility evidence; it does not rewrite
or requalify the historical real neural-runtime freeze.

A development-only first snapshot missed the stdlib `sitecustomize.py` symlink
endpoint in Python's system configuration directory. Its probe rejected that
uncovered file. The current snapshot includes the endpoint and passes. Both
preparation artifacts remain external; no new R4 cohort was dispatched while
this runtime preparation was being corrected.

An additional actual namespace storage probe confirms Windows-mounted source
and Linux observation/output storage classes. Its retained `storage-validation.json`
SHA-256 is `d0632e5293db3d409553698034609fd8c5c6444eb56b5974df77f88ee05207c2`;
it binds this exact snapshot and preparation receipt. No cohort was run by that probe.

## Reproduce and next action

Use Linux/WSL with the retained canonical interpreter and `bubblewrap` installed.
`experiment-python` below denotes that interpreter; use a new Linux snapshot
destination for creation. No root permission is required.

```text
experiment-python tests/unit/apta_coupled_time_v2_runtime.py prepare --snapshot NEW_LINUX_SNAPSHOT
experiment-python tests/unit/apta_coupled_time_v2_runtime.py probe --snapshot PREPARED_SNAPSHOT
experiment-python tests/unit/apta_coupled_time_v2_runtime.py check --snapshot PREPARED_SNAPSHOT --suite runtime
experiment-python tests/unit/apta_coupled_time_v2_runtime.py check --snapshot PREPARED_SNAPSHOT --suite numerical
experiment-python tests/unit/apta_coupled_time_v2_runtime.py check --snapshot PREPARED_SNAPSHOT --suite driver
experiment-python tests/unit/apta_coupled_time_v2_runtime.py check --snapshot PREPARED_SNAPSHOT --suite v2
```

Creating another snapshot changes its receipt identity; it needs its own bound
preparation evidence. Use the exact retained qualified snapshot for the next
measurement. Commit/push this checkpoint and verify clean matching local/remote
SHA before the separately recorded new generated R4 campaign. Then use a new
Linux output and Windows-mounted generated source-fixture destination:

```text
experiment-python tests/unit/apta_coupled_time_v2_runtime.py campaign --snapshot PREPARED_SNAPSHOT --output NEW_LINUX_RUN --source-fixture NEW_WINDOWS_SOURCE
```

The helper exposes fixed generated checks and the generated-only driver; there
are no audio/labels/manifest/count/limit or arbitrary-command CLI arguments. Full
100-source construct, independent verify and canonical evaluation must all pass
under the original caps before a new v2 preflight is possible. Any failure closes
that distinct attempt INVALID with partials preserved and no automatic retry.
This checkpoint has not launched that campaign or created a preflight token.

Any later real attempt still requires the separate R5 scope/authorization and
explicit treatment of historical runtime identities. Model independence,
transfer, confidence, full neural-pipeline resources, native consumer, physical
P4, fresh acceptance and release remain open. No production promotion follows.

Qualified snapshot and its private inventory are retained in the WSL
`coupled-time-v2-runtime-20261024-v2` directory. Verification driver, full probe,
logs, source bindings and private identity evidence are retained outside Git in
`D:/AI/LIBAPTA/outputs/coupled-time-v2-runtime-prep-20261024/`.
