# P1 generated context-anchor projector and independent oracle

Date: 2026-10-24. **GENERATED IMPLEMENTATION PASS; no musical or native activation claim.**
Source baseline: `03229c066d3de733aecc581c6bc925d8bcf8f5c0`, `rust-rewrite`.
Authority: unchanged [P0 protocol](CONTEXT-ANCHOR-PROJECTION-PROTOCOL.md) and
[machine registration](CONTEXT-ANCHOR-PROJECTION-PROTOCOL.json), SHA-256
`56e5b498d0baa7d83b0913fe7dfb7ff14f542d479c94a2e59818753bfe424489`.
The [P1 receipt](CONTEXT-ANCHOR-PROJECTION-P1-20261024.json) binds implementation,
test, authority and external evidence hashes. P0's implementation-disabled fields
remain the immutable registration-time snapshot; this receipt records P1 only.

## Implemented boundary

`tools/apta_context_projection.py` exposes only the registered pure
`project_window(beat_frames, downbeat_ordinals, total_frames, first_frame, end_frame)`.
It validates all input before abstention, fits exact rational OLS once, preserves
the rounded source-valid anchor, and publishes precisely the half-open owned Q32
lattice and original-base model ordinals. The only import is `fractions`.
Negative source anchors and unsupported tempos abstain without repair.
Meter uses the first eligible original bar triple, then its left/middle/right
owned witness, retaining original witness indices and zero confidence.
No caller, CLI, scorer, constructor, native producer or consumer is changed.

The independent test-only oracle assembles normal equations and eliminates a
column, enumerates bounded lattice indices instead of using ceiling arithmetic,
and checks meter residuals by integer cross-products. It imports no projector
or registration helper. Independent windows remain independent; model ordinals
do not establish a bijection with raw observations or continuous source coverage.

## Executed verification

WSL Ubuntu, Python 3.12.3, jsonschema 4.10.3:

```text
/usr/bin/python3 tests/unit/apta_1_1_context_projection.py -v
/usr/bin/python3 tests/unit/apta_1_1_context_projection_registration.py -v
```

The final run passes **14 P1 + 9 P0 tests**, zero failures/errors/skips.
All **446 generated call comparisons** match the independent oracle exactly,
including schema and exact integer runtime types. These include repeated
independence controls, not 446 musical tracks. Coverage includes:

- Five frozen public projection examples and six lattice-only boundary examples.
- A fixed 405-case public matrix of constant, alternating and ramp events,
  three window geometries, three event counts, and absent/3/4 meter evidence.
- Positive context and negative source anchors, inclusive tempo limits, empty
  and insufficient inputs, exact half ties, fractional carries and Q32 endpoints.
- Maximum 3072-event inputs and eight independent windows, invalid types/order,
  no input/output aliasing, lossless JSON controls and corrupted result rejection.
- Import and I/O isolation in a fresh interpreter; only `fractions` is allowed.
- Middle/right same-triple meter fallback and explicit `NO_OWNED_WITNESS` without
  later-triple rescue. The public residual construction has zero zeroth/first
  moments, so its actual OLS fit is exact; no fabricated fit is injected.

On the old accepted synthetic intersection, **246 grid results and 119 owned
meter results** preserve old tuples and nominal points exactly. The unchanged
legacy projector and point expander are called only with newly generated public
integers; no scoring function or saved-source evaluation runs. Observed legacy
native-unowned meter conflicts: **0 in these fixtures**, reported separately
from owned comparisons; this does not establish absence in other inputs.

Statuses across the 446 calls: accepted325, insufficient50, unsupported-tempo70,
source-anchor-out-of-domain1. Meter states: owned152, no-eligible-bars172,
no-owned-witness1, not-evaluated121. High-level `NO_OWNED_POINTS` is not observed;
empty ownership is tested only in the explicitly lattice-only fixtures. No
high-level fit is altered to force defensive-state coverage. Internal arbitrary
lattice tests also reject capacity/domain/native-width overflow without partial
results; they do not claim such overflow is reachable from a valid constrained fit.

Final supervised P1 wall time: **2.030268s**; P0: **0.672575s**.
Longest checked generated call including oracle/schema validation: **0.068695s**.
Largest observed process RSS/HWM: **32540 KiB**. The Linux supervisor enforces
60s for each suite (stricter than the registered 1800s suite cap), 262144 KiB per
process and 67108864 external bytes, including the isolated-interpreter child.
The receipt records actual external byte counts and hashes. These are host
arithmetic measurements, not embedded latency/allocation budgets.

Before and after execution, all14 document bindings,17 reviewed sources and37
frozen C3 component hashes match P0. The P0 receipt and registration test also
retain their original hashes. Tests ran on the named P0 baseline plus the three
SHA-bound uncommitted P1 files; this is not a clean-source corpus campaign.
External final logs/resources are under
`D:/AI/LIBAPTA/outputs/context-anchor-projection-p1-20261024/final/`.

## Next boundary

**P2: prepare C/Rust generic-builder/GGRD/MTRD interoperability and consumer-policy
probes for generated records.** Verify exact owned point/whole-frame/ordinal
conversion and existing rejection policies before proposing any producer change.
Keep the consumer checkout/provider/pin and history producer unchanged. P1 does
not run those probes or the full C/Rust/P4 matrix; the earlier five P0 native
coordinate controls remain historical evidence, not newly rerun results.

P3 still needs its own matched evaluation registration. No old candidate, raw
event, score, support denominator or 16-gate verdict is changed. Scientific C3
**REJECTED14/16** remains frozen. No corpus, saved source coordinates/labels,
training/inference, real authorization, confidence qualification, current
consumer build, physical P4, acceptance or release is performed. Real-runtime
metadata stays `NOT_AUDITED`, independence `UNRESOLVED`, and `VERSION` is `1.0.1`.
