# Native Rust migration

Status: implementation in progress; no release, ABI replacement, full profile,
DSP accuracy, or physical hardware qualification claim. C remains the working
reference. This document owns the migration checklist and next-step handoff.

## Authority and baseline

The chosen starting point is `1.1.0`, on the separate `rust-rewrite` branch.
Initial local and remote verification on 2026-10-03 found both at
`e0369e2ce4f0bf44c0b9c14c9ef32f97012d2785`, with a clean tree. This is a dated
baseline, not a promise about current refs. No wholesale later-branch merge.

Contracts: [CONTRIBUTING](../../CONTRIBUTING.md), [specification](../../specification/APTA-SPEC.md),
[API/ABI](../api/APTA-API-ABI-1.0.md), [1.1 API](../api/APTA-API-1.1-DEVELOPMENT.md),
[registry](../../specification/container-v1-registry.md),
[DJ sections](../../specification/APTA-1.1-DJ-SECTIONS.md), and
[development status](../status/APTA-1.1-DEVELOPMENT-STATUS.md).
The required upstream issue is [drafted locally](ISSUE-DRAFT.md); scope and
release compatibility have not been approved by the maintainer.

## Architecture and constraints

One workspace and one experimental `libapta` crate under `rust/`. Its version
`0.1.0` describes the unpublished Rust crate only; C package/API/specification
versions remain unchanged. Core is `no_std`, forbids unsafe code, and uses no
allocator. `libm` supplies portable floating point square root/floor needed for
reference quantization. Desktop examples may use `std` and allocate explicitly.

Shared types in `rust/src/types.rs` use fixed-width values, checked lengths and
an explicit `Error` enum. These are native Rust types, not C ABI structures.
Serialization writes individual little-endian fields and never casts structs.
Session storage and output belong to the caller. Processing has a frame budget
and steps of at most 256 frames; zero budget fields mean unlimited, as in C.
A borrowed result view prevents concurrent mutation; copying to separate caller
storage allows a retained snapshot to outlive its session. The full C generation
pool, context lifetime and reference counting contracts still need adapters.

No filesystem, codec, thread or clock ownership enters the portable core.
Existing C consumers must eventually retain exported symbol names, calling
conventions, layouts, `struct_size`/`api_version`, error/status semantics,
custom allocators, push partial acceptance, pull exactly-once release, thread
contract, and immutable result acquire/release. Implement that boundary in a
separate narrowly audited FFI module only when a real slice can satisfy it.
Do not expose a partial library under the existing `libapta` shared-library ABI.

Container version 1 stays unchanged. Unknown optional sections are skippable;
unknown required sections fail. Known sections require payload semantics and
cross-feature validation before a full result is accepted. Borrowed envelope
inspection is deliberately distinct from full result validation. Buffer and
callback writers must eventually remain byte-identical. Preserve explicit
input, section, count, scratch and allocation limits.

ESP32-P4 requires fixed storage, bounded publication and no hidden heap use in
processing. Preserve fast/internal, PSRAM and DMA allocation-class distinctions
at the platform boundary. Existing C 30-minute capacity estimates and firmware
builds are not Rust measurements. Diagnostic physical boot is not qualification:
real USB/audio coexistence, 1,800-second timing/memory counters, thermals and
exact-candidate rerun remain open. No hardware operation is authorized here.

## Evidence and known failures

The initial C Release build (examples and warnings-as-errors enabled) passed
120/120 tests; explicit `APTA_BUILD_TESTS=ON` / reconfiguration enabled all
123/123, including installed conformance, versioned interchange and test
classification. The existing version-check include precedes the tests option,
so the first implicit-default configuration misses those three checks. The
Rust runner sets the option explicitly; no C build-source change is included. It includes API/layout/package checks, compatibility consumers,
container/streaming/builder tests, scheduler/ownership checks and synthetic DSP
coverage. Logs/builds live outside source at
`/home/shome/.local/share/libapta-audio/rust-rewrite/`.
The contribution guide's Clang sanitizer/fuzz configuration could not start:
Clang is not installed. GCC ASan/UBSan passed 116/116 after explicit test enablement; it does not
replace a libFuzzer campaign. The existing workflow pin audit fails on five
unpinned action references in `dj-candidate-comparison-1.1.yml`,
`key-validation-1.1.yml` and `meter-validation-1.1.yml`. Those unchanged files
are pre-existing failures; the Rust CI step introduces no new external action.

Software correctness and algorithm acceptance are different gates. The
[final DJ record](../status/APTA-1.1-FINAL-DJ-CORPUS-STATUS.md) retains a rejected
60-track attempt: key 15/60, downbeat 5/60, grid 4/60; meter 58/60. Key/grid
high-confidence errors fail. These are historical results, not rerun here.
Porting Rust does not fix them. Reproduce each affected failure before proposing
an improvement, preserve original gates and provenance, and reserve fresh
holdouts until the preregistered development gates pass.

Inspected later development ref `origin/agent/dsp-takeover-20260904` at
`b899eed4110d46ac4cf07eb0c924173ec0e7a59f`. Relative source changes concern
opt-in key mean normalization, contrast tracing and resonator workspace reuse
(`3fc7421`, `1e3a151`, `a883dd6`); no later core/port/container changes were found.
Its offline N1 solver repair and A1 attribution experiments are synthetic
research evidence, not production promotion. Historical E2 and fresh A1 banks
are spent. Inspect their source/protocols before touching key/NNLS; do not relabel
those results as fresh Rust evidence. Preserve accepted tempo ensemble and
confidence calibration IDs and gates during behavior-preserving ports.

Known waveform limitation retained for C comparison: clipping is derived from
the mixed signal, so opposing clipped stereo channels lose clipping provenance.
Rust rejects nonfinite F32 input before mutation, an explicit option in the
[PCM contract](../../specification/pcm-input.md); C replaces it with zero.
Neither choice is an accuracy improvement claim.

## Dependency order and acceptance

1. **Foundation and waveform interchange:** native types, checked container
   framing, WOVR semantics, PCM arithmetic, bounded sequential session. Require
   exact quantized C comparison, golden bytes, malformed input/resource tests,
   and PCM-to-container-to-C acceptance.
2. **Complete result model and container:** META, WDTL, TEMP/LGRD, GGRD/REVN,
   MKEY/MTRD/CONF in that dependency order; validated external-result builder,
   selective parsing and callback I/O. Require existing positive/negative
   fixtures, cross-feature checks, frozen 1.0 reader and deterministic bytes.
3. **Full session:** sparse ranges, overlaps, priority/FIFO/deadlines/aging,
   cancellation, pull/release, resumption/seeding, workspace planning and bounded
   immutable generation slots. Require no-allocation and concurrency/lifetime
   tests plus existing scheduler conformance.
4. **DSP parity:** three-band overview/detail, onset/S4 tempo/local grid, S6
   global/dynamic grids/revisions, key/meter and calibrated confidence. Define
   tolerances before comparison; waveform integer outputs and container bytes
   are exact. For later float DSP compare intermediate values with explicitly
   justified absolute/relative tolerances plus unchanged discrete selections.
5. **Consumer/platform replacement:** C ABI, CMake/pkg-config, static/shared
   distribution, Linux/POSIX and Windows adapters/tools, ESP-IDF component and
   examples. Run frozen consumers and LP64/ILP32 layouts; verify supported
   compiler/platform matrix. A host `no_std` build alone is not embedded proof.
6. **Separate algorithm improvements and release qualification:** reproduce
   failures, preregister changes, development/holdout separation, all original
   acceptance gates, physical P4 measurements, maintainer decisions and freeze.
   Retire C only after the agreed full replacement matrix passes.

Parallel ownership for initial implementation: lead owns Cargo/root builds,
shared types/public root, integration example/tests and this document; container
agent owns `rust/src/container.rs` and container tests; DSP agent owns
`rust/src/waveform.rs` and waveform oracle/tests; runtime agent owns
`rust/src/session.rs` and session tests. Lead also owns the borrowed WAV decoder,
example, pipeline test and `rust/check.py`. Subsequent ownership transfers must be
explicit. Integrate before independent review; never reset another agent's work.

## Capability checklist

| Existing capability / source | Rust destination | Validation / status |
|---|---|---|
| Fixed-width results/errors, `include/apta` | [types.rs](../../rust/src/types.rs) | Native wire and in-memory result graphs implemented; C layout/status boundary pending |
| Header/directory/CRC, `src/serialization` | [container.rs](../../rust/src/container.rs) | Implemented; 16 container checks + strict C oracle |
| WOVR reader/writer | [container.rs](../../rust/src/container.rs) | Implemented; exact C golden roundtrip, malformed inputs, strict C reader |
| META and fingerprints | [meta.rs](../../rust/src/meta.rs), types/container | Deterministic bounded CBOR, recognized-field ownership and writer complete; fingerprint computation pending |
| WDTL/detail interchange | [detail.rs](../../rust/src/detail.rs), container/types | Level-1 payload reader/writer and aggregate result validation implemented; exact C bytes; native detail analysis/scheduling pending |
| TEMP/LGRD, GGRD/REVN | [tempo.rs](../../rust/src/tempo.rs), [grid.rs](../../rust/src/grid.rs), [result.rs](../../rust/src/result.rs) | Payloads, canonical writing and cross-feature links implemented; exact C roundtrips; native DSP pending |
| MKEY/MTRD/CONF + cross-feature checks | [dj.rs](../../rust/src/dj.rs), result/builder | Payloads, limits, grid/quality links and canonical bytes implemented; native analysis pending |
| Buffer/stream/selective parse APIs | container/result/[stream.rs](../../rust/src/stream.rs)/[stream_write.rs](../../rust/src/stream_write.rs) | Complete known wire section set, bounded callbacks and selective retention; differences documented below; C ABI pending |
| S16/S24/S32/F32 interleaved/planar PCM | waveform/session | All five formats through typed views/session; exact C comparisons; C block ABI pending |
| Quantized mono/stereo overview | waveform | Implemented; 44 exact C oracle cases, ties, overflow, endpoint clipping |
| Three-band overview/detail | waveform | Pending (three-band detail remains outside original scope) |
| Push/backpressure/EOF/budgets/cancel | session | Implemented sequential known/unknown duration; fixed output capacity, budget/backpressure/EOF/cancel tests pass |
| Pull/seek/release callbacks | [pull.rs](../../rust/src/pull.rs), [sparse_pull.rs](../../rust/src/sparse_pull.rs) | Sequential known/unknown and scheduled known-duration pull; exactly-once release and absolute-offset seeking; C callback ABI remains pending |
| Focus/requests/sparse ranges/scheduler | [sparse.rs](../../rust/src/sparse.rs), [scheduler.rs](../../rust/src/scheduler.rs), publication | Known-duration sparse overview and request policy integrated; oracle/review/combined acceptance below; other feature schedulers pending |
| Context/static workspace/allocation classes | future runtime/FFI | Caller typed storage and sequential two-slot size plan; full scheduler/platform planner pending |
| Immutable generations/pool/concurrency | session + future runtime/FFI | Owned graphs and two caller-owned immutable slots; five lifecycle oracle traces and independent review pass; concurrent acquire/release pending |
| Resume/result seeding | publication/sparse/waveform | Validated owned overview checkpoint, source/fingerprint compatibility, inverse quantization and atomic native preflight; C tail difference documented below |
| External validated result builder | [builder.rs](../../rust/src/builder.rs), [native_validation.rs](../../rust/src/native_validation.rs), [owned_result.rs](../../rust/src/owned_result.rs) | Encoded subset plus native graph/provenance/session-state validation and deep ownership; C allocator/API boundary pending |
| S4 onset/BPM/local grid | future tempo | Pending |
| S6 global grid/dynamic tempo/revisions | future beatgrid | Pending |
| Musical key/meter/downbeat | future key/meter | Pending; accuracy gates remain failed |
| Quality/confidence calibration | future confidence | Pending; preserve accepted protocol/model |
| POSIX/Windows file and WAV adapters | [wav.rs](../../rust/src/wav.rs) | Borrowed WAV decoder complete initial format slice; four tests; filesystem/callback adapters pending |
| Analyze/inspect/validate/version/corpus tools | [wav_to_apta.rs](../../rust/examples/wav_to_apta.rs) | Waveform-only desktop demonstration; eight WAV formats/channel smoke cases; CLI parity pending |
| Push/pull/installed/package/ESP examples | examples | Push/WAV-to-container example implemented; others pending |
| C API/ABI and frozen 1.0 consumers | future FFI | Pending; C remains installed product |
| CMake/pkg-config/shared/static packages | root build future FFI | Cargo additive only |
| Linux, Windows/MSVC, ILP32 | platform CI | x86_64 host tests + AArch64 compile check; Windows/ILP32 pending |
| ESP-IDF ESP32/S3/P4 | ports future Rust integration | Pending compile and physical measurements |
| macOS/big-endian | platform matrix | C macOS community only; big-endian unsupported baseline |
| Conformance/interoperability/fuzz/security/SBOM | tests/tooling | C retained; Rust coverage partial |
| Research/evaluation Python tools | existing tools | Retained, adapt analyzer interface later; no rewrite needed solely for language |

## Initial implementation verification (2026-10-03)

`rust/check.py` passes the combined native/C path: 123 C tests, 34 ordinary Rust
tests in debug and release, four explicitly enabled external-C tests (44 exact
waveform comparisons and strict C container parsing), formatting, Clippy with
warnings denied, and no-default-features library compilation. Eight 70,001-frame
synthetic WAV cases (four encodings, mono/stereo) produce three-column containers
accepted by the C strict reader. Existing-output protection is checked too.
A separate AArch64 library compile succeeds; it is not target execution.

Tests: [container](../../rust/tests/container_conformance.rs),
[PCM/waveform](../../rust/tests/waveform.rs), [session](../../rust/tests/session.rs),
[allocation counter](../../rust/tests/session_allocation.rs),
[WAV](../../rust/tests/wav.rs), [integrated pipeline](../../rust/tests/pipeline.rs).
Evidence: `combined-check.log`, `c-gcc-sanitized-test.log`, and
`combined/smoke-waveform.apta` under the external evidence directory above.
No C product source was changed. No commits, pushes, issues, PRs or releases
were made. The GitHub CI step is authored, not remotely executed.

Independent agent review fixed: valid noncanonical container placement,
permissive reserved-column normalization, unsupported compression status,
completed-session cancellation behavior and S32 near-endpoint clipping after
F32 rounding. These are fixes to this Rust implementation; they do not claim to
resolve historical DJ algorithm failures. The retained C stereo clipping
provenance limitation remains open.

Native API differences remain explicit: PCM representation may vary between
pushes, source ranges are sequential, only complete
columns publish before EOF, and WAV status errors are native Rust errors. None
changes the existing C contract, whose replacement adapter remains pending.

## Waveform result and streaming continuation (2026-10-03)

Preserved all initial uncommitted work and the C reference. Rechecked the later
DSP ref against `1.1.0`: only key source differs; no META, WDTL or session changes
were applicable. Lead owns shared types, container integration, runner and docs;
agents owned META, WDTL and session modules with separate independent review.

Implemented:

- META recognized fields and presence (including an empty map), borrowed parsing,
  caller-owned copies and deterministic writing. UTF-8, shortest encodings,
  unsigned top keys, nested encoded-key order and preferred floating encodings
  are checked. C-equivalent limits: 64 top entries, depth 8 and 256 visited
  unknown-value items, 8,192 bytes per unknown string, plus recognized field caps.
  Typed parse/write ignores unknown keys; `copy_canonical` retains validated
  unknown bytes. No metadata allocation or lifetime tied to a destroyed session.
- WDTL level 1: 256 frames per column, 64 columns per tile, sparse tile coverage,
  borrowed columns, caller copies, canonical sorted writing, descriptor/range/
  state/confidence/alias validation. Multiple sections must have globally unique
  tile identities. Result parsing enforces aggregate column/tile caps. Default
  1,024-tile bound limits quadratic identity checks; validated lookup does not
  rescan payload semantics. This does not implement native detail DSP or sparse
  input scheduling.
- `parse_waveform_result` validates WOVR/META/WDTL and exposes borrowed results;
  `parse_waveform` now validates that same supported slice. Both reject other
  recognized analysis sections. The envelope parser remains framing-only.
  `write_waveform_result` emits canonical WOVR, optional WDTL, then optional META.
- Sequential unknown-duration sessions use `TOTAL_FRAMES_UNKNOWN`, cap accepted
  input by caller output storage and resolve length at EOF. Capacity exhaustion
  is distinct from temporary input-queue backpressure. Known and unknown paths
  produce exactly equal quantized columns; processing remains allocation-free.

Explicit native differences and review findings:

- META unknown values obey deterministic nested UTF-8/key/float rules more
  strictly than the C skip walker. Limits use `LimitExceeded`; malformed CBOR
  uses `Corrupt`. This is a native API, not C status-code compatibility.
- Permissive waveform/detail views normalize reserved column flags and absent
  band bytes before canonical writing. C's permissive copy retains those bytes.
- WDTL rejects a whole represented column starting at or beyond EOF; only the
  final column may be truncated (normative `specification/waveform.md`). The C
  reader/writer accepts that malformed geometry. C source remains unchanged.
- The C WDTL reader permits FINAL tiles with unknown duration; its writer refuses
  them. The native payload reader/writer preserves that asymmetry. Container
  overview constraints still apply independently.

### Sequential pull ownership

`pull::PullSession` owns one `PullSource` and a private sequential session.
`PullBlock` borrows PCM and a release callback; its drop guard calls release once
on success, validation failure, capacity failure or cancellation. No block is
retained across processing calls. EOF/WouldBlock/source errors transfer no block.
Source errors are terminal; WouldBlock is retryable. `into_inner` consumes the
adapter to return the source, preventing source substitution during processing.

Each call issues at most one read and consumes at most one 256-frame step within
the frame budget. Zero budget fields retain their unlimited convention; the
adapter may use less than the permitted work. Known-length sources complete
without an extra EOF callback; early EOF fails. Unknown sources probe one frame
at full output capacity: EOF succeeds, additional data is released and returns
`BufferTooSmall`. This probe may read one frame that is not accepted. Host callbacks
must return promptly; there is no wall-clock, panic recovery, seek or sparse
callback ABI guarantee. The core remains safe `no_std` without allocation.

### Final combined verification for this continuation

The required `rust/check.py --build-root .../combined --c-build .../c-baseline`
run passed after integration and independent review:

- C Release **123/123**; separate GCC ASan/UBSan **116/116**, explicitly configured
  with `APTA_BUILD_TESTS=ON`.
- Rust **71 ordinary tests** in each debug/release run, plus **5 explicitly run
  external-C tests**. These include 44 exact waveform cases, C strict parsing,
  and four byte-exact C parse/serialize result cases (combined META+WDTL,
  empty META, and existing META/detail fixtures).
- Eight 70,001-frame WAV conversion cases accepted by the strict C reader,
  including existing-output protection.
- Formatting, workspace all-target Clippy with warnings denied, host no-default-
  features compilation, and separate AArch64 no-default-features compilation.
  AArch64 is compilation evidence only; minimum Rust 1.81 remains unverified.
- Metadata tests include every binary16 encoding and 32,768 deterministic
  adversarial nested-CBOR cases. This is bounded regression coverage, not a
  libFuzzer campaign. Allocation counters cover known/unknown push and pull.
- Knowledge validation passed (15 projects, 86 notes, zero broken links).

Evidence remains outside source under
`/home/shome/.local/share/libapta-audio/rust-rewrite/`: `combined-check.log`,
`c-gcc-sanitized-test.log`, `knowledge-validation.log`,
`workflow-pins-current.log`, and the combined build/oracle outputs.
Clang is still unavailable. The workflow audit still fails on five unpinned
external actions in the three unchanged workflows named above. No C product
source, host services or hardware changed. No commits, pushes, upstream messages,
issues, PRs, merges or releases were made.

Tests added: [META](../../rust/tests/meta.rs), [detail](../../rust/tests/detail.rs),
[integrated result](../../rust/tests/waveform_result.rs),
[pull](../../rust/tests/pull.rs), and the
[C reserialization oracle](../../rust/tests/fixtures/container_oracle.c).
Session tests and the shared allocation counter were extended. Independent
review covered module semantics, integration, aggregate bounds, ownership,
cancellation and wire equivalence; findings and deliberate C differences are
recorded above. Full API/ABI, analysis, scheduling and platform replacement
remain incomplete.

## Complete payload and streaming continuation (2026-10-03)

The persistent replacement goal is active. All pre-existing Rust work and C
source remain preserved. Ownership for this continuation: lead owns shared
native types, root modules, result/container integration, runner and canonical
documentation; agents own TEMP/LGRD, GGRD/REVN/external validation, DJ payloads,
and stream transport/output in separate files. Independent reviews rotate across
those owners. Later-branch inspection found no changes to affected serialization,
builder, grid-position or grid-match source.

### Acceptance matrix for the current result milestone

| Capability | Implemented behavior | Validation command / evidence | Remaining limitation |
|---|---|---|---|
| TEMP/LGRD | Bounded borrowed payloads, native copies and canonical fields; selected tempo agreement | `cargo test -p libapta --test tempo --test result`; checked-in container fixtures | DSP generation and full C result ownership pending |
| GGRD/REVN | Segmented/explicit/hybrid grids, revision links, checked signed-ordinal Q32 positions | `cargo test -p libapta --test grid --test result`; C canonical oracle | Native-only coverage/representations supported by typed model below |
| MKEY/MTRD/CONF | Bounded views/copies, strict records and ordering, meter/grid and quality target validation | `cargo test -p libapta --test dj --test result`; independent DJ fixture | Native key/meter/calibration analysis pending |
| Full buffer results | All recognized payloads validated before publication; canonical writing and aggregate limits | `APTA_C_CONTAINER_ORACLE=.../combined/container-oracle cargo test -p libapta --test result -- --include-ignored`; eight original fixtures plus combined result byte-exact | Existing waveform-only API deliberately remains fail-closed on analysis sections |
| External construction | Stronger native validation and independent encoded caller storage | `cargo test -p libapta --test builder --test result_allocation` | Encoded API requires overview/one grid coverage; native graph API below handles broader representations; C allocators pending |
| Selective stream input | Small caller scratch, CRC of skipped sections, retained selected payloads and dependencies, source-size stability | `cargo test -p libapta --test stream`; 96 unchanged-C selection comparisons | Combined run and independent review pass; native callback/API differences documented below |
| Stream output | Record-wise canonical emission with fixed buffers, partial progress, seek/flush and preflight validation | `cargo test -p libapta --test stream_write` | Combined run and independent review pass; native callback/API differences documented below |
| Allocation and retention | No heap use in payload parse/copy/write, encoded finalize or stream input; retained caller storage survives input destruction | `cargo test -p libapta --test result_allocation` | C allocator ABI and bounded generation pool pending |

Wire reserialization and external construction are distinct APIs because the C
reader accepts some values its builder rejects. `result::write` preserves wire
acceptance; `builder::finalize` applies the stronger serializable construction
subset. Error semantics remain native Rust: malformed wire uses `Corrupt`,
invalid native values use `InvalidArgument`, exhausted bounds use `LimitExceeded`
or `BufferTooSmall`, and invalid callback progress uses `Source`.

MTRD validation keeps monotonic global-grid cursors; it does not rescan all beats
for every meter segment. The whole-frame coordinate and ordinal must match,
including beats with nonzero Q32 fractions. Pending streaming differences from
the C implementation are recorded and tested rather than silently weakened:
multiple WDTL sections follow the normative registry, and meter references are
checked against present encoded grids even when those grids are unrequested.

### Combined verification and explicit differences

Required combined command passed after integration and independent review:

```bash
python3 rust/check.py \
  --build-root /home/shome/.local/share/libapta-audio/rust-rewrite/combined \
  --c-build /home/shome/.local/share/libapta-audio/rust-rewrite/c-baseline
```

Evidence: `result-combined-check-rerun.log` under the external evidence root.
C Release **123/123**, Rust **121** ordinary tests in each debug/release run,
and **7** explicitly enabled external-C tests pass. External coverage includes
nine complete canonical parse/reserialize comparisons and **96** selective
stream feature-mask comparisons (eight fixtures times twelve masks), in addition
to existing waveform oracles. Formatting, all-target Clippy, host no-default-
features compilation and eight WAV interchange cases pass. A separate AArch64
no-default-features library compilation also passes; it is not target execution.
Tests demonstrate zero
allocation for full parse/copy/write, external encoded finalize and stream
input/output. Stream output uses fixed record buffers and at most 144 bytes per
callback request. Input handles a 1 MiB skipped section with 17-byte scratch and
zero retained payload bytes.

The first combined attempt passed 122 C tests and failed source packaging because
CPack copied a changing Cargo incremental lock from the ignored root `target/`.
The build tree was preserved at external `preserved-cargo-target-20261003`.
`cmake/APTAPackaging.cmake` now excludes generated `target/` directories, and the
existing archive test checks their absence. C implementation source is unchanged;
this is an additive packaging correction. All later Cargo commands use external
`CARGO_TARGET_DIR`. No sanitizer rerun is implied by the Release result.

Native stream semantics preserve the C stream parser's feature-mask distinction:
key/meter alone do not add `CONFIDENCE`. Full buffer parsing still does. Validation
only grid/tempo dependencies do not expose unrequested confidence or grid views.
Three-band selection uses the overview storage view. Native numeric limits are
explicit Rust limits, not C's zero-means-default initializer convention. Scratch
may be any nonempty caller buffer up to the configured cap. Selected payloads
remain encoded in caller-owned storage rather than allocated C structures.
CONF records are filtered to actually requested/materialized features. Unsupported
quality target bits fail rather than becoming materialized records.

External construction covers serializable results. The C in-memory builder also
supports empty/feature-incomplete results, provenance/session-state fields,
multiple grid coverage ranges and values its wire writer/reader cannot represent.
Those are now represented by the native typed ownership model described below. Streaming output deliberately uses
stronger external preflight; `result::write` remains the wire roundtrip path.
Tempo-period coherence uses the exact rational form of C's one-millibpm builder
tolerance; target-specific long-double rounding at pathological boundaries is
not claimed identical.

The migration remains incomplete. Runtime scheduling/generations, remaining DSP,
C ABI, packaging, adapters/tools and platform execution still require their own
contract acceptance. No algorithm improvement, fresh holdout result, maintainer
approval or hardware qualification is claimed.

## Native typed ownership and publication continuation

`native_validation::validate` accepts the full native graph independently of
wire serialization: selected-only tempo, multiple grid coverage ranges,
explicit local grids, unknown confidence, native provenance and session/result
info. `owned_result::requirements` checks exact retained graph bytes before
`copy` or transactional `replace` mutates caller storage. Copies preserve
metadata bytes and compact referenced waveform/detail columns, including
aliased ranges; unused backing columns are excluded. Owned results can outlive
all input buffers and be read concurrently. Borrowed views prevent replacement.

`result::from_native` converts the serializable subset using caller tile
descriptors and full preflight. Wire-inexpressible representations fail explicitly;
provenance, lineage and native result generation are not container fields.
Use the compact owned view when importing aliased source columns.

| Capability | Implemented behavior | Validation command / evidence | Remaining limitation |
|---|---|---|---|
| Native external validation | Source/info/provenance, cross-feature and full in-memory grid constraints | `cargo test -p libapta --test native_validation`; 28 C builder scenarios in `native_result_oracle` | Exact rational tempo-period tolerance retained; pathological target long-double rounding not claimed identical |
| Native ownership | Deep copy, packed referenced data, transactional replacement, byte/count limits, borrowed lifetime and independent concurrent reads | `cargo test -p libapta --test owned_result --test result_allocation`; C owner comparison and compile-fail borrow test | Caller storage; C allocator and binary layouts remain pending |
| Native to container | Fully validated wire subset, exact all-section output | `cargo test -p libapta --test native_wire`; explicit C oracle test | Native-only fields/representations cannot all be serialized by version 1 |
| Two-slot publication | Initial empty generation, retained leases, fixed caller storage, planned complete overview capacity | `cargo test -p libapta --test publication --lib` | Five unchanged-C traces and independent review pass; sequential known-duration overview only, single thread of pool control |

The native ownership milestone passed the required combined command; evidence
is `native-owned-combined-check.log` in the external evidence root. This run
precedes the subsequent publication integration and does not certify that slice.
C source remains unchanged. Native trusted-session construction is crate-private;
external callers cannot bypass external-import validation with session origins.
No result slot uses a serialized container as its universal representation.

### Publication milestone verification

`publication-combined-check.log` records the required combined runner passing:
C Release **123/123**, Rust **156 tests including doctests** in both debug and
release, and **11 explicitly enabled external-C tests**. Formatting, all-target
Clippy, host no-default-features and eight WAV interchange cases pass. A separate
AArch64 no-default-features library compile also passes; no target execution is
implied. The checked-in allocation counter now includes pool creation, typed
storage, push/EOF/process, acquisition/cloning and retained reads after session
destruction. C implementation source remains unchanged.

Five test-only C lifecycle traces compare generation, state, changed/available
features, exact quantized waveform fields and confidence. They cover initial
empty publication, state-only first-push/EOF generations, EOF before processing,
publication failure after consumed PCM, release/retry, cancellation rollback,
completion rollback and retained results after destruction. Successful processing
can publish both a waveform generation and a completed-state generation. Native
`Progress` is explicitly mapped to C status in the oracle; this is not a C ABI.
The oracle uses one mono column, supplemented by native multi-column, final-tail,
capacity/overflow and cloned-lease tests. Concurrent acquisition/release, unknown
length publication and full C workspace-layout parity remain open. Sparse work
continues in the next milestone below.

### Sparse runtime and scheduler milestone

`SparseSession` retains disjoint accepted ranges independently of its completed
column coverage. Push accepts at most 4096 frames up to the next overlap; a block
starting inside accepted coverage conflicts. Caller-owned fixed node storage,
dense accumulators and snapshot arrays make resource exhaustion explicit. Before
EOF, a short last column remains incomplete. Published spans contain completed
columns in source order and preserve holes. A session can complete with a Partial
overview; trusted native validation permits that C runtime behavior while the
external builder retains its stricter Completed/Final rule.

`PublishedSparseSession::new_scheduled` adds caller-owned request slots. Policy
covers priorities, +8 aging, deadline/FIFO ties, focus, demand gaps, cancellation
and coarse request progress. Cancelled/satisfied requests retain their slots and
IDs. Processing refreshes request progress before waveform publication and ages
requests once after successful work publication, before final-state publication.
Only overview requests are supported by this slice.

Expanded C traces corrected the earlier publication retry assumption: a retry
may consume more queued PCM and then publish one accumulated snapshot. After
slot exhaustion, a state-only cancellation or EOF snapshot temporarily omits the
overview, matching C's pending-completion flags. A later process reconstructs
coverage; terminal cancellation cannot republish it. These behaviors are covered
by ten sparse C scenarios and sequential counterparts. Native focused tests
also cover retained clones, byte/span limits, gap/fragment order and partial-tail
quantization. `sparse_oracle.rs` compares every published span and column exactly.

| Capability | Implemented behavior | Validation / evidence | Remaining limitation |
|---|---|---|---|
| Sparse overview | Prefix admission, overlap conflicts, bounded nodes/ranges, complete-column coverage, EOF holes and budgets | `cargo test -p libapta --test sparse --test sparse_publication`; `APTA_C_SPARSE_ORACLE=.../combined/sparse-oracle cargo test -p libapta --test sparse_oracle -- --ignored` | Known duration; dense accumulators and conservative snapshot capacity; full platform planner remains pending |
| Request policy | Sixteen retained slots, score/deadline/FIFO/aging, focus/demand gaps and progress | `cargo test -p libapta --test scheduler --lib`; six exact C scenarios and independent review pass | Overview only; no clock ownership/time budget or other feature dependencies |
| Scheduled publication | State transactions, immutable slots, request refresh/aging order and allocation-free buffers | Sparse/publication tests; all-target Clippy passed during integration | Combined runner and scheduler C oracle pass; C concurrency/allocator boundary remains open |

The sparse/scheduler milestone passed the required combined runner; evidence:
`sparse-scheduler-combined-check.log` under the external evidence root.
C Release **123/123**, Rust **178 tests including doctests** per debug/release
run, and **14 explicitly enabled external-C tests** passed. Ten sparse scenarios,
three sequential pending-publication counterparts and six scheduler scenarios
are included. Formatting, all-target Clippy, host no-default-features compilation
and eight WAV interchange cases passed. Independent review corrected the
unknown-duration scheduler's background start to use the contiguous accepted
prefix. Allocation instrumentation includes scheduled sparse creation, request/
focus/demand, processing, exhaustion recovery, EOF and retained leases.

### Checkpoint seeding and scheduled pull milestone

Checkpoint seeding accepts an already validated `OwnedResult`,
checks source geometry/fingerprint policy and overview resolution, then copies
inverse-quantized accumulator state while retaining Created and its generation.
Native storage preflight is atomic; C resource failure has undocumented partial
mutation. C's seeded short-tail transient can extend beyond declared EOF before
end-of-input; the native model retains bounded source extents. Both differences
are recorded by explicit tests; complete seed behavior parity is not claimed.

The pull adapter reuses absolute-offset `PullSource` and exactly-once borrowed
block release. Scheduled demand provides seeking without a separate seek callback.
Transactional Failed-state publication is required for source errors. The slice
compares callback/release/cancel/EOF ordering against the effective C activation
wrappers, including blocked publication and retry.

| Capability | Implemented behavior | Validation / evidence | Remaining limitation |
|---|---|---|---|
| Checkpoint seeding | Created-only source/fingerprint checks, exact C inverse quantization, repeated seeding and independent input lifetime | `cargo test -p libapta --test seed --test seed_oracle -- --include-ignored` with `APTA_C_SEED_ORACLE`; thirteen exact C scenarios | Native preflight is atomic; seeded transient tails stay inside declared EOF where C can exceed it; these are explicit behavior differences |
| Scheduled pull/seek | Absolute-offset reads from scheduled demand, partial blocks, WouldBlock, exactly-once release, transactional source failure and cancellation | `cargo test -p libapta --test sparse_pull --test sparse_pull_oracle -- --include-ignored` with `APTA_C_SPARSE_PULL_ORACLE`; sixteen C scenarios | Known duration for bounded publication; callback latency excluded from processing budget; C ABI still pending |
| Allocation and retained lifetime | Seeded pull creation/read/release/EOF/publication and post-destruction snapshot | `cargo test -p libapta --test result_allocation` | Caller storage; final C allocator/refcount/concurrency boundary remains open |

`seed-pull-combined-check.log` records the required combined runner passing:
C Release **123/123**, Rust **187 tests including doctests** in each debug/release
run, and **17 explicitly enabled external-C tests**. Formatting, all-target
Clippy, host no-default-features and eight WAV interchange cases passed. Each
consequential implementation received independent review. The expanded pull
oracle found and corrected Failed + cancellation error precedence: InvalidState
for that transition, Source for an ordinary terminal pull retry.

The unchanged C bounded-slot implementation explicitly rejects unknown duration
(`src/core/apta_result_pool_layout.c`, `tests/unit/result_pool_layout.c`). The
native bounded publication limit therefore matches this C profile. Nonbounded
unknown-duration result publication remains part of later allocator/runtime
acceptance; existing native sequential pull supports unknown input separately.

### Active continuation: soft processing clock and waveform analysis

Clock injection is being added without changing existing frame/step budget
literals. It must initialize a saturating deadline, treat a zero initial clock
value as disabled, check after each chunk and exclude source callback latency,
as the C implementation does. No host clock or service is owned by the core.

Independent three-band and detail kernels are being ported from the inspected
baseline. Three-band filter/quantization order and detail cache eviction, pinned
runs, eager push processing and replay remain distinct contracts. Float kernel
comparisons start with bit identity; quantized columns and canonical bytes must
remain exact. Integration, independent reviews and a new combined check are
required before these active additions enter the accepted matrix.

## Next executable handoff

Continue in this checkout on `rust-rewrite`; preserve all uncommitted work.
Inspect live status before editing. Run Cargo with an external target directory:

```bash
cd /home/shome/p/libapta-audio
git status --short --branch
git remote -v
CARGO_TARGET_DIR=/home/shome/.local/share/libapta-audio/rust-rewrite/combined/cargo-target cargo test --workspace --locked
python3 rust/check.py \
  --build-root /home/shome/.local/share/libapta-audio/rust-rewrite/combined \
  --c-build /home/shome/.local/share/libapta-audio/rust-rewrite/c-baseline
```

Finish soft-clock and native detail/three-band integration, then advance onset/
tempo/grids/key/meter/confidence in dependency order. Preserve the pending
nonbounded runtime, C ABI/allocator/concurrency and platform acceptance items. New slices must pass focused
oracles, independent review and combined checks before acceptance. A safe single-thread RefCell pool
does not satisfy concurrent C acquire/release; retain that acceptance item until
the final allocator/FFI boundary is implemented and independently reviewed.

No frozen accuracy holdouts, push, issue, PR, release, hardware operation or
host-service change is authorized. Full C ABI/platform replacement remains
pending; no maintainer approval or completed rewrite is claimed.


## Paused checkpoint — 2026-10-03

Paused at the user's explicit request because concurrent agent work was placing
excessive load on the machine. The persistent goal is **paused**. All three
workers were interrupted. No Cargo/rustc/combined-runner/CTest/CMake build process
was found running at the pause check. Preserve every working-tree file; no reset,
clean, branch switch, commit or publication was performed. Resume with the lead
alone by default; do not restart concurrent workers without the user's agreement.

### Last accepted integrated checkpoint

`seed-pull-combined-check.log` remains the latest complete combined verification:
123 C tests, 187 Rust tests including doctests per debug/release run, 17 external-C
tests, format/Clippy/no-default-features checks and eight WAV interchange cases.
The active clock and DSP additions below were made afterward and have not passed
a fresh combined run. All evidence is under
`/home/shome/.local/share/libapta-audio/rust-rewrite/`.

### Preserved work after that checkpoint

- `deadline.rs`, timed Session/SparseSession hooks, and public
  `process_with_clock` methods in publication and scheduled pull are implemented.
  Existing WorkBudget literals remain unchanged. The root module exports the
  private deadline module. Focused tests are in `tests/deadline.rs`; inspect the
  actual clock-oracle files and current results before resuming.
- Clock oracle investigation found an unresolved observable difference: C's
  effective overview process also makes four deadline clock reads at the S4,
  S6, key and meter stage boundaries, even when those features are not requested.
  For one tested pull call C reports six clock calls, native two; source read and
  release both precede deadline initialization. Do not weaken or hide this test.
  Decide how to preserve these calls in the integrated runtime before accepting
  the clock slice; future analysis must share the same deadline.
- `band.rs`, `tests/band.rs`, and `fixtures/band_oracle.c` are present and exported.
  The agent reported three tests passing with 63 C comparisons (nine rates times
  seven original signals), bit-identical filter outputs and exact quantized
  columns. The kernel is **not yet integrated** into sparse/session processing.
  Proposed integration: optional caller-owned BandSums per overview column plus
  one persistent filter, attached while Created. Preserve scheduled processing
  order and filter history across seeks. The proposed private `apply_complete`
  helper was discussed but not authorized/implemented at the interruption;
  inspect the file before deciding. Public checked apply must remain validated.
- The band oracle confirms a C distinction: heap publication advertises the
  three-band feature, bounded publication omits its bit despite identical band
  data/flags. Record this explicitly; do not claim complete feature-mask parity.
- `detail_analysis.rs`, `tests/detail_analysis.rs`, and
  `fixtures/detail_analysis_oracle.c` are present and exported. Agent-reported
  seven tests pass with five direct C kernel scenarios: exact tile geometry,
  states/confidence, quantized columns, cache eviction/protection and pinned runs.
  This is a **kernel oracle**, not public session integration. Integration must
  preserve eager work during accepted push, replay rules, scheduler protection,
  cache-degradation semantics and publication. Completed sessions can retain
  Partial detail tiles; trusted validation will need a source-backed extension.
- `rust/check.py` includes seed/pull/scheduler/sparse oracles. It does not yet
  necessarily build or run the new clock/band/detail oracles; inspect and extend
  it once those slices are integrated. No later full verification is implied.

### Exact resume order

1. Read this checkpoint and inspect live branch, remote, status and processes.
   Do not restart the stopped workers automatically.
2. Inspect current deadline/band/detail source and tests; reconcile the explicit
   clock callback-count discrepancy against effective C wrappers.
3. Integrate three-band and detail kernels with caller-owned storage, native
   publication, scheduler dependencies and resource plans. Keep C reference
   source unchanged and run focused oracles before combined checks.
4. Run the required combined command above with external build directories,
   update this matrix and the knowledge index, and validate the notebook.
5. Continue onset/tempo/grids/key/meter/confidence, then C ABI/allocators,
   packaging, tools and platform acceptance. The full replacement is unfinished.

### Completion estimate at pause

Approximately **35% of the full software replacement**, with a broad **30–40%**
uncertainty range based on remaining effort rather than lines or test counts.
Result model/container work is mostly complete; bounded overview runtime is
substantial. Most musical analysis, C ABI/allocator/concurrency compatibility,
consumer packaging, full tools and platform integration remain. Standalone DSP
kernels and compile-only target checks do not count as completed integration or
hardware acceptance. This estimate is not a completion or release claim.

### Checkpoint publication authorization

After pausing, the user explicitly authorized committing and pushing the current
checkpoint to `origin/rust-rewrite`. This authorizes checkpoint publication only;
the implementation goal remains paused and the unfinished clock/DSP work above
is preserved without a new full verification claim.
