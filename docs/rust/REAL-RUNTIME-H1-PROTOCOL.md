# H1: immutable neural runtime and synthetic compatibility

Registered 2026-10-25 from clean, pushed `rust-rewrite` source
`c2941f1c6115ca31b57cc2c948324aea592e1b52`.
[Machine registration](REAL-RUNTIME-H1-PROTOCOL.json).
**H1-R is registration only; preparation and qualification remain NOT_RUN.**
Real-cohort execution remains **NOT_READY** throughout H1.

## Purpose and fixed boundary

The [completed H0 audit](REAL-RUNTIME-H0-20261025.md) establishes one changed
real-decoder system library and incomplete original neural runtime coverage.
The operational hypothesis is that a separately copied, coherently inventoried
runtime can execute the unchanged reference models and canonical synthetic checks
without writable runtime dependencies or unbound loader/cache fallback.
This creates a new runtime identity. It cannot restore the old zlib bytes,
prove old numerical outputs unchanged, or requalify historical musical results.

Retain BeatNet model 1 at `81cedd4beeb7235262db80969a0c9ce9a48a0ed4`, its
registered weights, and Beat This! final0 at
`b95c8ab0c58c2d9fcfd40508ae8dffbc05ac4f5c`. Preserve the unchanged CPU/two-thread,
float32, minimal-postprocessor/no-DBN, recurrent-reset and PF-seed behavior.
No model selection, training, threshold, projection, scorer or native change.
Weights are separately identified model inputs, never classified as programs.

Read only the already selected original comparison-freeze metadata and H0 private
inventory when deriving historical program identities and the exact asset root.
Recheck their registered digests before parsing. Follow no data references.
The asset root is also the mixed experiment root: **never copy or recursively
open that whole directory**. Select only its `venv`, `BeatNet`, `beat_this` and
`madmom` program subtrees, necessary explicitly classified resources, and the
two exact registered weight files. Do not enumerate sibling audio, annotations,
archives, inputs, prepared manifests, old attempts or scoring directories.

## Ordered checkpoints

1. **H1-R (this checkpoint):** commit/push this registration, identities, limits
   and verification requirements. Validate document links, hashes and consistency.
   Do not import the venv/models, hash weights, copy a runtime or run inference.
2. **H1-P:** implement an opt-in runtime builder/validator and a synthetic-only
   worker. Reuse the neutral binding/supervision helpers and canonical neural
   functions. Keep the generated metric-runtime implementation unchanged.
   Finish public fixture/negative tests, then commit/push the implementation.
3. **H1-Q:** from that clean pushed source, record exact launch/source/protocol
   identities; prepare one new external snapshot, freeze its inventories and
   run the registered synthetic workers inside its read-only namespace.
   Independently close out the snapshot, outputs, resources and unchanged inputs.
   Publish aggregate evidence and commit/push before any later scientific step.

These are ordered parts of H1, not permission for a real-data attempt. Public
fixture failures may be repaired before the H1-P freeze, retaining the test logs.
Any measured H1-Q integrity, compatibility or resource failure closes that
attempt INVALID/INCOMPLETE; preserve partial output and stop. No automatic retry,
in-attempt repair, dependency installation, cap increase or alternate-model rescue.

## Runtime construction and import membership

The builder must inventory filenames, types, sizes and symlink text before
reading/copying candidates. Use explicit, disjoint runtime roots; bind the venv
configuration, interpreter link chain and exact interpreter binary. Include
stdlib, Python configuration/sitecustomize endpoints, editable import paths,
loader configuration, ELF dependencies, wheel libraries and required Git runtime
for the unchanged upstream-revision checks. System roots may be selected only
by the registered interpreter/sysconfig/loader requirements; `/`, `/home`,
`/mnt` and the mixed experiment root are forbidden snapshot roots.

Write a private membership manifest with one decision per discovered entry:
program, required runtime resource, separately selected weight, directory,
symlink, or excluded. Programs include `.py`, `.pyc`, ELF extensions/shared
libraries and individually selected executables. Runtime resources must have
explicit path and purpose, not a catch-all allowance for unknown file types.
Never copy audio, labels, events, logits, scoring artifacts or unselected model
files, including package/upstream example or test assets. Excluded directories
must not leave a live-host fallback beneath a mounted synthetic asset tree.
Git provenance access must not cause an unrestricted object/data-tree copy.

H0's 9433 unrecorded caches and 24 shared-library filenames require an explicit
per-entry decision. Include and hash importable `.pyc` caches alongside sources
as new-runtime inputs; disable bytecode writes with `-B`/`PYTHONDONTWRITEBYTECODE`.
Any deliberately excluded cache requires an enforced empty cache-prefix mount
and a test proving it cannot be read. Do not assume that `-B` prevents cache reads.
Unrecorded programs get new as-of identities, never invented historical hashes.
Require unchanged bytes for all selected programs with historical asset bindings;
keep system-library differences and missing historical expectations separate.

Record every symlink's text and canonical endpoint without following directory
links while enumerating. Reject loops, missing endpoints, path traversal,
special files, overlapping input/output roots and endpoints outside the selected
closure. Add necessary safe endpoints before freezing; do not silently resolve
a copied absolute symlink against the live host. Exact selected weights must
match the protocol's SHA-256 both before and after copying/loading.

Require identical before/copy/after inventories, including directory membership,
file bytes, modes and link identities. Cap enumeration/copying incrementally.
Bind the inventory, membership decisions and snapshot descriptor by digest before
the first model import. No snapshot reuse from the generated metric experiment:
its generated-only receipt is insufficient for neural inference.

## Execution and observable closure

Use read-only bind mounts for every copied runtime root and selected model input;
verify actual mount flags inside the child. Start with a read-only filesystem
view, permit writes only to the new output/temp area, hide protected experiment
and corpus trees, and disable networking. Re-expose only the explicitly selected
program/model paths needed at the original locations. The implementation must
test the mount order and hidden-parent/re-exposed-child behavior.
Do not describe a mere Python import/open guard as an OS sandbox.

Freeze the project source separately and mount it read-only. Clear uncontrolled
Python/library search and user-site environment settings; record the exact
environment, Python search path and effective cache policy. Include necessary
platform metadata/procfs/device access in a declared namespace access policy.
No inherited open data handles or host package/cache writes are allowed.

Capture loaded module source/cache/extension paths and `/proc/self/maps` after
initialization and each synthetic stage. Every program/resource read must match
the frozen membership or an explicitly declared virtual/system-metadata access.
Use an open-access trace/guard in addition to loaded-file maps: maps alone miss
transient reads and resource files. Report each mechanism's limits; fail closed
on an uncovered executable/import/resource path. No fallback to live libraries.
Parent-side snapshot and source rechecks must run after child success or failure.

The new entry point accepts snapshot, assets, output and frozen source identity
only. It must reject corpus/input-manifest/attempt/labels/model-selector options.
Instantiate unchanged `apta_1_1_neural_inference.Models`; call unchanged
`apta_1_1_neural_dense_capture.synthetic(models)` directly. Do **not** call
`run(..., preflight_only=True)`: its `bind_inputs` still opens real-cohort files.
Do not call `worker` with inputs, `bind_inputs`, corpus preparation or scoring.
Importing canonical modules is permitted only after the runtime freeze; importing
a scorer module as an existing dependency is not authorization to call it.

## Fixed synthetic checks and decision

Run the same suite in two fresh processes under the same frozen runtime:

- Canonical BeatNet 73-frame synthetic features, seed 12345, seven-frame chunks,
  atol/rtol `1e-5`, exact recurrent reset, and 402325 parameters.
- Canonical 300-row PF activations, seed 20261022 for both RNGs, exact batch/
  one-frame parity and the published 32 synthetic events. Preserve defaults.
- Canonical Beat This! two-second silence with valid timestamps, then unchanged
  dense silence and seed-20261023 pulse/noise fixtures: each 101 rows/824 bytes,
  bit-exact serialization, complete coverage and exact postprocessor replay.
- One additional fixed 30-second mono pulse/noise signal at 22050 Hz, using the
  same seed, noise sigma `.005`, pulse spacing 11025 samples/amplitude `.5`.
  Original `split_piece(1500, border_size=6, avoid_short_end=True)` and keep-first
  aggregation must cover all 1501 rows/12024 payload bytes. Preserve source
  center `960*i`, exclusive endpoint ownership and exact event replay.
- Exercise `Models.infer('beat_this', ...)` only on newly generated two-second
  48000-Hz stereo PCM16 silence and pulse/noise WAVs in the new temp directory.
  Generate with seed 20261023, sigma `.005`, pulses every 24000 samples at `.5`,
  identical channels, clip then round-to-nearest-even after multiplication by
  32767. Verify the canonical soundfile/NumPy-mean/soxr-HQ path, mono geometry,
  finite values, dense bit preservation and exact original/replayed events.
  These synthetic WAVs are the sole audio-content exception; no old audio opens.
- Compare deterministic output digests across the two processes, excluding
  timings/RSS/paths. Preserve both complete outputs. Check the public codec/
  mapping/endpoint/overlap unit suites without opening historical traces.

Public negative tests before H1-Q must demonstrate refusal of changed files,
new/unbound cache or ELF loads, symlink escape/loop/endpoint mutation, copy-time
mutation, malformed membership/weight identities, a writable or absent runtime
mount, forbidden corpus paths and worker options, existing output, deadlines,
RSS/byte limits, child failure/orphans and incomplete closeout. A mutation test
uses disposable synthetic copies, never historical runtime or system files.

All checks must pass without skips or substitutions to claim
`SYNTHETIC_COMPATIBILITY_PASS` for this exact snapshot and source. A completed
receipt must still state `historical_numerical_parity=UNKNOWN`: published old
aggregate checks do not provide golden old logits/complete synthetic outputs.
Record H0's zlib delta and current missing historical coverage in the new receipt.
No synthetic PASS changes old rejection/INVALID verdicts or enables real replay.

## Limits and publication

CPU only, two Torch threads. Neural worker RSS ceiling **2097152 KiB**, inherited
from the neural protocol; **120 s** each for initialization and each canonical/
additional synthetic stage, **600 s** total per worker. Supervisor must enforce
deadlines/process-group teardown and sample RSS; record actual wall/peak values.
H0's metadata-only 256 MiB AS cap is not an inference budget; do not impose it on
PyTorch or claim that virtual address space equals resident memory.

Preparation and independent closeout each have **1800 s**, hashes **60 s/file**,
and outer bookkeeping **7200 s**. New immutable input storage is capped at
**8589934592 bytes / 200000 entries**, including selected weights, with an
incremental bound before writes. New receipts/logs/fixtures/outputs are capped
at **67108864 bytes**. The separate input-storage allowance does not increase
any old attempt-output, model-time or RSS cap. Product budgets remain UNKNOWN.

Private membership, file paths, per-file diffs, raw synthetic outputs and detailed
traces remain external. Publish protocol/source/snapshot/weight identities,
aggregate membership classes, historical-equal/new/drift counts, test outcomes,
wall/RSS/storage measurements and coverage limitations. Retain all failures.

After H1-Q, the next scientific decision remains a separately registered real
scope/projection proposal with explicit treatment of old runtime drift and exposed
development results. No automatic replay of the old 100-source or failed subset.
Independent transfer, calibrated confidence, whole-pipeline product resources,
native/consumer integration, physical P4, fresh acceptance and release remain open.
Preserve VERSION 1.0.1 and every existing frozen experiment.
