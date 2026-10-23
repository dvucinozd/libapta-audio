# Complete Beat This! logit capture with legacy-event parity

Registered 2026-10-23 at clean `rust-rewrite`
`346d5034e68f266ffa52d0dc4cb14d10a6466d0e`, before code or new real inference.
The [fixed protocol](NEURAL-DENSE-CAPTURE-PROTOCOL.json) owns exact model,
preprocessing, coordinates, payload, parity, resource and stop contracts.

The [learned-piecewise result](LEARNED-PIECEWISE-RESULT-20261023.md) rejects
another fit over final events: 174 versus 187/401, with 13 breaks. This step
retains additional existing network evidence before peak filtering. It tests
capture integrity only, not another detector or musical-accuracy hypothesis.

Reuse the original pinned CPU runtime/model/inference adapter. Observe original
spectrogram and returned f32 beat/downbeat logits without modifying the original
forward or minimal postprocessor. Retain every row in a strict little-endian
binary, including negative/weak values and source-boundary centered rows.
Rows have 20-ms centers, exactly 960 canonical 48-kHz source frames apart;
centered feature support/future context is not causal availability. Keep all
original S6 windows and exclusive-endpoint events explicit.

Every saved binary must reproduce its exact f32 bits and all original beat/
downbeat seconds, source frames and exclusive-endpoint events through the same
upstream postprocessor. Bind/check legacy model/runtime/audio and all current
source bytes first; no prepared labels/scoring report is parsed. Prove the
original chunk aggregation assigns every row independently of logit values.

Expected cost is one additional pass over the same 100 thirty-second prefixes,
roughly the previous host inference cost, with caps of 120 s/source, 1800 s
capture, 2 GiB process RSS and 64 MiB output. These are conservative host limits,
not isolated model RAM or P4 feasibility. Any mismatch stops capture without
an alternative checkpoint, rounding, endpoint or sample-rate retry. A partial
attempt is preserved and never promoted to a complete trace set.

Commit/push registration, then implementation/synthetic preflight and its
commit/push, before one clean-source real capture. Independently decode/replay
all saved logits afterward without a second real model pass. Publish aggregate
counts/hashes only. Coordinates, logits, audio, IDs and mappings remain private.
Only complete parity permits the next separately registered dense probability
decoder; no labels, scoring, decoder, native/default/consumer/release change or
fresh/holdout/transfer access is part of this capture.
