# Reference desktop tools

The desktop tools are informative integrations built on the same stable
`apta::core` implementation. They are not separate DSP implementations and are
not stable exported package components.

## Rust target preparation tools — 2026-10-31

The board is available on COM3; its recorded P4 v1.3 identity is incompatible
with the prepared native images. See the
[board-arrival handoff](../docs/rust/BOARD-ARRIVAL-HANDOFF.md) and
[Rust documentation index](../docs/rust/README.md) before using experimental tools.

- [`apta_target_probe_telemetry.py`](apta_target_probe_telemetry.py) validates an
  explicit manifest and dedicated bounded JSONL log, retaining unknown metrics
  as null. Parser validity alone does not establish physical qualification.
- [`apta_target_probe_package.py`](apta_target_probe_package.py) builds and
  verifies the five separate `BUILD_ONLY_LINKCHECK` images. `build` uses the
  pinned Linux/WSL toolchain, locked offline dependencies and new external
  output/build directories. `verify --package PATH` checks a retained package.
  Neither command flashes, captures a serial port or runs trials. Capture
  templates still require actual unit/schedule/arming data and are not ingest
  manifests. `execution_ready` remains false.

The [frozen package result](../docs/rust/PRODUCT-TARGET-ADAPTER-PACKAGE.md) binds
the clean build source and external artifact hashes. Preserve those artifacts;
rebuilds or later runtime firmware need their own source/identity records. The
standalone `rust/probes/resource-core` and `rust/probes/target-adapter` crates
are outside the default workspace and need their own explicit manifests for
tests/builds. The [legacy probe contract](../docs/rust/PRODUCT-LEGACY-IDF-PROBE-PROTOCOL.md)
is registered; its tools are planned and not yet implemented. Next is a separate
legacy backend implementation/build-only package, not execution of these native images. Telemetry V1 requires revision >=302; it must not be
silently relaxed or given a fabricated identity for the legacy unit.

## Existing desktop and experimental workflows

Build them with the native adapters:

```bash
cmake -S . -B build-tools \
  -DAPTA_BUILD_DESKTOP_ADAPTERS=ON \
  -DAPTA_BUILD_TOOLS=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-tools --parallel
```

The current tool targets include:

- `apta-analyze` — analyse supported input through the reference decoder path;
- `apta-inspect` — inspect result/container metadata and sections;
- `apta-validate` — validate canonical container structure and semantics;
- `apta-version` — print package, API, specification and container versions;
- workspace and cost probes used for retained implementation evidence.

Run each executable with `--help` for the exact options compiled by the current
source revision. Platform adapters own file and decoder integration; the
portable core itself does not open files or decode codecs.

For byte-reproducible `apta-analyze` output, set `SOURCE_DATE_EPOCH` to a
non-negative Unix timestamp. It becomes the META creation time; malformed or
empty values are rejected.

The opt-in Rust history-development runner is
`apta_1_1_native_history_development.py`. It freezes explicitly spent development
inputs and compares provisional native-history coverage without reading labels
or opening formal holdouts. Build `apta-history-diagnostic` with runtime feature
`history-diagnostics`; see [the recorded experiment](../docs/rust/S6-RECORDED-DEVELOPMENT.md).
It cannot replace the frozen FINAL-only acceptance workflow. Keep its private
input manifest, audio and per-source artifacts outside Git.

The separate runtime feature `onset-diagnostics` enables `--mode=novelty`.
Use the development runner's explicit `run --candidate novelty` to retain and
compare all three modes on the same frozen spent inputs. The default remains
the original coarse/sparse comparison. This does not enable formal acceptance;
see [the onset experiment](../docs/rust/S6-NOVELTY-EXPERIMENT.md).

For actual native peak events, build runtime `onset-trace-diagnostics` and use
`run --candidate novelty --trace`. This retains three baseline modes and adds
one trace mode, verifies complete source-local capture and unchanged native
evidence. `apta_1_1_native_event_lattice.py` then checks exact coverage/label
hashes and cohort IDs and runs the pre-registered host-only event-support
selector. It imports the frozen evaluator's label parsing and tolerances,
reports unconditional window accuracy, unsupported outputs and paired breaks,
and cannot claim FINAL/track-level acceptance. See
[the event-lattice experiment](../docs/rust/S6-EVENT-LATTICE-EXPERIMENT.md).

The canonical ASAP preparer supports `prepare --development-only` to leave
holdout performance MIDI/audio unopened. Use
`apta_1_1_native_annotated_development.py` for the unchanged native history
against those source-frame dense references. It refuses undeclared/holdout
preparations before label/audio access, retains incomplete annotation context
and reports all represented beats without a new selector or FINAL acceptance.
See [the ASAP native baseline](../docs/rust/ASAP-NATIVE-DEVELOPMENT.md).

The canonical Ballroom preparer also supports `prepare --development-only`.
For this mode, supply checked distribution archives and a source directory with
annotation metadata and an empty `BallroomData` directory. Inventory uses tar
headers; only the forty frozen development WAV members are extracted and
converted. The compressed archive contains holdout bytes, but no holdout WAV
is materialized, decoded or analyzed. Original durations are preserved. The
same annotated-development runner accepts its explicit format/provenance and
analyzes at most thirty seconds with unchanged dense scoring. See
[the Ballroom native baseline](../docs/rust/BALLROOM-NATIVE-DEVELOPMENT.md).

The successor [dense temporal protocol](../docs/rust/S6-TEMPORAL-MODEL-EXPERIMENT.md)
is registered with [frozen parameters](../docs/rust/S6-TEMPORAL-MODEL-PROTOCOL.json).
Its native dense capture is implemented as stage A; a separate stage B host
model is implemented. Its ordered development evaluation now rejects the model
on ASAP; there is no native temporal selector.
Build runtime `temporal-trace-diagnostics`, then use the explicit
`run --candidate novelty --dense` on the history runner to retain an additional
`novelty-dense` output. Existing invocations keep their original report shape.
`validate_dense_trace` checks complete exact native f32 values, source-bin
coordinates, startup context and final partial-bin geometry. Dense capture is
independent of the sparse event count.

Use `apta_1_1_native_dense_development.py` for the registered development-only
stage: supply `--prepared`, `--analyzer`, `--baseline` (retained annotated report),
`--expected-baseline-sha256`, `--output`, `--source-revision` and
`--candidate-inventory-sha256`. It refuses holdout/undeclared preparation before
native execution, reuses the canonical hash-bound runner, compares every
ordinary file with its retained baseline, and verifies dense-stripped novelty
identity. It performs no selector or label scoring. Keep raw traces private;
see [the capture checkpoint](../docs/rust/S6-DENSE-TRACE-CHECKPOINT.md).

`apta_1_1_native_temporal_model.py` is the stage B label-blind recurrence module,
with no corpus-analysis CLI. It consumes validated native dense observations
and exact source geometry, keeps two paths per tempo cell, and checks causal
context and shared source resource caps. Run its synthetic suite with
`PYTHONWARNINGS=error python3 tests/unit/apta_1_1_native_temporal_model.py -v`.
An optional new `APTA_TEMPORAL_SYNTHETIC_REPORT` path retains synthetic-only
coordinates/results without overwriting an earlier report. See
[the host checkpoint](../docs/rust/S6-TEMPORAL-HOST-CHECKPOINT.md) for its observed
boundary alias. The subsequent
[ordered evaluation](../docs/rust/S6-TEMPORAL-DEVELOPMENT.md) retains the failed
ASAP result and leaves Ballroom candidate evaluation unrun.

`apta_1_1_native_temporal_development.py` implements that single ordered stage C
attempt over retained traces. Supply `--evidence-root`, `--stage-b-report`,
`--source-inventory`, full `--source-revision` and a new `--output` directory.
It binds the exact model/protocol/prerequisite/reference/native-output hashes
before any selection, reuses the unchanged annotated scorer, reports every
window and paired regression, and stops before Ballroom on any ASAP failure.
It has no DSP knobs, corpus-order switch, fallback or meter borrowing. Existing
outputs are never overwritten. The retained rejection is not permission to
retry or tune this frozen attempt. Synthetic wrapper verification is
`PYTHONWARNINGS=error python3 tests/unit/apta_1_1_native_temporal_development.py -v`.
