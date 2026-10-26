# HS-02 implementation checkpoint

Date: 2026-10-26. Registration checkpoint:
`a06afca15998a05a446ebb74dff1257fb119c563`.
[Protocol](PRODUCT-HOST-GRID-PROTOCOL.md),
[implementation receipt](PRODUCT-HOST-GRID-IMPLEMENTATION.json).
Status: **IMPLEMENTATION_PREFLIGHT_PASS; CLEAN_MEASURED_RUN_PENDING**.

The opt-in runtime feature `experimental-compact-grid` now provides the registered
safe host postprocessor. `Session` borrows probabilities/path or diagnostic events
and caller event storage. It performs incremental whole-source validation, exact
threshold trimming, frozen state/edge checks, connected-region correction, phase
checks and checked i128/u128 regular-grid fitting. It uses the prescribed rational
round-half-up and Q32 coordinates, preserves global row offsets and excludes EOF.
It rejects unsupported coverage, phase, timing, tempo and capacity without partial
results. Constructor, step, cancel and result allocate no heap storage; allocating
fixture/serialization/report helpers are in the explicitly synthetic helper module.

Each work unit handles one bounded row/state/search/event or fixed arithmetic
operation. Atomic cancellation is checked per unit. Cancellation cannot resume;
completed output remains stable. NoEvidence, unsupported grids, malformed input,
overflow and zero quota are explicit. Scratch capacity is3072 events/24576 bytes;
the postprocessor adds no HMM workspace or per-song parent table. Generated
probability/path owners and maps remain separate from that scratch count.

All50 frozen case inputs/expected outputs are fixed literals expanded from the
registered public JSON. Expected verdicts and case names never enter Session.
No Rust JSON dependency was added. The CLI accepts only the six fixed case names,
embeds protocol/fixture digests and reports actual generated-input, event and
summary hashes. The independent Python worker reconstructs input bytes and
compares every output/status/hash against the original fixture JSON.

The supervisor reuses the unchanged neutral binding/resource helper. It binds
protocol, fixture, frozen authorities/assets, exact code/executable/interpreter,
loaded source/cache/extensions/shared libraries and ELF dependencies; checks
clean source; enforces the registered inherited CPU/AS and parent wall/RSS/output
caps; preserves child logs; checks final supervisor status and aggregate size.
This is input/API isolation and supervision, not an OS filesystem sandbox.

## Executed preflight

- Eight new tests pass in debug and release: one checked-arithmetic unit test
  and seven integration tests. Each profile compares all50 public fixtures under
  quotas1/7/256/4096/usize::MAX (250 fixture/quota combinations).
- Cancellation/reuse covers all nine reachable processing phases; tests also
  check zero quota, canaries, hidden partial results and completed-result stability.
- All ten positive grid fixtures pass the unchanged native validator and wire
  builder/parser. Corrupt meter witnesses are refused. Artificial one-column
  waveform data is serialization scaffolding, not a delivered waveform analyzer.
  Independent wire bytes survive mutation/reuse of source and event scratch.
- Two small compositions run the existing HMM on generated3/4 and4/4 probabilities
  and validate resulting source-wide grids. The thirteen HS-01 tests remain green
  in both profiles; HS-01 source/tests/CLI/harness are byte-identical.
- Existing product_grid_contract controls:12 passes. Default runtime context:
  5 passes,1 existing ignored C lifetime comparison requiring
  APTA_C_CONTEXT_LIFETIME_ORACLE. That comparison is **NOT_RUN**, not PASS.
- fmt and focused Clippy(-D warnings) pass; release executable builds; Python
  compiles. Development supervisor checks refuse all four fixed wall/RSS/output/AS
  violations. A separate development fixture worker verifies all50 observations,
  actual input hashes and native/wire records under inherited resource limits.

Development evidence is outside Git at
`D:/AI/LIBAPTA/outputs/hs02-development-preflight-20261026/`.
Development lint repairs simplified redundant generated arithmetic; no registered
fixture, tolerance, source geometry, algorithm or resource cap changed. The clean
six-case measured run has not occurred at this implementation checkpoint.

## Continuation

Commit/push implementation before running:

```text
python tools/apta_compact_grid_synthetic.py --executable BUILD/release/apta-compact-grid-synthetic --output NEW_EXTERNAL_OUTPUT
```

Use the release binary built with `experimental-compact-grid`, an external build
directory and new external output. Preserve failures and stop rather than changing
caps or selecting successful cases. Record the exact clean implementation SHA,
then publish the separate aggregate result and commit/push checkpoint.

This is synthetic host implementation evidence. Model rights/training independence
remain unresolved; PF-01 still needs the unavailable JC1060P470C_I_W_Y, with no
observed silicon revision. Product PF-04, music/transfer/acceptance and physical P4
timing/memory are NOT_RUN. Default/core/consumer/wire behavior and VERSION1.0.1 are
unchanged. No model, corpus, training, device, provider activation or release ran.
