# HS-01 implementation and synthetic preflight

Date2026-10-26. Registration checkpoint:
`3c2946ef53968aa8b13d2eee2a00536187fc6857`.
[Protocol](PRODUCT-HOST-HMM-PROTOCOL.md), [implementation receipt](PRODUCT-HOST-HMM-IMPLEMENTATION.json).
Status: **IMPLEMENTATION_PREFLIGHT_PASS; CLEAN_MEASURED_RUN_PENDING**.

The registered safe Rust host prototype is implemented behind runtime feature
`experimental-compact-hmm`. The default runtime, allocator-free core, C/wire/
consumer integration and VERSION1.0.1 are unchanged. This is not the full neural
pipeline, beat-event correction/grid construction or a P4 port. No neural weights,
corpus, saved logits or formal holdouts were used. Product PF-04, board memory
budgets and model rights/provenance remain open; HS-01 is the authorized narrow
synthetic exception, not a replacement for those gates.

## Delivered behavior

`compact_hmm.rs` provides validated borrowed graph/log-density inputs, checked
storage planning and caller-owned host workspace. Score mode has two F64 vectors;
trace mode adds checkpoints, one local parent block and private output path.
Each step unit initializes/copies one cell, evaluates one edge, commits one state,
advances a phase or traces one path state. Step/cancel contain no allocation and
check an AtomicBool request per unit. Terminal cancellation cannot resume and no
partial path is exposed. Constructor validation/allocation and workspace drop are
separate from stepping. Completed results remain immutable under later cancel.

Forward checkpoints and block replay use identical arithmetic and compare boundary
score bits. A corrupt checkpoint produces ReplayMismatch/Failed with no result.
Zero rows, all-impossible paths, malformed/nonfinite/out-of-domain inputs, capacity,
allocation and zero work quota have explicit outcomes/errors. Incoming ties preserve
the first sorted predecessor; final ties preserve the lowest state ID.

Frozen graphs are embedded only in the opt-in synthetic helper/executable. Generated
densities are source-independent; the CLI accepts only fixed `--case NAME` values.
It does not accept an audio/model/input path. Ordinary cases independently score
both meters, select one, trace it, compare score bits and verify every output edge
and recomputed path score. Full trace capacity is16575840 B including the60000 B
output path; this is required payload, not a measured host/P4 peak.

The Python supervisor reuses the unchanged neutral DD supervision/binding helper.
It enforces inherited CPU/AS and parent wall/RSS/output caps, records child rusage
high-water, interpreter/source/cache/shared-library/executable bindings, and preserves
child stdout/stderr even on failure. This is API/input isolation and resource
supervision, not an OS filesystem sandbox. No old neural/corpus runner is imported.

## Executed checks

- New decoder tests:13 debug and13 release passes, zero ignored. Includes exhaustive
  tiny cases, independent monolithic large-graph257/513-row checks, B1/2/3 and quota
  invariance, cancellation in all phases, workspace reuse, invalid inputs/capacities,
  hidden partial output and deliberate replay corruption.
- Existing product-grid controls:12 passes. Default runtime context controls:
  5 passes,1 existing ignored test requiring APTA_C_CONTEXT_LIFETIME_ORACLE. That
  C lifetime comparison is NOT_RUN, not PASS; no broader C/embedded matrix was run.
- fmt and focused Clippy with `-D warnings` pass; release executable builds.
  Python parses/compiles without neural packages. No Cargo dependency was added.
- Development operational preflight: fixed wall/RSS/output/AS probes are refused.
  The RSS probe uses a minimal fresh supervisor and explicitly verifies that the
  allocating CHILD exceeds16MiB. An initial development check had hit the heavier
  parent baseline; the harness was repaired without changing the registered cap.
  The final probe observed61712KiB in that child and16252KiB in its supervisor.

These are implementation/generated preflight checks, not yet the clean fixed
five-case measured experiment. There is no musical/release/product success claim.

## Reproduce and continue

```text
cargo test -p libapta-runtime --locked --features experimental-compact-hmm --test compact_hmm --lib --target-dir BUILD
cargo test -p libapta-runtime --locked --release --features experimental-compact-hmm --test compact_hmm --lib --target-dir BUILD
cargo clippy -p libapta-runtime --locked --features experimental-compact-hmm --test compact_hmm --bin apta-compact-hmm-synthetic --lib --target-dir BUILD -- -D warnings
cargo build -p libapta-runtime --locked --release --features experimental-compact-hmm --bin apta-compact-hmm-synthetic --target-dir BUILD
python tools/apta_compact_hmm_synthetic.py --executable BUILD/release/apta-compact-hmm-synthetic --output NEW_EXTERNAL_OUTPUT
```

BUILD/output must be outside the checkout. Commit/push this implementation first.
The final command rejects a dirty tree or reused output and runs only the fixed
registered cases, once in order, with preserved failures. It may produce only
HS01_SYNTHETIC_HOST_PASS_ONLY, FAIL or INVALID/INCOMPLETE evidence. The separate
aggregate result will bind its exact clean implementation SHA and runtime.
