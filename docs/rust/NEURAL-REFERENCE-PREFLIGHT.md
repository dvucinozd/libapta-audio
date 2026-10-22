# Neural reference tooling and first preparation

Date: 2026-10-22. Registration was committed and pushed at
`941a543530537b8ef2574ea11b5eaad7d744d2d2`, with matching remote SHA before
implementation. This checkpoint implements host preparation, source-coordinate
projection, retained-development representation diagnostics and fixed upstream
inference adapters. No native processing or product adoption changes.

## What passed

Ten new projection/provenance tests and six unchanged annotated-development
tests pass. Tests cover half/double tempo, variable rhythm, missing events,
source shifts, incomplete reference context, unsupported meter, non-finite or
duplicate events, metadata-only balanced selection and hash mutation refusal.
The lightweight new suite is included in `rust/check.py`; no model packages are
needed by that suite.

The actual pinned BeatNet model loads strictly with 402325 parameters.
Synthetic feature inference split into seven-frame chunks agrees with one
73-frame call to maximum absolute error `8.940696716308594e-07` (registered
numeric check uses atol/rtol 1e-5). Reset reproduces the output exactly. With
both NumPy RNGs reset, the official PF produces the same 32 synthetic events
from batch and one-frame calls. Beat This! final0 completes a two-second
synthetic silence inference with valid timestamps. These checks do not measure
musical accuracy, complete streaming-frontend parity or P4 execution.

The first synthetic preflight exposed PyTorch inference-tensor mutation outside
inference mode. The adapter now creates fresh hidden/cell tensors at source
reset; the failed synthetic output is retained separately. No corpus inference
or accuracy result preceded this operational fix.

## Representation result

The same fixed label-derived OLS projection was evaluated with the unchanged
APTA scorer on all previously-open development references. All 480 windows are
retained, including incomplete reference context.

| Corpus | Complete context | Coarse joint-correct | Reference-fit joint-correct | Reference-fit phase-correct |
|---|---:|---:|---:|---:|
| ASAP | 160/240 | 1 | 6 | 72 |
| Ballroom | 165/240 | 9 | 49 | 156 |

These are reference-derived representability witnesses, never candidate
accuracy. Failure of this particular OLS fit does not establish an upper bound
over all possible constant grids. The results justify keeping event accuracy,
local period agreement and constant-grid projection separate; they do not
authorize changing any frozen gate or replacing the failed temporal result.
Every coarse aggregate reproduces the retained result exactly; all bound
source/audio/reference/output files are rechecked after the audit.

## Acquisition and preparation stop

The complete GTZAN audio archive was acquired from the Marsyas Hugging Face
mirror; its SHA-256 matches the registered archive digest. The independently
published mirdata index matches its MD5. The deterministic selection freezes
100 IDs before annotation contents are read. All selected WAVs match the index,
and all 100 are canonicalized using the existing preparation tool.

Preparation then **stops** because annotation v1.0 lacks one selected reference.
There is no replacement within this attempt, no complete prepared manifest,
no neural music inference and no model accuracy report. Retain the selection,
canonicalization and failure before registering a metadata-availability repair.
The correction must occur before the first corpus prediction; model settings,
metric thresholds and the old failed model gates remain frozen.

JKU's checkpoint host returned HTTP 503. The downloaded unmodified final0
mirror has 81058141 bytes and SHA-256
`8c328b45f59d8dd3dff219253ff6a8d6482be57d0133a29140e2febbf8eb8331`,
matching the independently recorded upstream checkpoint digest in the
[musetric export provenance](https://huggingface.co/musetric/beat-this-onnx).
Acquisition source is the
[LocalMusic checkpoint mirror](https://huggingface.co/rtikw/localmusic-assets).
This is mirrored provenance, not a claim of a successful direct upstream
download or author-signed authenticity.

Runtime is isolated under the external experiment root: Python 3.12.3, CPU
PyTorch 2.14.1, torchaudio 2.11.0, NumPy 1.26.4 and madmom source
`27f032e8947204902c675e5e341a3faf5dc86dae`. Madmom's model submodule is needed
for package imports; its pretrained networks are not used. BeatNet uses its
own fixed weights. No third-party source is copied into the project.

Private artifacts are under
`/home/daniel/.local/share/libapta-audio/rust-rewrite/neural-reference-20261022/`:
`prepared/` (stopped preparation), `representation/report.private.json`, and
`synthetic-preflight-3/preflight.json`. Source maps, labels, audio and raw events
stay outside Git. Formal holdouts/fresh acceptance remain closed; confidence
calibration, independent transfer, native implementation and P4 remain open.
