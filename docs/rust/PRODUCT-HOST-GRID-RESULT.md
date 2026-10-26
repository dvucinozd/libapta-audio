# HS-02 synthetic path-to-events/grid result

Date: 2026-10-26. Verdict: **HS02_SYNTHETIC_HOST_PASS_ONLY**.
Clean implementation commit: `1196e5459406d4179793d53a535d516577f28436`.
[Protocol](PRODUCT-HOST-GRID-PROTOCOL.md), [implementation](PRODUCT-HOST-GRID-IMPLEMENTATION.md),
[aggregate machine receipt](PRODUCT-HOST-GRID-RESULT.json).

The registered six-case run completed once, in fixed order, with 55 validated
observations: 50 public fixtures, four full postprocessor cases and pending cancel.
All expected statuses/fields and event/summary hashes match the frozen oracles.
The 50-case denominator includes valid grids/events and expected unsupported,
invalid and no-evidence outcomes; it is not 50 successful music analyses.

## Measured host scope

| Case | Observations | Step loop ms | Worker wall s | Child CPU s | Child peak RSS KiB | Owned generated payload B |
|---|---:|---:|---:|---:|---:|---:|
| fixtures | 50 | separate | 0.109511 | 0.002985 | 18552 | per fixture |
| full3-44100 | 1 | 0.606523 | 0.099807 | 0.003188 | 18600 | 444528 |
| full4-44100 | 1 | 0.692344 | 0.100947 | 0.003524 | 18568 | 444558 |
| full3-48000 | 1 | 0.568038 | 0.097754 | 0.003170 | 18556 | 444528 |
| full4-48000 | 1 | 0.607430 | 0.103452 | 0.003196 | 18468 | 444558 |
| cancel | 1 | separate | 0.108020 | 0.003070 | 18492 | 444558 |

Full cases represent 600 seconds at 44100/48000 Hz. Meter3 uses 1200 events/120 BPM
with first beat number2 and restored first peak row5. Meter4 uses 3000 events/
300 BPM with first peak0 and excludes the EOF point. Paths are artificial legal
state sequences; full-case HMM inference, audio decoding, frontend and neural
inference were not executed. Step time is only validation/correction/grid work.
Worker wall includes process and independent input-hash verification overhead.

Postprocessor scratch is **24576 bytes (24KiB)**. Full generated probability/path/
event payload was 444528 B (m3) or 444558 B (m4), below the 444576 B registered ledger.
These exclude frozen executable/map/library owners, allocator controls, native/
wire scaffolding and stack. Host Rust type value sizes: Session 432 B, Summary 96 B,
Event 8 B; these are type sizes, not stack high-water or P4 layout measurements.
Largest observed 4096-unit step among full cases: 0.071417ms.

The cancel case advanced with quota1 to CheckResiduals, then timed an already
pending request: **0.000112ms** request-to-return; scratch drop separately
**0.000916ms**. Cancelled output remained hidden and clearing the flag did
not resume work. This is pending-request service on host, not asynchronous
interruption or the P4 product cancellation target. HS-01 asynchronous results
remain separate. Single host observations do not establish worst-case timing.

Child rusage is a conservative process high-water including pre-exec fork
activity; independent sampled per-process/process-group envelopes are retained.
Neither is whole-device/internal RAM/PSRAM evidence. No P4 budget or 5s/60s/50ms
product timing target is qualified by these figures.

## Integrity and checks

Registered caps stayed unchanged: CPU 10s, wall 30s, per-process RSS 64MiB, AS 128MiB,
aggregate output 1MiB. All cases stayed inside them. Four intentional wall/RSS/
output/AS violations were refused. The allocating RSS child reached 53468KiB
against 16MiB, separately verified from the supervisor baseline.

The audit rechecked 170 parent bindings and loaded-runtime identities for every
worker, all six code components, exact binary/source/protocol/fixture/asset hashes,
child log hashes, actual inherited limits and complete case order. 58 retained
files total 322896 B including final aggregation, below 1048576 B. This is resource
supervision and API/input isolation, not an OS filesystem sandbox.

Executable SHA-256: `28a37cde4456347f27ec9ade660f7e26dccd3ba82899181aea7c409eb1a5359c`.
Build: Rust/Cargo 1.95.0, release `experimental-compact-grid`; Python 3.12.3, WSL.
Full identities and limits are recorded in JSON. External raw evidence:
`D:/AI/LIBAPTA/outputs/hs02-measured-20261026/`; separate manifest audit:
`D:/AI/LIBAPTA/outputs/hs02-measured-20261026-audit.json`.

Preflight: 8 new HS-02 tests pass in both debug/release, 250 public fixture/quota
combinations per profile, cancellation/reuse across nine phases, ten positive
native/wire fixtures and two small generated HMM compositions. Finalized bytes
survive source/scratch mutation. Prior HS-01 controls 13 per profile, product-grid
controls 12 and default-context 5 pass. One existing C lifetime comparison requires
APTA_C_CONTEXT_LIFETIME_ORACLE and remains ignored/**NOT_RUN**. fmt, focused
Clippy(-D warnings), release build and Python compile pass. No broader C/P4
matrix ran. No fixture, tolerance, algorithm or resource cap was changed.

## Remaining gates and handoff

HS-02 completes only the registered synthetic host postprocessor task. Existing
default/core/consumer/wire/HS-01 sources remain unchanged, VERSION 1.0.1 retained.
Confidence remains uncalibrated/unavailable, key absent. Artificial wire-overview
columns are test scaffolding, not analyzed waveform evidence. No model weights,
real logits, music, labels, formal holdouts, training, device, provider or release
were used/activated. No old rejection is reclassified and PF-04 remains NOT_RUN.

Next product prerequisites remain affirmative neural code/checkpoint rights and
checkpoint training/recording independence, plus actual PF-01 budgets when the
JC1060P470C_I_W_Y becomes available. Silicon 3.2 stays an assumption, not an
observed revision. A full frontend/neural/progress/audio-format/native consumer
pipeline still needs separately registered implementation/evaluation and physical
qualification. Further isolated component work requires its own scoped protocol;
this host result does not authorize model execution or a large product port.
