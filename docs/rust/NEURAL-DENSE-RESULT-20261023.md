# Complete Beat This! dense logits — verified capture

Date: 2026-10-23. Exact clean implementation:
`297284933e922a669f65b8939b824da7b9e6f740`, committed/pushed before real capture.
The [registered experiment](NEURAL-DENSE-CAPTURE.md),
[fixed protocol](NEURAL-DENSE-CAPTURE-PROTOCOL.json) and
[preflight](NEURAL-DENSE-PREFLIGHT.md) remain unchanged. The
[public aggregate](NEURAL-DENSE-RESULT-20261023.json) records full counts,
event parity, payload/coverage checks, resources and hashes without private IDs
or model coordinates.

## Decision and next work

**Complete capture passes: all 100 sources retain their original events exactly.**
The full beat/downbeat f32 evidence is now saved before peak filtering. Original
inference, saved legacy records and decoded-payload postprocessing give identical
event dictionaries on all sources, including floating seconds, source frames,
downbeat binding and exclusive-endpoint lists.

This adds evidence for future decoding, not a new decoder or musical-accuracy
result. The previous model/OLS result remains 187/401; the rejected piecewise
candidate remains 174/401 with 13 breaks. No labels or musical scores are read
or recomputed here. The new artifact does not close confidence, independent
transfer, fresh acceptance, native/product or physical P4 gates.

**Next: separately register a bounded label-blind decoder over these frozen
dense beat/downbeat logits, comparing variable timing and competing phase/family
paths.** Bind the completed trace report/payloads before defining model parameters,
coordinate interpretation, resource caps, output identity and zero-regression
gates. Preserve all weak evidence and explicit unknown support; reference labels
must remain exclusively in evaluation. Compare to original OLS and unchanged
upstream event output, not the label-derived construction witnesses. Do not
retrain/change checkpoint, tune the rejected learned-piecewise fit or reopen
reference-anchor research. No decoder is implemented or selected by this capture.

## Complete rows, source coordinates and legacy parity

| Capture invariant | Count |
|---|---:|
| Sources with exact saved-event parity | 100 |
| Original S6 windows retained | 600 |
| Complete two-channel logit rows | 150095 |
| Rows with centers inside original source windows | 149997 |
| Rows centered at exclusive source end | 98 |
| Rows centered beyond source end | 0 |
| In-domain learned beat events, unchanged | 6095 |
| In-domain learned downbeat events, unchanged | 1851 |
| Exclusive-endpoint beat events retained separately | 10 |
| Exclusive-endpoint downbeat events retained separately | 3 |
| Raw binary payload bytes | 1202360 |

Every row contains both beat and downbeat logits in original order, with exact
little-endian f32 bits. Neither sigmoid, thresholding, peak selection, f16 nor
lossy quantization is applied to the saved payload. Nonpositive values are
retained: 132081 beat logits and 144046 downbeat logits.
Logit sign/magnitude is network evidence, not calibrated confidence; these counts
are not a selection criterion or a musical correctness score.

Row index `i` has upstream time `i/50` seconds and exact canonical center
`960*i` frames. The original centered 1024-point spectral support and offline
future context remain explicit. These centers do not establish causal
availability or independent S6-window inference. Noninteger resampling lengths
stay explicit; complete feature length is `mono_samples//441+1`. Original chunk
starts/lengths independently prove every retained row is assigned after border
trimming with unchanged `keep_first` overlap behavior. No sentinel-like finite
value is treated as missing support.

All 600 original S6 center ranges remain. Window-owned rows plus exclusive-end
and beyond-source rows partition every retained row exactly once. Boundary rows
are not moved into the audio domain, and padded/centered support is not asserted
as measured silence. Original exclusive-endpoint events remain separate from
in-domain events. The private per-source metadata preserves all geometry, chunk
coverage, input/payload hashes, events and host resources.

## Isolation, runtime and verification

The capture uses unchanged legacy `Models.infer`, checkpoint, channel averaging,
soxr HQ resampling, CPU two-thread neural forward, reset behavior, upstream
minimal postprocessor and source-domain event adapter. Read-only taps observe
original inputs/outputs and return the original objects. Every binary is checked
against original interleaved tensor bytes and decoded tensor values, then replayed
through the same postprocessor. No source audio, model, package or upstream code
is modified. Exact asset/runtime/audio bindings from the legacy freeze are
rechecked before/after capture; original coarse files are newly hash-bound for
source/window geometry only. Prepared labels and saved scoring reports are not
parsed. No training, corpus selection, new audio, holdout or reserved transfer.

The run binds 11821 files. Separate replay independently decodes all
100 binaries with NumPy/struct, checks bit/framing/finite/count integrity,
reconstructs every source/S6/boundary row and original chunk-assignment schedule,
and applies the pinned original postprocessor to saved logits. All legacy event
records, aggregates and hashes reproduce without another real model forward.
The report and every private payload/record/source binding are rechecked before
the public aggregate is exported.

All **148 Python tests**, **37 unchanged Rust controls per debug/release
profile**, strict format/Clippy/no-default checks and two actual pinned-runtime
synthetic dense fixtures pass. The generated fixtures retain all 101 rows and
exact original events. No new neural/native integration or full Rust/C/sanitizer,
hosted CI/product/P4 qualification is claimed by these host checks. Core/native
validators/defaults, consumer pins, failed candidates and release state are
unchanged.

Measured capture/replay/write and final input recheck take **431.53 s**;
slowest source **5.78 s**, process peak RSS **648132 KiB**.
Private payloads/records occupy 1679365 bytes. The registered caps are
120 s/source, 1800 s capture, 2 GiB process RSS and 64 MiB total attempt output.
Initial binding/model initialization/synthetic preflight, tests and independent
export are outside capture timing. RSS includes the complete legacy harness
(both loaded reference models and Python/PyTorch/runtime), not isolated Beat
This! model RAM. No embedded resource/latency claim follows.

## Reproduction and evidence hashes

From clean exact implementation source in WSL, use the original pinned venv,
retained assets/label-free inputs and a new output directory (overwrite refused):

```sh
"$ASSETS/venv/bin/python" tools/apta_1_1_neural_dense_capture.py \
  --assets "$ASSETS" \
  --attempt "$ASSETS/comparison-source-domain" \
  --output "$NEW_PRIVATE_OUTPUT"
```

Private freeze, synthetic preflight, raw binaries, source records and report:
`/home/daniel/.local/share/libapta-audio/rust-rewrite/neural-dense-20261023/attempt/`.
Exact source archive, verification log and independent payload/event replay:
`D:/AI/LIBAPTA/outputs/rust-neural-dense-20261023/`.

- Protocol SHA-256:
  `28490ab1983cac6abd011923c5f4e78d3e7bf5096fdb119a102c386e1de62685`.
- Private report SHA-256:
  `72707018f6cbe8ff8550d6e799fcb3a224747f2ce68381f9e1e3963383cf9b05`.
- Exact source archive SHA-256:
  `0cad34cd5b26b55b8f1fc910ae2b86dac2493374c72ed6066fe4fbe1a5d5fff8`.
- Independent replay/export SHA-256:
  `6af4c8098217c546a0688b38eb13fae0b75c6fd1e8630d4138be001b54410acb`.

The already-open GTZAN cohort retains unresolved cross-dataset identity and is
development evidence only. Commit/push complete capture before dense decoder
registration. No improved musical accuracy or physical qualification is claimed.
