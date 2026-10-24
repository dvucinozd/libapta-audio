# P2 generated native interoperability and consumer policy

Date: 2026-10-24. **GENERATED NATIVE/WIRE AND SOURCE POLICY PASS.**
Baseline: `185b1a2357da1ca978f79a746f9ef8d3bde35893`, `rust-rewrite`.
Authority: [P2 protocol](CONTEXT-ANCHOR-PROJECTION-P2-PROTOCOL.md) and
[machine registration](CONTEXT-ANCHOR-PROJECTION-P2-PROTOCOL.json), following
[P1](CONTEXT-ANCHOR-PROJECTION-P1-20261024.md). The
[aggregate receipt](CONTEXT-ANCHOR-PROJECTION-P2-20261024.json) binds the exact
probe sources, authorities, executables, consumer snapshot and external logs.

## Result

Four new Python contract tests, nine unchanged P0 registration tests, ten
unchanged Rust builder tests and five unchanged native-coordinate tests pass:
**28 tests total, zero failures/errors/skips in the final run**. Focused Clippy
with warnings denied and rustfmt checks for both new Rust files also pass.
These are selected contract checks, not the full C/Rust/P4 matrix.

Fourteen public generated accepted windows match the unchanged independent P1
oracle before conversion. Each window is tested separately as SEGMENTS, EXPLICIT
and HYBRID; no source-wide assembly is inferred. Three additional frozen public
abstentions retain their statuses and are never dispatched to native builders.
The 402 native/policy process calls establish:

| Check | Final observed result |
|---|---|
| C/Rust generic builder constructions | 84 accepted |
| Both readers on both writers' containers | 168 exact point/ordinal/meter roundtrips |
| GGRD/REVN/MTRD payload comparisons between writers | 42 byte-identical comparisons |
| Shifted meter whole frame/ordinal, unowned context downbeat, one-Q32-unit hybrid error | 20 expected builder rejections; no output file published |
| Source-exact adapter, normal mode on all42 representations | 4 grids accepted,32 UnsupportedGrid,6 tempo-only |
| Missing/provisional meter, provisional grid, undersized destination | 3 tempo-only controls and1 Capacity rejection; destinations unchanged |

The native readers preserve the original context anchor, exact owned Q32 points,
advanced model ordinals and declared owned count. Fractional grid coordinates
remain exact. MTRD references the same encoded point's whole frame and ordinal;
its absence of a fractional field does not move the grid point. No absent meter
is filled. Right-witness fallback and explicit no-owned-witness cases retain
their P1 results. Neither builders nor these probes establish a bijection with
observed events or continuous coverage from independent windows.

The product result is narrower than generic representability. The actual adapter
source rejects partial-window and hybrid grids when final meter/grid are present.
Missing/provisional evidence yields tempo only; a full-source constant accepted
control still converts, with exact expected milliseconds, phase and BPM x100.
All rejection/fallback controls preserve sentinel destination banks.

## Consumer and source identity

The external probe compiles all12 extracted Rust source files from the registered
Ruzina snapshot `df7f8f4aea3852cafb660ea67dae26f1610287ec` across its adapter,
track-analysis and waveform crates. The adapter lib.rs hash matches P0. Source
bytes are unchanged. Only external minimal host manifests substitute current
local libapta and use the exact embedded-graphics0.8.2 dependency, with default
features disabled. The external lock and executable hashes are in the receipt.

This tests snapshot source policy against the migration library. It does not
build current firmware or qualify the real pinned dependency. The actual consumer
checkout stays at `0f2fb1425ef76f2541a8c76841cfa52178dca589`; provider, manifests and
libapta pin `25d09de364e9507532c0508c838a8fecf0772d13` are untouched.
All14 P0 document bindings,17 reviewed sources,37 frozen C3 components and P1
implementation/oracle hashes verify before and after execution. No production
source, old test, workspace manifest or repository lock changes.

## Execution and resources

WSL Ubuntu; Python3.12.3/jsonschema4.10.3; offline locked Cargo1.95.0.
The opt-in test provides an external `--prepare BUILD CONSUMER_REPO EVIDENCE`
step. It builds C `apta_core` with warnings as errors, compiles the C probe with
`-std=c11 -Wall -Wextra -Werror`, builds the Rust example and the snapshot-source
consumer probe, and records every build resource result. It never modifies the
actual consumer workspace. Final runtime invocation:

```text
APTA_P2_BUILD=/home/daniel/.local/share/libapta-audio/rust-rewrite/context-projection-p2-build-20261024-qualified \
APTA_P2_OUTPUT=/mnt/d/AI/LIBAPTA/outputs/context-anchor-projection-p2-20261024/fixtures-qualified \
/usr/bin/python3 tests/unit/apta_1_1_context_projection_native.py -v
```

The Linux supervisor bounds the whole native suite to60s (stricter than1800s),
262144 KiB per process and67108864 external evidence bytes. Final native suite:
**4.953909s**, maximum observed process RSS/HWM **28612 KiB**, longest individual
probe **0.024738s**. Build-only maximum observed process RSS/HWM is **276416 KiB**
under its separately registered1048576 KiB compiler cap. The completed final
prepared build tree is222827000 bytes; after Clippy/controls it is266190061 bytes,
below the536870912-byte per-build tree cap, measured after completed phases.
Stable logs are monitored live; ephemeral
compiler scratch trees are not traversed concurrently. Compilation is not an
inference/Rust/P4 resource measurement.

All evidence, including failed preparation logs, stays outside Git under
`D:/AI/LIBAPTA/outputs/context-anchor-projection-p2-20261024/`.
Preserved preparation failures: a wrong static-library filename, a supervisor
race with removed CMake progress files, and an incomplete C provenance setup.
The first native suite consequently failed its constant control; its negative
test counts have no qualification value. The harness was corrected to use
libapta.a, monitor stable logs while checking build bytes after each phase, and
provide the required external-import provenance/name. Preliminary builds were
also superseded after making zero confidence explicit. No production algorithm,
contract, threshold or original fixture was changed to obtain the final pass.
The final build binds all four probe/test source hashes before/after compilation;
final execution checks those hashes and executable identities again.

## Next boundary

**P3: separately register a matched projection evaluation**, applying the same
new representation to the original-event control and fixed coupled candidate.
Specify grid/meter/event/support denominators, unsupported sets and correctness
on the shared supported intersection before execution. P2 does not authorize a
corpus replay or scoring, nor does it change the consumer pin/history producer.

Scientific C3 remains **REJECTED14/16**; real-runtime metadata is `NOT_AUDITED`,
independence `UNRESOLVED`, real authorization absent, confidence unqualified.
There is no neural inference/training, product activation, physical P4,
acceptance or release. Stable `VERSION` remains `1.0.1`.
