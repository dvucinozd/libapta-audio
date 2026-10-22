# Multi-beat timing implementation and synthetic preflight

Date: 2026-10-22. Registration was committed and pushed at
`2c526efb30b44a72ea5333975690ac45d96789c5` before candidate implementation or
corpus scoring. The [protocol](MULTIBEAT-TIMING-PROTOCOL.json) remains unchanged,
SHA-256 `66ab4e3a5fa05dd5b4eb120609ecfa79d900e8876a131005e147275e02b67bd9`.

`tools/apta_1_1_multibeat_timing.py` implements the one registered host-only
four-neighbor extension with exact rational fitting, existing millibpm/Q32
publication, fixed support/residual/count guards and original global ordinals.
Its candidate function receives only event coordinates, source geometry and
explicit PCM gaps. Reference labels and baseline scores are used only by the
separate runner after candidate reconstruction. Meter remains tied to the
original supported downbeat ordinal on this reconstructed line.

The runner requires a clean exact checkout, refuses an existing output, binds
all tracked source files and previous input/event/score files before execution,
and freezes those bindings before candidate scoring. It reproduces all old
OLS tuples and old coarse/OLS verdicts, scores all 600 unchanged windows with
the frozen scorer, and checks the common diagnostic agrees. Every output detail
is hashed; input/source hashes and Git state are rechecked after the run.
The run reports all fixed vetoes, support losses, both original regressions,
resources and the exact implementation revision. Raw IDs and per-source records
remain outside Git. It never reads audio or runs a model.

Synthetic verification passes **18 new tests**, including positive constant
timing, quantized non-integer periods, source shifts, context clipping, exact
same-grid meter ordinals, missing/extra beats, known gaps in/outside the requested
window, half/double metrical ambiguity, unsupported tempos, insufficient support,
out-of-source anchors, count changes at the window end, partial coverage,
capacity/order/ordinal rejection, lost-support regressions, and a single-break
veto despite aggregate gain. Ten unchanged representation tests, ten unchanged
neural-reference tests and six unchanged annotation/scorer tests also pass.

Commands, from the repository root in WSL:

```sh
python3 tests/unit/apta_1_1_multibeat_timing.py -v
python3 tests/unit/apta_1_1_event_representation.py -v
python3 tests/unit/apta_1_1_neural_reference.py -v
python3 tests/unit/apta_1_1_native_annotated_development.py -v
```

The lightweight new suite is registered in `rust/check.py`. This change is a
Python host experiment; Rust core, native examples/validators, frozen scorers,
default DSP behavior and actual consumer code are byte-identical. No new Rust/C,
sanitizer or physical P4 qualification run is claimed. No native grid is
constructed from the new candidate before it passes this musical timing gate;
the previous native/consumer audit remains the contract baseline.

Commit and push this preflight before the single fixed saved-event run. The
protocol's resource caps are 120 s / 256 MiB on the host. A failed development
veto rejects this exact candidate and stops further corpus work without rescue.
Synthetic correctness alone does not establish musical accuracy or adoption.
