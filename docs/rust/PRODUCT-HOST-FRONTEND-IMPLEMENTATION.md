# HS-04 bounded frontend implementation checkpoint

Date: 2026-10-27. Registration: `efc9c431dc9e6670bbed5f9653c1363b58ab74dc`.
Implementation baseline: `d078ef291b2df49f015ba935974256ff10d0743b`.
[Protocol](PRODUCT-HOST-FRONTEND-PROTOCOL.md),
[six-case count erratum](PRODUCT-HOST-FRONTEND-COUNT-ERRATUM.md),
[machine receipt](PRODUCT-HOST-FRONTEND-IMPLEMENTATION.json).
Status: **IMPLEMENTATION_PREFLIGHT_PASS; CLEAN_MEASURED_RUN_PENDING**.

The opt-in `experimental-compact-frontend` implements the registered centered
1764-sample Hann window, explicit seven-frame DFS stack with radices2/2/3/3/7/7,
one in-place F64 complex work array, natural bin order, complex64 projection,
882 magnitudes, 144 filtered log bands and 144 positive lag1 differences.
Every frame performs all42336 complex MACs; no constant/zero/periodic shortcut
or different backend is present. Tables, arithmetic order, tolerances and input
envelope are unchanged. The input ring retains original samples for FFT leaves.

Session owns fixed arrays only, borrows no caller slice between calls, and has
no allocation, I/O or callbacks. Every sample, stack action, FFT leaf/load/MAC/
store, magnitude, filter term, log/difference/history scalar and complete-row
copy is budgeted. Cancellation is checked before every unit. Partial transform,
filter and history work survives pauses; only complete288-f32 rows are emitted.
Offset/length/PCM/EOF errors fail explicitly and retain prior reported progress.
Rows remain provisional until Done. Quota0 is inert; terminal sessions ignore
buffers and cannot resume. Known geometry gives exact source Q32 row centers
and separately reports future-context availability.

Host Session value is41448B (alignment8), below49152B; declared component payload
40760B and six immutable tables50476B remain separate. The fixed long Driver
value is44960B, including512-f32 input2048B, one output row1152B, rolling hashes,
indices and host timing state. Binary record packing adds a bounded1159B stack
record at the transport boundary, not another retained feature queue. These
are host layout facts, not measured P4 stack/heap/PSRAM or whole-pipeline peak.

The CLI accepts only registered names and emits `HS04BIN1`, ten complete
primitive records for fixtures, feature rows and one bounded JSON trailer.
It does not read files, music or neural weights. Python independently compares
every received primitive bin to both analytic/direct references and every
feature scalar to both dtype-aware and ideal references, then discards records.
It preserves exact trailer bytes, transport digest/count, last complete record,
bounded failure witnesses and raw stderr. There is no full-source stdout spool.
Worker SELF plus waited CHILDREN CPU has a combined120s acceptance gate in
addition to inherited hard limits. Numeric failures and operational/integrity
failures are recorded separately; both stop the run without parameter rescue.

Executed checks:

- Seven integration tests pass in debug/release, zero ignored. The initial six
  debug tests ran together; the subsequently added CLI rejection test ran
  separately in debug. All seven ran together in release. Twenty short streams
  each use40 schedules (800 combinations/profile), with bit-identical candidate
  output and both reference tolerances. Ten primitive probes use five quotas,
  all1764 bins and both oracles. One internal intermediate-spectrum/radix7/
  partial-filter test passes in both profiles.
- Tests cover exact geometry/EOF, backpressure/starvation/history, canaries,
  invalid PCM after a provisional prefix, source gaps/repeats/oversupply,
  quota/cancel priority, every phase, terminal fences and real HS-03 compositions
  at both sample rates and channel counts. Safe code/fixed arrays establish
  Session allocation absence; no allocator-hook or physical-memory claim is made.
- HS-03 controls: six tests and6120 schedules pass in debug and release.
  Product-grid controls:12 pass. Default-context controls:five pass, one ignored
  because `APTA_C_CONTEXT_LIFETIME_ORACLE` is absent; that check is NOT_RUN.
- fmt, focused Clippy(-D warnings), release CLI build and Python compilation pass.
  Clippy caught one range-style issue during development, corrected before freeze.
  The checker accepts two valid artificial transports and refuses12 malformed
  numeric/ordering/JSON/trailer cases. All four registered operational violation
  probes are refused, including the allocating child RSS/AS probes.
- The independent development fixture worker verifies30 observations:10 FFT
  probes plus20 streams/54 rows. Static registration validation still passes,
  including118 raw assets,140 Decimal complex anchors,93 selected filter bands
  and120000 reference rows. This static check is not a candidate measurement.

Development evidence is external at
`D:/AI/LIBAPTA/outputs/product-host-frontend-preflight-20261027/`.
No full600s candidate case ran before this implementation checkpoint. After
commit/push, run the six fixed cases once from clean exact source:

```text
python tools/apta_compact_frontend_synthetic.py --executable BUILD/release/apta-compact-frontend-synthetic --output NEW_EXTERNAL_OUTPUT
```

Use an external release build with `experimental-compact-frontend`. Preserve
failures; no changed caps/tolerances, subset, overwrite or backend rescue. Audit
all bindings/artifacts and write a separate aggregate result/handoff commit/push.
HS-01/02/03, default/core/wire/consumer and VERSION1.0.1 are unchanged. This is
synthetic host implementation only: neural inference, music/transfer/acceptance,
P4 budgets, codec/product integration, provider activation and release remain
unqualified. Model rights/training independence are unresolved; JC1060 hardware
is unavailable and its silicon revision is unobserved.
