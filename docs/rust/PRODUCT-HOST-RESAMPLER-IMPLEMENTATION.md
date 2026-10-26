# HS-03 streaming resampler implementation checkpoint

Date: 2026-10-26. Registration checkpoint:
`2f92972ba1147a084c442bc8d352d8cc1ea990fa`.
[Protocol](PRODUCT-HOST-RESAMPLER-PROTOCOL.md),
[machine receipt](PRODUCT-HOST-RESAMPLER-IMPLEMENTATION.json).
Status: **IMPLEMENTATION_PREFLIGHT_PASS; CLEAN_MEASURED_RUN_PENDING**.

The opt-in `experimental-compact-resampler` feature implements the frozen
CS-FIR65 arithmetic with a65-sample ring, borrowed static F64 tables and no
component heap allocation or retained caller buffers. Fill/Convolve/Emit phases
each perform bounded work. All real frames are validated, stereo mixing follows
the registered f32 order, tap products/addition remain separate F64 operations,
and only completed f32 outputs become visible. No output clipping, timestamp
shift, edge renormalization, coefficient regeneration or full-song storage occurs.

The API reports consumed SOURCE FRAMES, emitted samples, first global output,
work/flow/status/error and cumulative MAC/availability counts. It handles input/
output backpressure, exact offsets, explicit short EOF, known-length padding,
mid-call invalid PCM and cancellation. Quota0 changes no state. Cancellation is
terminal even after clearing the flag; Done remains stable. Returned PCM before
Done is provisional working data, not a committed source-wide analysis.

The separate synthetic helper/CLI uses fixed artificial generators,512-frame
input and256-sample output buffers, rolling SHA-256, scalar extrema/timing
records and no retained full trace. All102 short cases are fixed literals;
expected digests stay outside Session. The CLI accepts only six registered names
and embeds protocol/fixture identities. The independent Python worker imports
only the frozen stdlib scalar reference and neutral supervisor, verifies input/
output digests, counts, work, availability, numerical tolerance and statuses,
and preserves raw child logs on failure. Full reference uses verified periodic
interior compression plus direct zero-padded boundaries, not Rust scheduling.

## Executed checks

- Six new tests pass in debug and release, zero ignored. All102 public oracles
  are checked under60 schedules each:6120 fixture/schedule combinations per
  profile. Source/output hashes are bit-identical to the frozen ordered oracle.
- Tests cover phase/rational availability, last output/EOF, output canaries,
  mid-tap privacy, zero buffer/quota backpressure, source gaps/repeats, malformed
  interleaving, oversupply, unsupported profiles/lengths, explicit short EOF,
  nonfinite/out-of-range PCM in either channel and after an emitted prefix.
- Cancellation covers Fill/Convolve/Emit, priority over invalid input, no resume,
  stable completion and fresh sessions. The ring is a fixed65-element array;
  source slices/output pointers are never stored. Component allocation absence
  follows from this safe fixed-storage implementation, not allocator-hook timing.
- Existing waveform controls:6 pass,3 C-oracle-dependent ignored. Existing
  waveform_result controls:5 pass,1 C-container-oracle-dependent ignored. Default
  runtime context:5 pass,1 C-lifetime-oracle-dependent ignored. All five ignored
  checks are **NOT_RUN**, not PASS. No unrelated C/P4 matrix was executed.
- fmt and focused Clippy(-D warnings) pass; release CLI builds; Python compiles.
  A development worker independently verifies102 short observations and all four
  fixed operational violation probes are refused. No full600s case was measured
  before this implementation checkpoint. The clean six-case run is pending.

Development preflight artifacts are external at
`D:/AI/LIBAPTA/outputs/hs03-development-preflight-20261026/`.
During implementation Clippy required replacing an explicit drop of fixed stack
storage with lexical scope end. Cleanup timing now measures scope end separately;
there is no heap to reclaim in Session/Driver, and that observation must not be
presented as device memory reclamation. Extrema comparisons use decoded f32 bits
after JSON numeric parsing; output digests already bind exact f32 bytes. No
registered coefficient, oracle, tolerance, algorithm or resource cap was changed.

## Continue from a clean checkpoint

Commit/push implementation before running the fixed six-case supervisor:

```text
python tools/apta_compact_resampler_synthetic.py --executable BUILD/release/apta-compact-resampler-synthetic --output NEW_EXTERNAL_OUTPUT
```

Build is release with `experimental-compact-resampler`, outside checkout. Output
must be new and external. The supervisor binds actual installed Rust/Cargo tools,
feature, binary, component/authority/fixture/reference/coefficient bytes, Python
source/cache/interpreter/shared libraries and linked ELF libraries before execution.
It enforces the registered60s CPU/90s wall/64MiB RSS/128MiB AS/1MiB aggregate
output limits and checks final supervisor status/size. Preserve and stop on any
failure; no parameter rescue. This is supervision and input/API isolation, not
an OS filesystem sandbox. Freeze source before running, then publish a separate
audited result checkpoint and commit/push.

Default/core/consumer/wire/HS-01/HS-02 sources and VERSION1.0.1 remain unchanged.
This is synthetic host implementation/preflight only. FFT/frontend/neural model,
music/transfer/acceptance and physical P4 are NOT_RUN. Neural rights/training
independence and PF-01 exact-board budgets remain unresolved; JC1060P470C_I_W_Y
is unavailable, revision unobserved. No model/training/device/provider/release ran.
