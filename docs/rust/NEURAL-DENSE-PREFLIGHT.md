# Complete neural logit capture — implementation and preflight

Date: 2026-10-23. Registration committed/pushed at
`7f84b740ff6e70e43fad6d3f10c82c53a4adc2cd` before code or new real inference.
[Protocol](NEURAL-DENSE-CAPTURE-PROTOCOL.json) SHA-256:
`28490ab1983cac6abd011923c5f4e78d3e7bf5096fdb119a102c386e1de62685`.

`tools/apta_neural_dense.py` implements strict bounded interleaved little-endian
f32 framing, bit-preserving decoding, exact source/window center mapping and
independent chunk-assignment coverage. There is no threshold or sigmoid in
the payload path. It retains nonpositive logits, signed zero and finite f32
subnormals. NaN/Inf, malformed/truncated/extra bytes, wrong channels/counts and
geometry mismatches fail. Row order is implicit in serialization and verified
against the observed tensors and bound hashes, not guessed from signal values.

The opt-in `tools/apta_1_1_neural_dense_capture.py` reuses unchanged legacy
`Models`, including initialization/reset, synthetic preflight and inference.
Read-only taps observe original resampled signal and spectrogram/logit returns;
original function return objects flow unchanged to original postprocessing.
Payload bytes are compared with the original contiguous interleaved f32 tensor
bytes and decoded tensors before replay. No upstream/model/runtime modification,
package installation, native/core/default/consumer/release/scorer change.

Before any real forward the runner binds the old input manifest and worker
report, checkpoint, model revision, legacy-frozen asset/runtime/audio bytes,
old event records and current repository source. Coarse files are first hash-
bound in the new freeze, read for original source/window geometry only; unlike
model/runtime/audio they were generated after the original comparison freeze.
No saved scoring report or prepared-label contents are parsed. All legacy
event fields, including seconds and exclusive endpoints, must match exactly
both original inference and decoded-payload postprocessing. Existing output
and any integrity/resource failure are refused; no real retry/rescue follows.

Source centers remain 960 canonical frames apart with origin zero. Every row is
owned by exactly one original S6 center range or retained boundary/beyond list.
The centered spectral support and offline future context remain explicit; the
index is not causal availability. Original `split_piece` starts/lengths prove
all rows are assigned after six-frame borders with unchanged `keep_first`.
Finite sentinel-like values are retained and are not used to infer completeness.

All **148 Python tests pass**: 18 new dense codec/mapping/coverage/adapter checks
and the previous 130 tests. New checks cover exact byte order, signed zero,
subnormals and f32 extremes, no filtering, shape/cap/nonfinite failures, short
and overlapping chunk coverage, source/S6/exclusive/beyond centers, both short
real-source length geometries, invalid resampling/partition and label-bearing
window rejection. They also preserve the original exclusive-event adapter.

Actual pinned neural-runtime preflight passes on two generated two-second
signals: silence and pulse plus seeded noise. Each yields 101 complete rows,
824 payload bytes, bit-exact logits, full assignment and exact original/replayed
events. The retained legacy preflight passes neural chunk/reset and seeded PF
checks (32 generated events, chunk max error `8.940696716308594e-07`). No real
music, labels or accuracy scoring is opened by these preflight executions.

The unchanged native controls pass **37 tests per debug/release profile** with
Rustfmt, focused Werror Clippy and no-default library checks. These qualify no
new neural grid, native integration or P4 behavior. Full Rust/C/sanitizer/hosted
CI/product/physical matrices are not rerun or claimed for this host-only tool.

External stdlib/native verification script/log and independent replay script:
`D:/AI/LIBAPTA/outputs/rust-neural-dense-20261023/`.
Synthetic pinned-runtime attempt:
`/home/daniel/.local/share/libapta-audio/rust-rewrite/neural-dense-20261023/synthetic-preflight/`.
Commit/push this implementation/preflight before the single clean-source real
capture. After capture, independently replay every saved payload without a
second real model forward before publishing aggregate counts/hashes.
