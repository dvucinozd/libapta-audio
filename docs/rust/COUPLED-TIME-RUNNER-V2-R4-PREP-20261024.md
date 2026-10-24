# R4 generated campaign preparation

Date: 2026-10-24. **Driver preparation; full R4 measurement NOT_RUN at this checkpoint.**
Parent: `2faeabd3bd52cb397991ebc7700c33a2db2f21c9`.
Authority: [registered v2 protocol](COUPLED-TIME-RUNNER-V2-PROTOCOL.md), SHA-256
`9c252c0734c43d3856146b2915648e6273d38060edf635fce717298131a2370e`.
The [preparation receipt](COUPLED-TIME-RUNNER-V2-R4-PREP-20261024.json) binds code,
driver tests, current bootstrap and logs. Commit/push this preparation before
measuring the full generated cohort. CT-05 remains closed and unchanged.

## Generated-only driver

The [campaign driver](../../tests/unit/apta_coupled_time_v2_preflight.py) accepts
only a new output destination and a new generated source-fixture destination.
There are no audio, label, event, manifest, cohort, parameter or resource override
arguments. It requires the clean actual repair HEAD to match the pushed branch,
matching preparation component hashes and the retained canonical interpreter/
NumPy/scipy/mir_eval versions. It rechecks actual code/source after the run.

The source fixture is a tracked-only Git archive of that exact repair commit.
Extraction accepts regular files/directories only, rejects traversal/symlinks/
hardlinks/duplicate members and preserves existing destinations. Every extracted
blob's bytes and Git blob digest match the repair tree before and after creating
a disposable clean Git repository. Force-add preserves files that are tracked
in the repair commit even if its ignore rules match them. Exact tracked set,
file count (at least1024), byte count, archive/inventory hashes and both source
revisions are recorded. The fixture stays Windows-mounted; generated observations
and output stay Linux-side. Classification uses actual mount metadata.

The fixed100-source formulas have four families selected by index modulo4:

- Constant:20+(17*index modulo129) events between a9600-frame margin and the
  source end, at the integer period implied by that count.
- Jitter: the same affine sequence plus alternating-160/+160 frames. Index1
  explicitly uses148 events to exercise the registered realistic maximum.
- Ramp: successive intervals differ by20 frames around the same total span;
  all intervals remain positive and the source stays within1440000 frames.
- Boundary: the published CT-02 crossing formula, extended to60 events over
  the source, with the first boundary193319 and five subsequent prefix windows.

Each source has six contiguous windows and a fixed 4-beat downbeat topology.
Other windows end at multiples of240000 frames; source length is1440000. Labels
come from the corresponding fixed latent formulas, never private data. All data,
paths, synthetic source IDs and coordinates remain external. No WAV, raw logits,
checkpoint, inference, model training or corpus acquisition is needed.

Canonical unchanged helpers create the original-event OLS/coarse controls and
reference metrics before candidate construction. The generated protocol clone
binds these fixture-specific baselines and actual complete/incomplete support;
the scientific registration and real cohort's immutable values are not amended.
The generated runtime freeze binds loaded metric/runtime bytes. No candidate is
constructed while this baseline is prepared. Generated labels are physically
withheld before construct and restored only after full independent verification.

## Bounded access accounting and limits

The test-only bootstrap retains detailed access lists for existing focused tests.
The full campaign selects a separate bounded counter mode: it records integer
open/bind counts and rejects any unselected observation/job/candidate access or
worker content binding of the tracked fixture tree. There are no unbounded path
lists in full-campaign worker audits. Positive selected-observation counts are
checked as well:8 opens per worker,1600 across100 construct+100 verify workers.
This instrumentation never enters the production runner or pure numerical API.

Run once, sequentially: all100 construct, all100 independently verify, then all100
evaluate with unchanged canonical scoring. Keep60s/source,1800s/phase,262144KiB/
process and67108864bytes/attempt. Full source/cohort checks remain inside phase
supervision. Preparation time and CLI overhead are separately reported. Output
bytes include all attempt artifacts and driver evidence; generated input files
and the source archive are separate bound inputs, as in the original attempt.

Any source/cohort/hash/runtime/resource failure closes the attempt INVALID,
preserves partials and stops before later phases. No automatic retry, cap increase,
formula tuning, subset score or alternate model follows. A generated scientific
REJECTED verdict may coexist with operational preflight PASS; these gates do
not establish real musical quality, transfer, confidence or P4 feasibility.

## Preparation checks and next action

The receipt records23 repeated v2 regressions and6 driver tests, zero failures/
errors/skips. Driver checks cover fixed100-source geometry, safe archive extraction,
storage classification/tracked inventory, forbidden CLI overrides, preservation
of existing destinations and a two-source/twelve-window canonical three-process
fixture with labels withheld and positive bounded access counters. Full100 phases
have not run here. The unchanged numerical controls retain their R3 hash-bound
evidence, including N3072/window8 and process/descendant enforcement.

Reproduce preparation checks on Linux/WSL:

```text
python3 tests/unit/apta_1_1_coupled_time_v2_runner.py -v
experiment-python tests/unit/apta_1_1_coupled_time_v2_preflight.py -v
```

The combined runner includes the five nonmetric driver tests. The canonical
driver fixture uses the retained experiment environment separately. C/Rust/P4
matrices are not rerun for this generated host tooling.

After this checkpoint is committed/pushed and clean, use the canonical interpreter:

```text
experiment-python tests/unit/apta_coupled_time_v2_preflight.py --output NEW_LINUX_RUN --source-fixture NEW_WINDOWS_SOURCE
```

Publish exact full-cohort/resource/identity aggregates and a component-bound R4
receipt only after every required operational gate passes. No real authorization
is created by preparation or by a generated preflight PASS. Independent transfer,
confidence, full neural-pipeline/P4 resources, native consumer, fresh acceptance
and release remain separate. VERSION and production behavior are unchanged.

Preparation scripts/logs are retained outside Git under
`D:/AI/LIBAPTA/outputs/coupled-time-v2-r4-prep-20261024/`.
