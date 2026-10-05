# Native Rust migration

Status: implementation resumed 2026-10-04; no release, ABI replacement, full profile,
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

One workspace with an experimental portable `libapta` crate under `rust/` and
a separate safe desktop `libapta-runtime` synchronization boundary under
`rust/runtime/`. Its version
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
5. **Native consumers first; legacy compatibility separately:** direct Cargo
   dependency on the portable core for native Rust consumers. Pajoniiir's native
   P4 architecture does not require ESP-IDF, C allocators, C ABI or C packaging.
   Keep product adaptation in the consumer. Verify its actual target/toolchain,
   storage and bounded work; host/no_std compilation is not embedded execution.
   C ABI, CMake/pkg-config, static/shared distribution, frozen C consumers,
   LP64/ILP32 layouts and ESP-IDF for actual IDF consumers remain separate gates.
6. **Separate algorithm improvements and release qualification:** reproduce
   failures, preregister changes, development/holdout separation, all original
   acceptance gates, physical P4 measurements, maintainer decisions and freeze.
   Retire C only after the agreed full replacement matrix passes.

Historical parallel ownership for initial implementation (current continuation uses one implementation agent): lead owns Cargo/root builds,
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
| META and fingerprints | [meta.rs](../../rust/src/meta.rs), types/container | Deterministic bounded CBOR, recognized-field ownership and writer complete; source identity transport and opt-in CLI source-object SHA-256 implemented; host-supplied identity remains unverified |
| WDTL/detail interchange | [detail.rs](../../rust/src/detail.rs), container/types | Level-1 payload reader/writer and aggregate result validation implemented; exact C bytes; native eager sequential/sparse detail and sparse request protection/replay integrated |
| TEMP/LGRD, GGRD/REVN | [tempo.rs](../../rust/src/tempo.rs), [grid.rs](../../rust/src/grid.rs), [result.rs](../../rust/src/result.rs) | Payloads, canonical writing and cross-feature links implemented; exact C roundtrips; native default S4/S6 analysis and explicit revision acceptance integrated; explicit requested-capability path and 91 lifecycle profiles plus four expanded retry traces pass; full mask/failure matrix remains |
| MKEY/MTRD/CONF + cross-feature checks | [dj.rs](../../rust/src/dj.rs), result/builder | Payloads, limits, grid/quality links and canonical bytes implemented; default native key/meter/calibration integrated; accuracy gates unchanged |
| Buffer/stream/selective parse APIs | container/result/[stream.rs](../../rust/src/stream.rs)/[stream_write.rs](../../rust/src/stream_write.rs) | Complete known wire section set, bounded callbacks and selective retention; differences documented below; C ABI pending |
| S16/S24/S32/F32 interleaved/planar PCM | waveform/session | All five formats through typed views/session; exact C comparisons; C block ABI pending |
| Quantized mono/stereo overview | waveform | Implemented; 44 exact C oracle cases, ties, overflow, endpoint clipping |
| Three-band overview/detail | [band.rs](../../rust/src/band.rs), session/sparse | Three-band overview integrated; exact C comparisons; three-band detail remains outside original scope |
| Push/backpressure/EOF/budgets/cancel | session | Implemented sequential known/unknown duration; caller capacity plus safe std unknown-duration output growth; budget/backpressure/EOF/cancel tests pass |
| Pull/seek/release callbacks | [pull.rs](../../rust/src/pull.rs), [sparse_pull.rs](../../rust/src/sparse_pull.rs) | Sequential known/unknown and scheduled known-duration pull; exactly-once release and absolute-offset seeking; C callback ABI remains pending |
| Focus/requests/sparse ranges/scheduler | [sparse.rs](../../rust/src/sparse.rs), [scheduler.rs](../../rust/src/scheduler.rs), publication | Known-duration sparse overview/detail request policy, protection and replay integrated; oracle/review/combined acceptance below; musical focus/demand and S4 work/progress mapping integrated; twelve musical C scenarios pass |
| Context/static workspace/allocation classes | future runtime/FFI | Aggregate typed planning/atomic attachment and native std context quotas/lifetime; C layout/classes/custom callbacks pending |
| Immutable generations/pool/concurrency | session + [runtime](../../rust/runtime/src/lib.rs)/future FFI | Owned graphs, two core slots, unknown-duration sequential publication and safe independent Arc readers; safe standard-heap graph ownership/concurrent readers implemented; safe std growing sequential and owning sparse musical sessions/context lifetime implemented; C acquire/release pending |
| Resume/result seeding | publication/sparse/waveform | Validated owned overview checkpoint, source/fingerprint compatibility, inverse quantization and atomic native preflight; C tail difference documented below |
| External validated result builder | [builder.rs](../../rust/src/builder.rs), [native_validation.rs](../../rust/src/native_validation.rs), [owned_result.rs](../../rust/src/owned_result.rs) | Encoded subset plus native graph/provenance/session-state validation and deep ownership; C allocator/API boundary pending |
| S4 onset/BPM/local grid | [analysis.rs](../../rust/src/analysis.rs), session/sparse/publication | Default broadband analysis integrated; exact TEMP/LGRD C comparisons, focus and locking; experimental onset profiles pending |
| S6 global grid/dynamic tempo/revisions | [global_analysis.rs](../../rust/src/global_analysis.rs), session/sparse/publication | Default windows, dynamic grids, revision identities, locked conflict and explicit acceptance integrated; exact GGRD/REVN comparisons |
| Musical key/meter/downbeat | [key_analysis.rs](../../rust/src/key_analysis.rs), analysis | Default C profiles integrated; host-math MKEY and MTRD reference checks pass; portable key score rounding boundary remains open; original accuracy gates remain failed |
| Quality/confidence calibration | analysis/session/sparse/publication | BPM LUT/model 1867860160 integrated; exact CONF comparisons; fresh accuracy qualification remains open |
| POSIX/Windows file and WAV adapters | [wav.rs](../../rust/src/wav.rs) | Borrowed and constant-storage streaming WAV framing/block decode; filesystem ownership remains consumer-side |
| Analyze/inspect/validate/version/corpus tools | [wav_to_apta.rs](../../rust/examples/wav_to_apta.rs) | Waveform and default musical desktop modes; eight waveform/nine musical WAV smoke cases and exact C musical bytes; additive native analyze/inspect/validate/version/WAV batch commands; C CLI parity pending |
| Push/pull/installed/package/ESP examples | examples | Push/WAV-to-container example implemented; others pending |
| Direct native Rust consumers | portable Cargo + consumer-owned adapter | Experimental Pajoniiir lease-checked PCM worker, retained Deck generations, continuous multi-segment/global-cache transport and sparse/detail waveform windows; production/embedded gates remain separate |
| C API/ABI and frozen 1.0 consumers | future FFI | Deferred separate workstream; C remains installed product |
| CMake/pkg-config/shared/static packages | root build future FFI | Cargo additive only |
| Linux, Windows/MSVC, ILP32 | platform CI | x86_64 host tests; AArch64 core and i686/MSVC workspace compile checks; actual i686 SSE2 C/Rust execution established; default x87 differs; Windows linking/execution pending SDK environment |
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

### Historical continuation at pause: soft processing clock and waveform analysis

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

Continue from the latest feature-enabled overview seeding acceptance section below.
This bounded session stops at that milestone. Further onset/tempo/grids/key/meter/
confidence work needs a new task authorization. Preserve the pending
nonbounded runtime, C ABI/allocator/concurrency and platform acceptance items. New slices must pass focused
oracles, independent review and combined checks before acceptance. A safe single-thread RefCell pool
does not satisfy concurrent C acquire/release; retain that acceptance item until
the final allocator/FFI boundary is implemented and independently reviewed.

No frozen accuracy holdouts, push, issue, PR, release, hardware operation or
host-service change is authorized. Full C ABI/platform replacement remains
pending; no maintainer approval or completed rewrite is claimed.


## Historical paused checkpoint — 2026-10-03

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


## Waveform analysis continuation — 2026-10-04

The user resumed implementation, requested core work before the outside layer,
and authorized bounded parallel workers with lower-cost/low-reasoning settings.
The historical pause above no longer directs current work. No push or hardware
operation was performed. C remains the reference and installed product.

### Integrated scope

- Cooperative clocks preserve all four disabled-analysis boundary reads from
  the effective C wrapper chain, after successful waveform processing and before
  completion. Twelve public-C traces cover callback count/order, zero and saturated
  deadlines, chunk limits, source failures/WouldBlock and callback cancellation.
  Timed sequential pull adds four exact C traces, starting its deadline after
  source read/release; source callbacks remain outside the cooperative bound.
- Three-band caller storage attaches while Created to sequential/pull/sparse and
  published sessions. Persistent filters follow actual processing order across
  sparse scheduling. Sixty-three signal/rate cases compare float bits and complete
  quantized columns; a public-C sparse scenario verifies priority reversal,
  discontinuities, short EOF and retained published columns. Native result masks
  include three-band when columns carry it; the C bounded pool omits the bit.
  This explicit native difference does not affect column bytes. Band-enabled
  checkpoint seeding was Unsupported at this recorded milestone; the later
  feature-enabled overview seeding section supersedes that limitation.
- Sequential, pull and sparse sessions eagerly accumulate accepted detail PCM into four caller-owned
  cache tiles. Publication deep-copies tiles/columns into both preflighted result
  slots. Detail scratch requires four descriptors and 256 columns; aggregate
  retained-byte/column limits include worst-case overview plus detail capacity.
  A public-C trace checks eager detail output while only one overview frame is
  processed, then partial EOF tile geometry/state/columns. Kernel comparisons
  separately cover eviction/protection/pinned runs. Trusted native publication
  permits Completed + Partial detail, while external builder rules stay stricter.
  Sequential known/unknown-duration detail also matches two public-C traces and
  retains independent copied snapshots after session destruction. Sequential
  immutable publication also checks eager/EOF C output, retained-slot exhaustion,
  EOF rollback/retry and resource admission before accepting input.
- Attached sparse detail enables request/focus protection and aligned detail replay.
  Thirteen public-C scheduler traces check cache degradation/eviction, cancellation,
  partial-column skipping, EOF, priority/deadline/FIFO/aging, mixed-feature progress,
  retained slots and exact state/feature-mask/generation traces. Sixteen scheduled
  pull traces verify callbacks, source failures/cancellation, retained slots and
  detail output. As in C, public demands prioritize detail replay while the built-in
  pull loop asks for overview gaps. Simultaneous overview/detail publication uses
  C's overview changed-mask precedence; detail-only publication uses the detail bit.
  Failure-path comparisons preserve C's ordering of overview publication, aging,
  detail request refresh and detail-only publication. Effective CMake symbol
  renaming matters: public detail demand uses aged/deadline scores, but replay
  acceptance reselects by raw priority/FIFO without aging. This C inconsistency is
  preserved and directly tested; a public demand can therefore differ from the
  replay that C will accept. Native invalid-float preflight remains atomic.
- Combined allocation instrumentation covers band/detail processing, focus and
  overview requests, retained clones, slot exhaustion/retry, EOF tails and result
  lifetime beyond the session. A cache-eviction/replay path verifies zero allocation
  and unchanged overview columns/band history. Sequential band/detail push/pull
  also allocate nothing.
- The combined runner now compiles/registers clock, band, detail kernel and public
  detail-session, scheduler and scheduled-pull oracles. External-C comparisons
  also run in optimized Rust builds. `--jobs` defaults to two and bounds C build/CTest, Cargo
  build and Rust test concurrency. Generated evidence stays outside the checkout.

### Remaining core boundary

This is not completed core replacement. Feature-enabled overview seeding is now
accepted by the later section; restoring band/filter/detail internal state is not
part of the C seeding contract. Raw sequential sessions support caller-copied detail; sequential
and sparse publication support immutable slots.
Standalone `Scheduler::new` stays overview-only; attaching detail through the
complete sparse session enables the policy with its required cache. Onset/S4, S6/global/dynamic grids, native key/meter and
calibrated quality are still unported analysis stages. Nonbounded ownership,
concurrent acquire/release and full workspace/allocation-class contracts remain.
Only then can the outside C ABI, distribution, CLI and platform layer be replaced.


### Integrated verification

The required runner passed on 2026-10-04 with `--jobs 2`, using the unchanged
`c-baseline` build and external `combined` build root. Evidence:
`/home/shome/.local/share/libapta-audio/rust-rewrite/waveform-core-combined-check.log`.
This covers C Release, Rust debug/release and all registered external-C tests,
formatting, Clippy, no-default-features and eight strict C WAV interchange cases.
It establishes the integrated scope above, not the remaining core/outside gates.


Final integrated totals: **123/123 C tests**, **214 Rust tests** (including
unit/integration/doctests) and **27 external-C test groups**, in each Rust debug
and release profile. Format/Clippy/no-default-features, allocation instrumentation
and all eight WAV interchange cases passed. The final run includes the sequential
pending-detail-to-overview retry mask regression. `git diff --check` and notebook
validation also passed; changes remain local and no push was performed.


## Feature-enabled overview seeding — 2026-10-04

### Accepted contract and call order

This milestone closes the Unsupported guard for overview seeding with three-band
and/or detail attached. It does not introduce a richer checkpoint format or restore
internal analysis from published output. Create `PublishedSparseSession`, attach
caller-owned band/detail storage and publication scratch/slots, then seed the
validated `OwnedResult` while Created, before input acceptance. Repeat seeds only
while Created. A scheduled pull source wraps the seeded Created session before any
processing. Feature attachment after seeding remains rejected by the existing APIs.

Unchanged compiled public C calls establish that only overview peaks, RMS,
clipping and accepted ranges are restored. Overview quantization is reconstructed
using C's arithmetic, rather than copied verbatim. Checkpoint band bytes and flags
are discarded. With bands attached, seeded columns publish HAS_3BAND with zero band
bytes; band sums and filter history remain fresh until new overview PCM processing.
Checkpoint detail tiles are ignored even when attached detail is enabled: the cache
starts empty. Detail demand/replay can recover seeded regions without adding
overview samples, processed frames or band history. No checkpoint generation,
lineage, metadata, tempo/grid/key/meter/confidence evidence is installed. Seeding
retains Created and the current initial generation; it does not publish.

The oracle also established immediate EOF detail snapshot state: an already
complete resident run covering the final tile is Final in the Draining publication.
Native snapshot construction now observes signalled EOF without mutating cached
completion state, so publication exhaustion/rollback remains transactional. Invalid
floating PCM is preflighted before the first sparse state publication as well as
before working accumulation. Native rejects nonfinite PCM; C substitutes zero.

Preserved native/C differences: atomic native seed resource preflight where C may
partially mutate, clipped native transient seeded tail extents where C can exceed
EOF, and native band availability derived from column flags where C's bounded pool
omits the feature bit. Exact trace comparison normalizes only the last mask bit and
the documented C transient tail range; quantized columns, remaining masks, ranges,
generations, states and detail output compare exactly. Public replay selection
keeps aged/deadline ordering, acceptance keeps raw priority/FIFO, and automatic
scheduled pull keeps overview-gap selection. Overview publication takes precedence
over a pending detail changed mask.

### Coverage and evidence

- `seed_oracle.rs` / `fixtures/seed_oracle.c`: the original thirteen exact scenarios
  and native tail exception remain; the new matrix executes **20 scenarios in all
  four feature combinations (80 exact lifecycle comparisons)**. It covers sparse
  and full seeds, checkpoints with/without band output, ignored checkpoint detail,
  short final columns, repeated seeds, source/resolution/identity/EOF rejection,
  noncontiguous new PCM, fresh filters, detail replay into seeded ranges, retained
  slots/exhaustion/retry and changed-mask precedence. Opposite-polarity replay and
  subsequent PCM expose accidental filter mutation. Native tests additionally
  prove capacity failure atomicity, subsequent usability, nonfinite resume
  rejection and retained overview/detail after session destruction.
- `detail_pull_oracle.rs` / `fixtures/detail_pull_oracle.c`: the existing sixteen
  scenarios remain plus two feature-enabled seeded pull scenarios (prefix and
  sparse coverage). Public demand selects seeded detail while automatic reads
  select overview gaps. Exact callback ranges, releases, state/generation/mask
  traces and quantized overview/detail output compare against unchanged C.
- The existing isolated `result_allocation` counter includes feature-enabled
  seeded scheduled pull, overview resume, seeded-region detail replay and retained
  immutable publication. Its original eviction/replay path still runs separately
  inside the same test executable. No second allocation counter test was added.
- Existing seed, band, detail scheduler/pull, immutable-publication and allocation
  tests remain in the combined runner. No new oracle registration is needed:
  `rust/check.py` already runs both extended oracles in debug and release.

Baseline evidence:
`/home/shome/.local/share/libapta-audio/rust-rewrite/checkpoint-feature-seeding-baseline.log`.
Final required combined command (`--jobs 2`, external `combined` build root and
unchanged `c-baseline` library):
`/home/shome/.local/share/libapta-audio/rust-rewrite/checkpoint-feature-seeding-combined-check.log`.
The final combined runner passed: **123/123 C tests**, **216 Rust tests including
unit/integration/doctests**, and **28 explicitly enabled external-C test groups**
in each Rust debug and release profile. All 80 new feature-matrix comparisons
and the two additional scheduled pull cases run in both profiles. Format,
all-target Clippy, no-default-features, zero-allocation instrumentation and all
eight strict C WAV interchange cases passed. `git diff --check` and knowledge
validation passed. Tests previously accepted by the baseline remain included.

### Exact remaining boundary

Stop at this milestone. It accepts native overview checkpoint resume in
feature-enabled bounded sparse sessions through processing, detail replay,
scheduled pull and immutable publication. It does not establish full core or
outside replacement. Onset/S4, S6/global/dynamic grids, key/meter/calibrated quality,
nonbounded ownership, concurrent acquisition and workspace/allocation-class
contracts remain unfinished. C ABI, packaging, CLI and platform replacement follow
those core gates. C source, headers, container format and published ABI are unchanged;
no commit, push, issue/PR, deployment, service or hardware operation was performed.


### Subsequent publication authorization

After acceptance, the user authorized committing and pushing the preserved
waveform implementation and feature-enabled overview seeding work to
`origin/rust-rewrite`. The milestone boundary above describes the completed
implementation session; the user subsequently requested an unrestricted
continuation prompt for the remaining rewrite, with core integration before the
outside layer. Publication does not change the remaining acceptance gates.


## Integrated musical analysis continuation — 2026-10-04

This continuation supersedes the historical stop instruction above. It preserves
all waveform/seeding behavior and adds actual PCM-driven default C musical stages
through sequential/sparse processing, cooperative budgets, immutable publication
and native session serialization. C algorithms, headers, ABI and container version
remain unchanged. This is an integrated native software checkpoint, not full C
replacement or renewed algorithm-accuracy acceptance.

### Implemented behavior and caller storage

- [analysis.rs](../../rust/src/analysis.rs): S4 absolute-energy onset bins and
  positive broadband flux, frozen cooperative autocorrelation sweep, candidate
  ordering/relations, lognormal prior, fine refinement, phase fit, ambiguity,
  evidence/focus ranges and local Q32 grid. A scan fills once, evaluates four lags
  per step, then commits atomically. S6 proposal endorsement and metrical/close
  ensemble gates use the default C contract. Public focus and idempotent range
  locking are exposed on sessions and publication wrappers; lock publication
  failure restores the working grid/serial.
- [global_analysis.rs](../../rust/src/global_analysis.rs): S6 2048-frame bins,
  128-bin windows, adjacent tempo merging, bounded segments and explicit beats,
  degraded fallback, dynamic representation, signature/revision identities and
  previous IDs. Overlapping locked ranges conflict at >500 millibpm or >2048
  anchor frames; proposals become Pending. `apply_grid_revision` takes the first
  overlapping segment and updates locked local tempo/period/anchor, then marks
  Applied. Zero/wrong/repeated IDs preserve C errors. Acceptance precedes
  publication: slot exhaustion leaves the accepted revision applied and a later
  process call retries publication; repeated apply returns InvalidState.
- [key_analysis.rs](../../rust/src/key_analysis.rs): default C decimation,
  one-second Goertzel/chroma windows, log compression, Temperley major/minor
  templates, stable ordering, confidence and refresh cadence. Key consumes
  processed PCM; onset/S6 consume accepted PCM. Sparse seeks reset partial key
  windows without clearing accumulated chroma.
- Meter integrates beat strengths, 3/4 versus 4/4 phase selection and downbeat
  coordinates; publication follows S4/S6/key. Calibrated BPM quality preserves
  LUT/model **1867860160**, coverage permille and selected state. These remain
  the existing C algorithms, including their unresolved corpus failures.

Attach before input (and before overview seeding): `enable_tempo`,
`enable_global_grid`, `enable_key`, `enable_meter`, `enable_calibrated_quality`.
Meter and quality require tempo attached. S4 caller arrays each contain at least
4096 `OnsetBin`/f32 entries. S6 requires 16384 bin/flux entries and 3072 beats;
working segments are fixed at eight. Each publication slot needs three tempo
candidates, one local coverage/segment, one global coverage/eight segments/3072
beats, three key candidates, one meter segment and one quality record, as enabled.
Caller-owned working arrays and immutable slot arrays are distinct. Short working
arrays reject attachment. Publication wrappers preflight both slots and count
limits for each attached musical feature before initializing caller storage; an
eight-case test proves failure leaves Created/generation/working arrays intact
and waveform processing usable. Runtime byte-limit/publication failures remain
atomic.
The existing waveform workspace planner does not yet plan these music arrays or
provide aggregate retained-byte/workspace planning across enabled features.

Processing executes waveform/detail publication before analysis, then independently
publishes each changed musical stage. Publication exhaustion preserves retained
results and retries pending working output. Completion waits for onset/S6/key/meter
work, including budget-zero deferral. Sparse completed snapshots may contain
partial waveform coverage and Final musical views, matching C completion snapshots.
`result::from_session_result` serializes an immutable session result while keeping
external builder validation strict: native C snapshots can temporarily disagree
between S4 and S6, carry an older meter, tie candidate scores, select a revision
tempo absent from the old candidate list, or retain a completed Pending revision.
Per-section/range/resource validation remains required; arbitrary external imports
do not gain these trusted-session exceptions.

Two explicit native termination differences are covered: a final partial evidence
scan is consumed once rather than repeatedly restarted, and a rejected ensemble
proposal is not recharged against unchanged evidence/selection/proposal on every
one-step drain call. The unchanged C process can stall in these cases. New evidence
or proposals make ensemble work eligible again. Pull draining performs no further
source reads; each acquired block is still released once before processing.

### Verification and acceptance limits

[tempo_analysis.rs](../../rust/tests/tempo_analysis.rs) and its public-mutation
[C oracle](../../rust/tests/fixtures/tempo_analysis_oracle.c) compare exact TEMP,
LGRD, GGRD, REVN, MKEY, MTRD and CONF payloads. Seven S4 cases cover 8/44.1/48 kHz,
short evidence, ring wrap, partial EOF, budgets, focus and locking. Four S6 cases
include a changing tempo and a running lock with pending/applied revision errors.
Three key cases cover tonal major/minor and broadband input; three meter/quality
cases cover triple/quadruple accents and multiple rates. No numerical tolerance is
used. [musical_publication.rs](../../rust/tests/musical_publication.rs) adds a
whole eight-section container byte comparison against C, retained generations,
slot exhaustion/retry/destruction, sparse partial evidence and revision acceptance
before failed publication. [pull.rs](../../rust/tests/pull.rs) exercises both
known/unknown-duration musical one-step draining and exact read/release counts.
The original isolated session allocation test now also measures all musical stages;
its original paths remain. Core stays no_std, allocator-free and forbids unsafe.
The runner explicitly enables every new C oracle in both debug and release.

Baseline: `/home/shome/.local/share/libapta-audio/rust-rewrite/continuation-baseline.log`.
Final combined evidence:
`/home/shome/.local/share/libapta-audio/rust-rewrite/continuation-combined-check.log`.
The final source-state runner passed **123/123 C tests**, **222 Rust tests**
(unit/integration/doctests) and **33 explicitly enabled external-C groups** in
each debug/release profile. Formatting, all-target Clippy, no-default-features,
the original isolated allocation counters and eight strict WAV interchange cases
passed. AArch64 no-default-features compilation passed; evidence is
`/home/shome/.local/share/libapta-audio/rust-rewrite/continuation-aarch64-check.log`.
`git diff --check` and knowledge validation passed. No original coverage was removed.

### Remaining dependency gates and next work

1. Extend musical public-C lifecycle traces beyond final payload/container parity:
   requested/available/changed feature masks, generation scheduling, clock samples,
   cancellation and all publication failure stages. Native feature availability
   currently derives from attached result content (including confidence/dynamic/
   locking), while C also uses requested capabilities. This checkpoint does not
   establish full musical request-mask parity or exact intermediate generation
   traces. Wire bytes already compare exactly for the accepted cases above.
2. Route musical focus/request masks through sparse demand scheduling and prove
   scheduled-source musical refresh/replay behavior with public C traces. Direct
   tempo focus is supported; musical request-priority/deadline scheduling is not
   yet implemented. Overview seeding continues to ignore musical internal state.
3. Extend S4/S6 acceptance to weak/silent, sparse gaps, long ring replacement,
   ensemble promotion, beat/segment exhaustion, near-limit coordinates and
   optional experimental onset/key profiles. Default profile porting does not
   qualify experimental research or fix original accuracy failures.
4. Finish nonbounded ownership, concurrent acquisition/release, caller workspace
   layout/allocation-class contracts and an integrated feature planner. The native
   two-slot RefCell pool remains single-threaded and known-duration; standalone
   sequential/pull sessions support unknown duration within caller output capacity.
5. Then replace C API/ABI, allocator callbacks, packaging, frozen consumers,
   analyzer/inspect/validate/version/corpus tools and platform software integration.
   The existing desktop example remains waveform-only. No partial Rust shared
   library is published under C's product ABI. Windows/ILP32/ESP-IDF and physical
   hardware acceptance remain open. No services or hardware were operated.

The accepted code is preserved for successive continuation. Start from live Git,
these source-linked remaining gates, and the final combined evidence; do not
restart waveform work or infer whole-library completion from the synthetic matrix.


## Musical lifecycle and runtime continuation — 2026-10-04

This continuation supersedes the preceding next-work descriptions for the
implemented slices below. C source, published headers, container format and
installed product remain unchanged. No full replacement, original algorithm
accuracy or physical qualification claim follows.

### Implemented integrated behavior

- Sparse musical request/focus masks route through the existing scheduler. Public
  PCM queries translate S4/global/key/meter targets to overview gaps. Effective
  C S4 wrappers additionally score and refresh BPM/local/locking requests as
  overview work. Key/global-only progress remains queued, matching C. Automatic
  scheduled pull deliberately calls the internal overview selector without the
  public musical mapping. Existing detail replay priorities remain intact.
- Explicit `ResultPool::new_with_requested_features` validates C feature
  dependencies, projects unrequested payloads, and uses requested capabilities
  for confidence/dynamic/locking availability. Quality stays hidden until EOF.
  Ordinary native publication retains content-derived availability. Attachment
  rejects unrequested stages before mutation; locking validates empty ranges
  before returning Unsupported for an absent requested capability.
- Effective wrapper order is waveform → S4 → S6 → meter → key, established from
  CMake compile definitions and public traces. Clock checks occur after lag
  groups/windows, not scan initialization or commits. A frozen scan can commit
  and follow newer evidence within the same call after the last clock expired,
  provided work steps remain. Final partial scans and rejected unchanged
  ensemble attempts still retain the accepted native termination differences.
- `publication::plan_features` covers optional band/detail/default musical
  working arrays, both slots' maximum typed graph counts and retained bytes.
  Shared `owned_result::retained_size` avoids separate byte-accounting formulas.
  Sequential/sparse attachment accumulates all attached features and checks
  aggregate counts/bytes plus both slots before initializing caller arrays.
  Native S4 reserves local arrays even for a BPM-only projected capability.
  Queue/control bytes and caller capacity beyond referenced data are excluded;
  this is native planning, not C ABI offsets or allocator-class equivalence.
- Unknown-duration sequential publication plans against caller column capacity.
  EOF resolves working/result duration atomically, rolls back on exhausted-slot
  failure, and retries without corrupting retained unknown-source generations.
  Partial capacity acceptance and empty EOF are tested. Invalid floating PCM is
  now preflighted before the first Running publication as well as core mutation.
- `OwnedResult::copy_to` deep-copies trusted immutable graphs, preserving native
  capabilities, into independent typed storage after complete capacity preflight.
  The separate std `libapta-runtime` crate forbids unsafe code and integrates
  sequential/sparse processing with `ConcurrentResults`. Readers acquire Arc
  generations under a short RwLock; independent snapshots do not pin core pool
  slots. Standard allocator control blocks are explicit; caller-owned graph
  arrays and their lifetimes remain enforced by Rust. Short copies can retry
  just the mirror; committed cancellation is mirrored before returning its error.
- The std runtime also implements `HeapResult` and `HeapResults`. Complete native
  graphs, metadata (including opaque application identity), provenance and masks
  are copied into independent standard-heap arrays without unsafe code or source
  lifetime references. Copies use fallible Vec reservation and enforce aggregate
  retained *capacity* bytes/counts; control Arc/RwLock allocations are separate.
  Concurrent readers retain any number of generations independently of caller
  arrays/session/core pool. Actual sequential/sparse processing mirrors through
  the same publication lifecycle; heap limit failures retry only the mirror.
  Trusted source-bounded meter/grid exceptions are retained exactly. `view()`
  exposes a native graph; generic external conversion still has strict external
  validation, so heap ownership does not widen serialization acceptance.
- `wav_to_apta --music` advances the desktop consumer into the integrated default
  musical graph using planning/publication/serialization. Existing waveform
  behavior and create-new output protection remain. It is not a C CLI replacement.

### Reference-derived trusted snapshot boundaries

Public compiled C scheduled-source traces establish that focus-limited meter
snapshots may contain a source-bounded downbeat outside segment applicability.
Unknown-duration EOF can leave a grid's *requested* range rounded past the final
partial bin. Trusted native session validation accepts those exact values;
external builder rules remain strict, and evidence/applicability/source downbeat
bounds remain checked. No clamping, mask normalization or tolerance hides these
cases. Wire conversion/writers keep their own validation: every trusted snapshot
is not thereby promised strict-interchange acceptance. The complete desktop
container fixtures pass both native parsing and the strict C validator.

### Acceptance and evidence

`rust/tests/fixtures/musical_lifecycle_oracle.c` mutates only public C APIs.
`rust/tests/musical_publication.rs` compares 53 profiles' exact generation, state,
available/changed masks, tempo values/states, clock counts, meter coordinates,
global flags/counts and source read/release coordinates. Profiles cover S4-only
and all default stages with confidence/locking on/off; expired/resumed clocks;
retained initial exhaustion/retry; cancellation; scheduled musical focus;
known/unknown EOF; silence, near-zero and DC; long ring replacement; and changing
tempo. This is a defined matrix, not every feature combination or failure stage.

`rust/tests/scheduler_oracle.rs` compares twelve musical scenarios across
S4/local, key, global and mixed key/tempo requests, plus the preserved six
waveform scenarios. Exact request masks, ranges, priorities, progress, deadline
processing order, cancellation and overflow/EOF behavior are checked.
`rust/tests/tempo_analysis.rs` extends integrated global TEMP/GGRD/REVN bytes to
long ring replacement, 3072-beat exhaustion and eight-segment degraded output.
The shorter changing-tempo fixture establishes five-segment behavior separately.
No new numerical tolerance is used.

Native atomic aggregate preflight tests enumerate all 4096 feature masks and
exercise cumulative attachment order/byte limits, preserving sentinel working
buffers on failure. Unknown-duration publication tests cover short output,
invalid PCM, retained source identity and EOF retry. Eight safe runtime tests exercise two participating threads, caller and heap
retained generations across 16 actual PCM publications, storage/session/channel
destruction, sparse gap generations, full default musical graphs, metadata/detail/
opaque identity/provenance, count/byte limit boundaries, mirror-only retry,
source/generation conflicts and cancellation. All caller arrays go out of scope
before heap generations are inspected or moved to another thread.
The complete musical container is deep-copied and reserialized after session and
pool destruction. Existing isolated allocation counters remain separate from
the deliberately allocating std runtime tests.

Baseline evidence: `/home/shome/.local/share/libapta-audio/rust-rewrite/continuation-music-baseline.log`.
Final integration evidence: `/home/shome/.local/share/libapta-audio/rust-rewrite/continuation-music-combined-check.log`.
Final source-state acceptance: **123/123 C tests**, **233 ordinary Rust tests**
(including eight std runtime tests) and **35 explicitly enabled external-C groups**
in each debug/release profile. Formatting, all-target Clippy, no-default-features,
isolated allocation counters, eight waveform and nine musical WAV cases pass.
AArch64 portable no-default-features compilation passes; evidence:
`/home/shome/.local/share/libapta-audio/rust-rewrite/continuation-music-aarch64-check.log`.
`git diff --check` and knowledge notebook validation also pass.
The runner registers the new C oracle and explicitly executes ignored groups in
both debug and release. Eight waveform and nine musical WAV interchange cases
include overwrite protection and one complete desktop/C byte comparison.

### Remaining dependency gates

1. Complete musical lifecycle request combinations (including BPM-only, key-only
   and capability projection across all stages), public C traces of all-stage
   publication failure/retry, locking/revision exhaustion and integrated sparse
   musical detail replay. Existing native lock/revision failure tests are retained;
   their complete public-C lifecycle matrix is not established by these tests.
2. Extend sparse musical evidence and near-limit coordinate acceptance, ensemble
   promotion variants and optional experimental profiles selectively. Synthetic
   default-profile parity does not qualify original rejected accuracy gates,
   optional research or spent holdouts.
3. Finish dynamically growing nonbounded session workspaces/context lifetime and
   allocator callbacks/classes/C workspace layout. Native heap-owned retained
   graphs and reader lifetimes are implemented using the standard allocator. The portable two-slot pool is
   still single-threaded; the std caller/heap copy boundaries establish native
   concurrent readers, not C ABI acquire/release or custom allocator compatibility. Unknown
   duration remains constrained by explicit caller output capacity.
4. Implement the audited unsafe C API/ABI boundary, static/shared CMake/pkg-config
   packaging and frozen consumers. Complete native analyzer/inspect/validate/
   version/corpus interfaces; the musical example covers only its documented WAV
   mode. Existing C package/consumer tests validate preserved C, not a Rust ABI.
5. Establish Windows/ILP32/ESP-IDF software/platform acceptance and physical P4
   qualification. A portable AArch64 compile is not platform/hardware acceptance.
   No services, deployment or hardware were operated.

## Owning runtime, late failure ordering and desktop continuation — 2026-10-04

This continuation extends the preceding accepted checkpoint. C algorithms,
published headers, ABI and container version remain unchanged. Work used one
implementation agent and two build/test jobs.

### Integrated core and runtime

- Existing `Session`, `Analysis` and `GlobalAnalysis` accept owning array storage
  through defaulted generic storage types. Borrowed constructors/callers remain
  intact. Core remains `no_std`, allocation-free and unsafe-free; no replacement
  session implementation or self-referential owner is introduced.
- Transactional queue replacement retains FIFO order across ring wrap. Overview
  growth retains written columns, partial accumulators, queued PCM and musical
  history. Band capacity/detail coordinates are checked before replacement writes.
  `SessionSnapshot` is an opaque graph obtained only from actual processing. It
  supports trusted caller copying and session-to-wire conversion without granting
  arbitrary external inputs trusted validation exceptions. Detail/metadata use
  their existing separate interfaces.
- Safe std `GrowingSession` owns queue/output and all default musical arrays.
  Unknown duration grows overview online; known duration reserves overview up
  front. Queue/output replacement allocations and aggregate capacity-byte limits
  are preflighted before either replacement commits. All five musical arrays
  allocate before attachment. Ring/beat/segment caps preserve default C policy.
  Heap snapshots outlive every working array/writer and support Arc readers.
  Failed snapshots/context quotas preserve latest; `refresh` retries only the
  mirror, and further mutations are blocked until it succeeds. Accepted PCM and
  completed processing must be inspected after such an error.
- `RuntimeContext` serializes registration/closure, tracks owning writers and
  actual retained heap-graph capacities, and returns Busy until writers, current
  channels and acquired graphs are released. Graph leases drop after payload
  arrays. Logical close is permanent across cloned context handles. Quotas cover
  committed graph headers/arrays, not peak process memory, transient copies,
  mutable workspaces, or standard Arc/Mutex/RwLock control allocations. This is
  native context lifetime/accounting, not C allocator/layout/allocation classes.

### Public C contracts established and corrected

The lifecycle oracle now checks **75 profiles**, adding BPM-only, key-only,
BPM/global without local grids and calibrated BPM without local grids, with
clock, exhaustion, cancellation and unknown-duration variants. Local flags,
applicability and revision identities/states join the exact discrete trace.
Two late Draining profiles establish S4-only/all-stage lock publication
exhaustion, rollback, successful retry and idempotence.

BPM-only bounded publication omits the local payload but reports the S4
LOCAL_BEATGRID change bit. Unknown-duration C nonbounded publication retains the
S4-derived local grid. Explicit requested-mask publication now preserves those
measured distinctions; ordinary construction still derives native availability.
This compatibility projection compares known bounded C and unknown nonbounded C;
it is not a general known-duration C heap-mode emulation.

A new public mutation oracle establishes running locked revision exhaustion.
Applied working state survives failed acceptance publication; repeated apply
returns InvalidState. Retry publishes S4 before S6, and a retained newer reader
can exhaust S6 again after S4 succeeds. The previous native combined retry was
incorrect. Sequential and sparse publication now acknowledge musical mutation
serials only for their stage's successful publication. Waveform snapshots retain
existing detail acknowledgement, including state-only EOF. Old readers remain
immutable throughout the trace.

Unknown-duration S6 now receives resolved EOF through the same transactional
source-duration setter used by publication rollback. Its requested range matches
C's final known length. Reference-derived S4 requested-range rounding and trusted
meter exceptions remain preserved.

### Numerical backend boundary

The broader runtime fixture exposed a portable key discrepancy: uniform 120 BPM
impulses, 8 kHz, 320000 frames, amplitude 0.75 produce portable selected score
**55734** versus host C **55735**. The retained diagnostic reports exact coefficient,
chroma and candidate comparisons; no tolerance, clamping or changed fixture hides
this failure. Existing portable default behavior is preserved.

`KeyMath` is an explicit fixed numerical backend. The portable default is libm;
std owning sessions select platform f32 cosine/log/square root. The unchanged C
math oracle uses public session processing and test-only internal arithmetic
observations, then public key access. Platform coefficients, chroma and all three
encoded candidate scores compare exactly on this host. Complete owning-runtime
containers compare exactly against public C for known duration, partial unknown
EOF and changing tempo. This qualifies those host profiles, not universal
portable libm equivalence or another platform's math backend. Original accuracy,
rejected optional profiles and spent holdouts remain unchanged.

### Native outside consumer

`rust/runtime/src/bin/apta-native.rs` implements actual native analyze, inspect,
validate, version and local WAV corpus commands through owning analysis,
context lifetime and trusted serialization. Waveform and default music are
supported; strict native/C validation, all recognized section summaries,
create-new output protection, deterministic WAV ordering and retained successful
batch outputs are tested. Inputs are limited to 256 MiB. This additive command
surface is not C tool option/output parity or a replacement for fingerprinting,
metadata, JSON qualification exports and frozen privacy/corpus tooling. C remains
installed; CMake/pkg-config/static/shared ABI packaging is unchanged.

### Acceptance and evidence

Evidence root: `/home/shome/.local/share/libapta-audio/rust-rewrite/`.

- `continuation-runtime-baseline.log`: unchanged starting suite.
- `continuation-runtime-combined-check.log`: final **123/123 C tests**, **244
  ordinary Rust tests** and **40 explicitly enabled external-C groups** in each
  debug/release profile; formatting, all-target Clippy, no-default-features and
  isolated core allocation counters pass. The runner registers all new oracles.
- Eight waveform/nine musical example cases plus eight waveform/nine musical
  native CLI cases pass strict C interchange/overwrite protection. Exact complete
  runtime containers and command tests include batch failure retention and corrupt
  input rejection. Small retained artifacts: `combined/smoke-native-musical.apta`,
  `combined/smoke-musical.apta`, `combined/smoke-waveform.apta`.
- `runtime-allocation-tests.log`: isolated injection into both queue/output
  replacement allocations, both early snapshot provenance arrays and all five
  musical workspace allocations. Preflight failures commit no PCM/stages;
  snapshot failures retain committed accepted PCM and retry only the mirror.
  Standard Arc/lock control allocation failures are deliberately not injected.
- `continuation-runtime-asan-check.log`: **22 runtime tests**, including all three
  runtime external-C groups, pass AddressSanitizer with leak detection using the
  available compiler's sanitizer flag via `RUSTC_BOOTSTRAP=1`. This instruments
  native Rust/runtime tests, not the unchanged C reference or a rebuilt std.
- `continuation-runtime-aarch64-check.log`: portable no-default-features AArch64
  compilation. `continuation-runtime-key-backends.log` retains the portable
  discrepancy and exact host backend acceptance. Focused context/revision/CLI
  traces are in `runtime-context-oracle.log`, `runtime-revision-failure.log` and
  `runtime-tools-tests.log`. Diff and knowledge notebook validation pass.

### Remaining work and next executable handoff

1. Extend the all-stage publication failure matrix beyond late lock and running
   S4/S6 revision traces; integrate public C musical/detail replay evidence,
   near-limit sparse coordinates and ensemble promotion variants. Native all-stage
   sparse processing is retained, but its complete combined detail matrix is open.
2. Extend owning sessions into band/detail, sparse/pull and requested-capability
   mutation paths. Known-duration owning output is preallocated. Establish broader
   portable numerical/backend acceptance before claiming universal exact scores.
3. Implement full C context/custom allocators, allocation classes, C workspace
   layout, allocation failure ordering and concurrent ABI acquire/release. Native
   standard-heap controls remain subject to the standard allocator contract.
4. Implement/review the unsafe ABI boundary separately, static/shared
   CMake/pkg-config packaging and frozen consumers. Finish C-compatible analyzer
   options, inspection/JSON/metadata/fingerprint and frozen corpus interfaces.
5. Establish Windows/MSVC, ILP32 and ESP-IDF software acceptance plus unchanged
   original DSP accuracy and physical P4 gates. AArch64 compilation and host ASan
   are not those platform or hardware qualifications. No service, deployment or
   hardware operation occurred.

Resume from this section and the current source/checklist, inspecting Git live.
Run the final combined command with two jobs before new behavior. Preserve C and
all accepted native differences; do not infer complete replacement from counts.

## Owning waveform, detail and source continuation — 2026-10-04

This continuation adds owning waveform features, sequential sources and their
native desktop consumer. It preserves the previous lifecycle/numerical contracts;
C algorithms, public headers, ABI, format and installed product are unchanged.

### Integrated behavior

- `Session` accepts owning or borrowed band/detail storage within its existing
  generic storage architecture. `DetailCache::new` retains its borrowed API;
  `with_storage` supports owning arrays and uses exactly the reference four
  tiles even when extra storage is supplied. Core stays safe, allocator-free
  and `no_std`.
- Band storage replacement preserves accumulated partial/completed sums and
  continuous filter history, zeroing the new tail. `GrowingSession` allocates
  queue/output/band replacements before committing any of them and counts their
  actual Vec capacities together with detail and all musical workspaces.
  Allocation failure before acceptance leaves PCM, capacities and stages intact.
- Actual `SessionSnapshot` includes eager detail using fixed inline scratch for
  four tile descriptors and 256 columns. Trusted heap copies and serialization
  include that graph. Metadata ownership remains separate. Snapshot stack size
  is consequently larger, including for callers without detail enabled; embedded
  stack sizing remains part of platform qualification.
- Owning detail follows the four-tile reference eviction policy. The current
  graph holds resident runs only; older acquired graphs retain evicted data.
  Tests cover partial EOF, growth during partial overview accumulation, continued
  filter history, cache eviction, retained generations and known/unknown duration.
- `GrowingPullSession` owns a configured Created writer and `PullSource`. It
  accepts at most one block/256 frames and processes one step per call. Known
  EOF requires no extra read; unknown EOF resolves the length. WouldBlock retries;
  malformed blocks/source errors/cancellation are terminal. Every acquired block
  releases once before processing. Mirror/resource-limit errors retry the mirror
  first and drain already accepted PCM before another source read. Failed working
  allocation can rerequest the same absolute offset; this is native retry behavior,
  not C custom-allocator failure compatibility. No mutable writer escapes the
  adapter. Sources are recovered with `into_inner`.
- Owning push/pull expose cooperative clocks through core processing. Pull starts
  its deadline after read/release. Source callbacks and allocation/heap publication
  are outside the processing deadline. Musical draining does not read more PCM.
- `apta-native analyze` and `corpus` accept independently combinable `--bands`,
  `--detail` and `--music`. Detail exports resident cache tiles rather than complete
  track detail. Duplicate/unknown flags fail before creating output; create-new
  protection and retained successful corpus outputs remain in force.

### Acceptance boundary and evidence

Evidence root: `/home/shome/.local/share/libapta-audio/rust-rewrite/`.
The unchanged starting suite is `continuation-next-baseline.log`; the intermediate
integration run is `continuation-next-integration-check.log`.

- `continuation-next-combined-check.log`: **123/123 C tests**, **250 ordinary
  Rust tests and 43 explicitly enabled external-C groups** in each debug/release
  profile. Formatting, all-target Clippy, no-default-features and isolated
  allocation instrumentation pass. The existing 34 strict WAV example/CLI cases
  pass, plus two new band/detail CLI combinations checked by the C reader.
- `continuation-next-asan-check.log`: **31 runtime tests**, including six
  explicitly enabled external-C groups, pass host AddressSanitizer/leak checks.
  This uses `RUSTC_BOOTSTRAP=1 RUSTFLAGS=-Zsanitizer=address`, targeting
  `x86_64-unknown-linux-gnu`, with two build/test jobs and all runtime ignored
  groups enabled. It does not instrument the C reference or rebuild std.
- `continuation-next-aarch64-check.log`: portable `libapta --lib
  --no-default-features --target aarch64-unknown-linux-gnu` compilation passes.
- `git diff --check` and knowledge notebook validation pass. Earlier evidence
  remains preserved. The failed new silence assertion is retained in the first
  final-run attempt log; the complete runner was rerun after correcting it.

Existing unchanged public C band/detail oracles are reused by runtime ignored
tests and enabled in both profiles by `rust/check.py`. Known/unknown growing
bands compare every quantized column field exactly against both C publication
profiles; the documented bounded-C missing band capability remains unchanged.
Eager detail compares exact public C tile coordinates, states and columns before
and after EOF. Two new all-feature CLI outputs pass the strict C reader, including
music with bands/detail. These are content/interchange comparisons, not C heap
intermediate-generation or custom-allocator equivalence.

The isolated allocation test injects failure into each of queue/output/band growth
and band/detail attachment, in addition to previous snapshot/music injections.
Owning pull tests verify committed-snapshot retry without rereading/releasing PCM,
terminal malformed blocks/cancellation, WouldBlock, known/unknown EOF and no reads
while draining. Clocked owning processing compares core callback counts and exact
processed columns, including mirror failure without repeated processing. Silence
intentionally produces no selected key; a mistaken new test expectation was fixed
without changing its signal or algorithm (`continuation-next-combined-attempt1.log`).

### Remaining dependency gates

Continue with public C all-stage failure/replay and capability/mutation matrices,
near-limit sparse coordinates and ensemble variants. Owning sparse scheduling,
requested-capability projection and musical mutation APIs remain open. Owning
sequential source acceptance does not qualify scheduled sparse pull or C callback
ABI. Broader portable/backend numerical qualification remains open: the portable
55734 versus host-C 55735 selected-key score remains explicitly unresolved.

Full C context/custom allocator classes, workspace layout/failure ordering,
concurrent ABI acquire/release, separately reviewed unsafe boundary, CMake/pkg-config
static/shared packaging and frozen consumers remain open. Native tool options are
additive; complete C options/JSON/metadata/fingerprint and frozen corpus interfaces
remain open. Windows/MSVC, ILP32, ESP-IDF, original accuracy and physical P4 gates
remain separate. No deployment, host-service change or hardware operation occurred.

## Owning sparse scheduling, replay and mutation continuation — 2026-10-04

This continuation extends the owning waveform/source handoff above. C algorithms,
headers, ABI and container format remain unchanged. All work used one agent and
two build/test jobs. The native implementation remains additive.

### Integrated behavior

- `SparseSession` accepts generic borrowed or owning arrays through
  `SparseStorage`/`with_storage`; `Workspace` and `new` preserve the borrowed API.
  `Scheduler::with_storage` similarly owns or borrows up to sixteen slots.
  Portable core remains `no_std`, allocator-free and unsafe-free. Generic storage
  uses the same processing, priority/deadline/FIFO/aging, protection and replay
  implementations; no second sparse engine was introduced.
- `SparseSession::snapshot_graph` produces trusted actual sparse overview,
  resident detail and musical graphs, using the existing inline detail scratch.
  Sequential and sparse snapshots preserve their different overview spans.
  The earlier snapshot-stack/embedded qualification boundary remains open.
- `OwnedSparseSession` owns known-duration sparse storage, scheduling, bands,
  four-tile detail and default music. Initial allocation and feature attachment
  preflight minimum and actual Vec-capacity bytes. Queue nodes, range slots and
  request slots are fixed configured capacities, not dynamically grown tables.
  Limit/backpressure behavior remains explicit. RuntimeContext accounts for the
  writer and committed retained graphs; resource leases still drop last.
- Owning sparse processing supports focus/requests, cancellation, cooperative
  clocks and shared waveform/S4/S6/meter/key budgets. Immutable retained results
  survive processing and writer destruction. Mirror failure can follow committed
  PCM/detail/music; it blocks further mutations until mirror-only refresh succeeds.
  Scheduler progress is committed with native work, before heap mirroring. This
  is not C's intermediate bounded-slot generation/failure schedule.
- Detail replay preserves overview columns, band history, musical evidence and
  processed-frame counts. Old results preserve evicted tiles. `seed_from_result`
  copies only validated overview evidence while Created, after feature attachment;
  it restores no detail, band history, music, provenance or generation. Seed itself
  publishes nothing. Native writers have no fingerprint; required identity fails.
- `OwnedScheduledPullSession` drains accepted work and retries mirrors before
  reading. It performs at most one read of up to 4096 frames per call, bounded by
  the input budget. Acquired blocks release exactly once before processing and
  timing. WouldBlock is retryable; source errors, malformed blocks and premature
  EOF are terminal. Reported source length must match the configured known length.
  Automatic reads preserve the internal overview selector, while public demand
  exposes detail replay. A satisfied selected target can finish with other holes.
- Both owning writers and pull adapters expose tempo focus, grid locking and
  revision acceptance. Failed lock publication restores the working lock;
  idempotent locks publish nothing. Failed revision publication retains Applied
  state, blocks other mutations, and retries only the mirror. C's separate S4/S6
  intermediate retries remain covered by the existing bounded-publication path.
- Requested-capability projection is configured before feature attachment and
  shares payload/capability rules with ResultPool. Default music may attach a
  superset of stages; output is projected and dynamic S6 follows the requested
  bit. Unknown-origin BPM retains its derived local grid even after resolving EOF.
  Ordinary native content-derived masks remain unchanged.

### C-derived boundaries and evidence

The requested projection profile is **bounded known-duration C**, and
**nonbounded initially unknown C**, as in the preceding lifecycle acceptance.
A new attempt against nonbounded *known* BPM-only C exposed its extra derived
LGRD, preserved in `continuation-sparse-projection-oracle.log`. The explicit
profile comparison uses bounded known C and passes without dropping/normalizing
wire sections. Native projection does not claim nonbounded-known equivalence.
`continuation-sparse-projection-profile-check.log` compares eight complete wire
containers across four masks and known/unknown duration. Payloads and bytes are
exact; no numerical tolerance was added.

- The lifecycle oracle now covers **83 profiles**, adding eight EOF drain profiles
  that retain the current result at each call and force successive stage
  publication failures across base/all and projected feature combinations.
  Masks, generations, state and musical coordinates match public C exactly.
  Focused evidence: `continuation-sparse-failure-matrix.log`.
- `continuation-sparse-oracle-check.log` compares sparse eager detail before/after
  partial EOF and one integrated all-stage/bands/detail-replay container exactly
  against public C mutations. This extends replay content acceptance; it does
  not prove every bounded musical/detail intermediate-generation combination.
- `continuation-sparse-source-oracle.log` compares five owning scheduled-source
  scenarios against C: focus movement, WouldBlock/short blocks, queue draining,
  explicit requests and partial selected-range completion. Every callback count,
  offset, requested size, status, available mask, span and quantized column is
  exact. Native heap generation and changed-mask identities are excluded explicitly.
- `continuation-sparse-coordinate-check.log` adds near-u32/u64-limit public
  ranges, deadlines, request IDs and invalid PCM coordinates. This is validation
  and clipping evidence on a small source, not huge-workspace qualification.
- Isolated allocation injection covers all seven sparse working-array allocations,
  initial provenance allocations, five musical attachments, bands and detail.
  Failures leave preflight state intact. Native tests also cover context quota
  mirror failure, retained threaded readers, source mirror retries, lock rollback,
  revision acceptance, cancellation, fixed queue/range exhaustion and seed/resume.
- Optional C profiles were inspected selectively through CMake definitions and
  the effective wrapper renaming. Default cache experiments remain OFF; no
  experimental onset/key/meter profile or original DSP accuracy gate is qualified.

All evidence below is under
`/home/shome/.local/share/libapta-audio/rust-rewrite/`:

- `continuation-sparse-baseline.log`: unchanged combined baseline.
- `continuation-sparse-integration-check.log`: passing intermediate combined run.
- `continuation-sparse-combined-check.log`: **123/123 C tests**, **262 ordinary
  Rust tests and 47 explicitly enabled external-C groups** in each debug/release
  profile. All 34 existing strict WAV cases and two band/detail CLI combinations
  pass. Formatting, all-target Clippy, no-default-features and isolated allocation
  instrumentation pass. `git diff --check` and notebook validation also pass.
- `continuation-sparse-asan-check.log`: **47 runtime tests**, including **10
  external-C groups**, pass host AddressSanitizer/leak checks, with `RUSTC_BOOTSTRAP=1`,
  `RUSTFLAGS=-Zsanitizer=address` and x86_64 target. C/std are not instrumented.
- `continuation-sparse-aarch64-check.log`: portable core/no-default-features compile.
- `continuation-sparse-ilp32-compile.log` and `continuation-sparse-msvc-compile.log`:
  all workspace targets compile for i686 Linux and x86_64 Windows/MSVC. These
  `cargo check` runs do not link executables, execute tests or qualify the C ABI.

### Remaining dependency gates

Continue the public C failure/mutation/replay matrix beyond these fixtures,
ensemble-promotion variants, long-coordinate capacity and optional-profile
qualification. Sparse owning tables currently have fixed capacities; unknown
sparse duration, dynamic range/node growth and fingerprinted source identity are
not implemented. Broader cancellation/resource races and C source callbacks remain.

Full C custom allocators, allocation classes, workspace layout/failure ordering,
concurrent C acquire/release, separately reviewed unsafe ABI, static/shared
CMake/pkg-config packaging and frozen consumers remain open. Native tool options,
JSON, metadata/fingerprint and frozen corpus interfaces remain incomplete.
Windows/MSVC and ILP32 execution/linking, ESP-IDF Rust software integration and
embedded snapshot-stack sizing remain open despite cross-compilation. Original
DSP accuracy and physical P4 qualification remain separate. Portable libm's
selected-key score **55734 versus host C 55735** is still unresolved; the explicit
std KeyMath backend matches accepted host fixtures only. No deployment, service
change or hardware operation occurred.

## Sparse capacity, source identity and consumer continuation — 2026-10-04

This continuation preserves the preceding sparse/runtime/music contracts. C
algorithms, public headers, ABI and container format are unchanged. Work used
one implementation agent with two build/test jobs. The starting checkout was
verified clean at the supplied published checkpoint before the combined baseline.

### Integrated behavior

- `SparseSession::replace_pending_storage` transactionally replaces caller-owned
  or owning range/node/PCM arrays in the existing core engine. Dimensions are
  checked before copying; occupied node indices, partial processing positions,
  serials and PCM stay intact, and new nodes start empty. Core stays safe,
  allocator-free and `no_std`.
- `OwnedSparseSession::reserve_pending` explicitly grows those arrays while
  Created/Running. It preserves default fixed-capacity push backpressure until
  the caller reserves more. All three replacements allocate before commit;
  minimum and actual Vec-capacity bytes are checked against the aggregate
  working limit. It does not shrink, publish, mutate scheduler progress, or grow
  request slots. Transient copies are outside the committed working-byte limit.
  A dirty mirror still blocks reservation and other mutations.
- Scheduled pull exposes the same reservation. A zero accepted block now reports
  retryable BufferTooSmall instead of silent zero-progress success. The block
  releases once; no source failure is recorded. Reservation permits retry at the
  same unaccepted offset. Accepted work/mirror retries still precede new reads.
- Validated `session::SourceIdentity` represents absent, application-opaque or
  SHA-256 source-object identity. Core sessions configure it before input/seed;
  owning writers and RuntimeContext accept it at construction, before publishing
  the initial immutable graph. Identity is fixed for an owning writer, survives
  unknown EOF, and travels through trusted snapshots, retained heap graphs and
  wire output. The host supplies bytes; no hashing or digest verification occurs.
- Owning seed now enforces the C identity policy: present identities must match
  kind and all 32 bytes; required identity rejects either missing side. A missing
  identity is otherwise permitted. Seed remains overview-only, Created-only,
  transactional, and does not publish or import provenance/music/detail/history.
- `apta-native analyze` accepts one `--source-identity=opaque:HEX` or
  `--source-identity=sha256:HEX` (64 hexadecimal digits), and inspect displays it.
  Invalid/duplicate flags fail before output creation. Corpus rejects this
  per-file option. Existing output-preservation and feature-combination rules
  remain intact. Automatic hashing, metadata and complete C tool parity remain open.

### Exact reference and failure evidence

- `source_identity_oracle.c` uses actual public C processing to create checkpoints,
  then seeds and resumes another writer. The runtime matrix covers 72 combinations
  of known/unknown checkpoint duration, absent/opaque/SHA-256 identities, equal/
  unequal bytes and required/optional policy. Statuses and successful complete
  containers compare exactly. Native intermediate heap generations are not C
  bounded-slot generations and are excluded; wire output is not normalized.
- `sparse_capacity_oracle.c` processes 63 and 4,096 disjoint fragments, then fills
  every hole. The larger case processes 524,288 frames, grows through the default
  4,096-range boundary to 4,097 slots, merges to one range and compares the complete
  final container exactly with public nonbounded C. This is real capacity/work
  evidence, not near-u64 coordinate validation or maximum-address-space acceptance.
- Eight variable-tempo lifecycle profiles extend the existing exact public C
  generation/capability matrix to 91 profiles: projections, retained slots,
  cooperative timing, unknown EOF and all-stage drain exhaustion. These exercise
  ensemble proposal/lifecycle combinations; they do not claim every possible
  promotion signal or original tempo accuracy qualification.
- Allocation injection fails each of the three pending-array replacements before
  commit, then retries with partially processed PCM. Native tests compare the
  complete all-stage/band/detail output with a preallocated writer, exercise
  fragmented backpressure, no-op/overflow/terminal reservations and pull recovery.
  Identity survives unknown pull EOF and retained results. Repeated synchronized
  sparse creation/context-close races verify registration and resource release.
  These are native std concurrency/resource contracts, not C callback/ABI ones.

Release integration exposed a one-bit coefficient discrepancy in the existing
std KeyMath test after code-layout changes (coefficient 27: -0.74939865 versus
C -0.7493987). The configured callback was eligible for LLVM constant folding.
`KeyAnalysis::new_with_math` now keeps callback dispatch opaque at construction,
so the selected runtime backend performs the operation. No coefficient, score,
fixture or tolerance was changed. The focused release audit matches raw C
coefficients/chroma/scores exactly and still reports portable scores
[55734, 55374, 54150] versus C [55735, 55374, 54150]. This is host/compiler fixture
evidence, not a universal floating-point guarantee.

Evidence root: `/home/shome/.local/share/libapta-audio/rust-rewrite/`.

- `continuation-owning-next-baseline-20261004T171443.log`: unchanged passing
  combined baseline, before behavior edits.
- `continuation-owning-next-combined-check.log`: **123/123 C tests**, **269 ordinary
  Rust tests and 49 explicitly enabled external-C groups** in each debug/release
  profile. All 34 runner WAV example/CLI cases and the two all-feature CLI cases
  pass strict C reading. The existing two CLI export tests additionally carry
  supplied SHA-256 identities. Formatting, all-target Clippy, no-default-features
  and isolated allocation counters pass. Borrowed sparse replacement and identity
  setup/processing/snapshot add zero allocations to the core instrumentation.
- `continuation-owning-next-asan-check.log`: **56 runtime tests**, including
  **12 external-C groups**, pass host AddressSanitizer/leak checking. Uses
  `RUSTC_BOOTSTRAP=1 RUSTFLAGS=-Zsanitizer=address ASAN_OPTIONS=detect_leaks=1`
  and the x86_64 Linux target; C and std are not rebuilt with instrumentation.
- `continuation-owning-capacity-check.log`, `continuation-owning-ensemble-check.log`,
  `continuation-owning-pull-growth-check.log` and
  `continuation-owning-key-dispatch-check.log` retain focused passing evidence.
  The earlier `continuation-owning-identity-check.log` covers 36 known-duration
  cases; the final combined run covers all 72. Growth allocation and complete
  output comparisons also pass in the final combined run.
- `continuation-owning-next-integration-check.log` and `...integration-check2.log`
  deliberately preserve the release-only coefficient failure before the callback
  dispatch correction. Earlier compile/test attempts remain preserved too.
- `continuation-owning-next-aarch64-check.log`: portable core/no-default-features
  compilation. `continuation-owning-next-ilp32-compile.log` and
  `continuation-owning-next-msvc-compile.log`: all-target workspace compilation.
  These are compilation evidence, not linking or execution.

Actual platform linking was attempted, without changing the host:

- `cc -m32 -x c -o /home/shome/.local/share/libapta-audio/rust-rewrite/ilp32-link-probe -`
  with stdin `int main(void) { return 0; }` fails because Scrt1.o, crti.o and
  32-bit libgcc are missing (`continuation-owning-ilp32-link-probe.log`). Remedy:
  a provisioned 32-bit libc/GCC multilib toolchain, then actual Rust/C execution.
- `cargo build -p libapta-runtime --bin apta-native --target x86_64-pc-windows-msvc
  --locked` fails because `link.exe` is absent
  (`continuation-owning-msvc-link-attempt.log`). Remedy: a usable MSVC linker and
  Windows SDK/import libraries in a Windows build/test environment, followed by
  actual executable acceptance. Installed Wine alone does not supply that toolchain.

`git diff --check` and the shared knowledge notebook validator pass. Source
handoffs: [runtime sparse owner](../../rust/runtime/src/owned_sparse.rs),
[source identity tests](../../rust/runtime/tests/source_identity.rs),
[capacity oracle](../../rust/tests/fixtures/sparse_capacity_oracle.c), and
[native usage](../../rust/README.md#source-identity-and-explicit-sparse-growth-acceptance).

### Remaining dependency gates

Continue broader all-stage failure/replay/capability and ensemble-promotion
matrices. Explicit range/node reservation is implemented; automatic growth policy,
fixed request-table expansion and unknown sparse duration remain separate work.
Fingerprint transport and seed identity are implemented; automatic source hashing
and metadata/tool parity remain open. No maximum-duration sparse heap or embedded
stack qualification follows from the 4,096-fragment fixture.

Full C custom allocation classes/layout/failure ordering, concurrent C ownership,
separately reviewed unsafe ABI, CMake/pkg-config static/shared packaging and frozen
consumers remain open. Platform linking/execution, Rust ESP-IDF integration and
snapshot stack sizing remain separate gates. Portable key score **55734 versus
host C 55735** remains unresolved; explicit std KeyMath matches accepted host
fixtures only. Original DSP accuracy and physical P4 qualification are unchanged.
No deployment, host-service change or hardware operation occurred.

## Publication retry, numerical boundary and desktop/platform continuation — 2026-10-04

One implementation agent, two build/test jobs. The clean supplied checkpoint was
verified live before the dated combined baseline. C algorithms, public headers,
ABI, container format and the portable backend remain unchanged.

### Integrated behavior and exact contracts

- Bounded sequential and sparse publication now retries musical failures in their
  own stages. Previously a failed key publication could consume the newly free
  slot before S4 retried an accepted revision. The public-C regression reproduced
  generation 130 with native changed mask 512 versus C 280. Both wrappers now
  match the entire trace. Fresh waveform/detail publications also clear unrelated
  pending musical mask bits; the unacknowledged stage serial remains retryable.
  Four new reference traces cover both wrappers with all musical stages, with
  and without detail. Retained wire graphs stay immutable, revision acceptance
  survives failed publication, and owning mirror-only retry is unchanged.
- The new public allocation oracle records each allocation's size/alignment/class
  and exhaustively injects failure at all 49 allocation calls in its successful
  all-feature lifecycle. Calls 11, 12 and 24 are recoverable in that fixture and
  still reach END_OF_INPUT; the other 46 return OUT_OF_MEMORY. Every case releases
  all allocations. The class union is LARGE|PERSISTENT, not a universal promise
  that other profiles never request FAST/TEMPORARY/DMA. Invalid allocator pairs
  fail before callbacks. The x86_64 workspace profile reports minimum 591392,
  recommended 628370, alignment 16; minimum-minus-one fails before allocation,
  exact minimum creates successfully. These are measured C layouts, not Rust
  layouts. A synchronized reader acquires across 128 publications with a custom
  allocator, checks immutable/monotonic generations and releases before session
  destruction. An independently retained result is released on another thread
  after session destruction; context destruction stays Busy until release.
- The initially-unknown public-C sparse oracle establishes out-of-order push,
  complete versus gapped EOF, rejection below the highest accepted offset,
  idempotent repeated EOF, conflicting changed EOF and initial-result retention.
  Bounded-result-slot construction rejects unknown duration. The two final
  containers match the existing known-duration native owning sparse engine
  byte-for-byte. **Native unknown sparse construction/growth/scheduling and its
  intermediate publications remain unimplemented**; final overview equivalence
  does not establish those contracts or musical unknown-origin equivalence.
- Native `--hash-source` computes SHA-256 over the exact byte buffer analyzed,
  including RIFF headers/chunks. Hashing is opt-in, conflicts with supplied
  identity and works per file in corpus. `verify-source` requires SHA-256 identity
  and rejects absent/opaque/mismatching objects. No implicit verification is added
  to host-supplied identity. RustCrypto `sha2` is confined to the std runtime;
  portable core dependencies and allocator/unsafe prohibitions are unchanged.
- `--metadata-cbor=FILE` accepts at most 8192 bytes of canonical CBOR containing
  the seven supported typed META fields. Re-encoding must reproduce the input,
  so unknown fields cannot silently disappear. Validation precedes analysis and
  output creation. Metadata is export-only, not live-session mutation; corpus
  rejects the per-file option. All-feature/hash/metadata exports pass strict C
  reading. `inspect --json` emits schema version 1 with exact numeric source,
  feature/section, overview/detail, tempo and selected-key/candidate fields.
  Numeric FourCC byte arrays avoid escaping untrusted section names. This summary
  is not a frozen corpus interface or a full graph export.

### Portable numerical gate: diagnosed, not corrected

The unchanged public C key fixture still uses 8000 Hz, 320000 frames, uniform
120 BPM impulses of amplitude 0.75. The extended oracle returns coefficients plus
1250 snapshots (every 256 source frames) of both Goertzel arrays and chroma.
Eight combinations isolate runtime/portable cosine, logarithm and square root.
Debug and release establish:

- Earliest coefficient divergence is index 27, argument bits `3ffa3924`:
  portable `bf3fd897`, host C `bf3fd898`. The existing opaque callback dispatch
  remains necessary to prevent the separate release constant-folding discrepancy.
- Portable state first differs in the sampled frame-256 Goertzel trace; chroma
  first differs at sampled frame 8192 (after the first 8000-frame window).
- Host cosine alone removes all sampled Goertzel differences and restores the
  selected quantized score, but portable log still leaves a one-bit chroma
  difference. Host cosine+log makes every sampled intermediate bit-identical;
  changing sqrt alone has no effect on this fixture.
- Final score bits are portable `3f59b759`, host `3f59b75b`. The actual
  `score*65535+0.5` values are 55734.996 and 55735.004, producing selected scores
  55734 and 55735. Remaining candidate scores are 55374 and 54150.
- `python3 rust/tests/fixtures/key_rounding.py` independently evaluates cosine
  with 90-digit Decimal arithmetic and 100 Taylor terms. At that argument the
  portable coefficient is the nearer float32 value (absolute errors about
  2.90714e-8 versus 3.05333e-8). Runtime host `cosf` selects the other neighbor.
  Thus treating the host value as a portable accuracy correction would be wrong;
  universal portable/platform-libm identity is not established. No output
  normalization, special-case coefficient, tolerance change or backend replacement.

Run the compiled audit after the combined runner builds its oracle:

```bash
export CARGO_TARGET_DIR=/home/shome/.local/share/libapta-audio/rust-rewrite/combined/cargo-target
export CARGO_BUILD_JOBS=2 RUST_TEST_THREADS=2
export APTA_C_KEY_MATH_ORACLE=/home/shome/.local/share/libapta-audio/rust-rewrite/combined/key-math-oracle
cargo test -p libapta --lib trace_portable -- --ignored --nocapture
cargo test -p libapta --release --lib trace_portable -- --ignored --nocapture
```

### Platform evidence and prerequisites

The missing ILP32 startup/libgcc prerequisite was resolved without privileged
installation or host changes. Ubuntu packages were downloaded and extracted under
`/home/shome/.local/share/libapta-audio/rust-rewrite/ilp32-toolchain/`:
`libc6-i386`, `libc6-dev-i386`, `lib32gcc-s1`, `lib32gcc-13-dev`, `libc6-dev`,
`linux-libc-dev`, `lib32stdc++6`, `lib32stdc++-13-dev`. Local `cc-i686`/`cxx-i686`
wrappers use that sysroot/startup/library path and explicit local ELF interpreter
and rpath. They do not install files into `/lib` or change loader services.

Default `-m32` C links and executes its 121-test configuration; ordinary Rust
binaries also link and execute. However default GCC x87 excess precision differs
from Rust's SSE arithmetic: the key oracle already differs at coefficient 4.
That default-profile parity failure is preserved. A separate C build under
`c-ilp32-sse2`, with `-msse2 -mfpmath=sse` for both C and C++, tests the same
arithmetic profile as Rust and enables examples for the 123-test configuration.
It does not silently replace or qualify the default x87 profile.

Reproduction scripts and all generated binaries stay in the evidence root:
`run-ilp32-20261004T154451Z.py` (default-profile failure) and
`run-ilp32-sse2-20261004T154451Z.py` (explicit SSE2 profile). For native checks:

```bash
export CARGO_TARGET_I686_UNKNOWN_LINUX_GNU_LINKER=/home/shome/.local/share/libapta-audio/rust-rewrite/ilp32-toolchain/cc-i686
cargo test --workspace --target i686-unknown-linux-gnu --locked
cargo test --workspace --target i686-unknown-linux-gnu --release --locked
```

Rust's bundled `rust-lld` removes the missing-linker obstacle for MSVC, but actual
linking still fails on absent Windows SDK import libraries: `kernel32.lib`,
`ntdll.lib`, `userenv.lib`, `ws2_32.lib`, `dbghelp.lib`. The smallest remaining
remedy is a provisioned Windows SDK/MSVC build-and-execution environment. The
existing Windows CI job now runs native Rust release tests when that workflow
runs; adding the step is not evidence that it has executed. No Windows acceptance
is claimed from `cargo check` or Wine availability.

The RV32IMAF-C release diagnostic measures `SessionSnapshot` at **3768 bytes**
and LLVM reports **3456 bytes** for the probe's snapshot function stack frame.
This excludes caller result placement, nested calls, interrupts and RTOS use; it
is not a safe task-stack recommendation. The probe source/object and reproducible
`run-diagnostics-fixed-20261004T154451Z.py` stay in the evidence directory.
No ESP-IDF Rust toolchain/integration or complete embedded snapshot-stack
qualification was established. Existing C ESP-IDF support is not a Rust firmware artifact.
Physical P4 operation, deployment and host-service changes were not performed.

### Verification and remaining work

All evidence is under `/home/shome/.local/share/libapta-audio/rust-rewrite/`.
The dated `continuation-deep-*20261004T154451Z.log` files retain the baseline,
failed probes, exact arithmetic audit, integration passes and final checks.
- `continuation-deep-baseline-20261004T154451Z.log`: combined baseline before edits.
- `continuation-deep-final-20261004T154451Z.log`: **123 C tests, 271 ordinary
  Rust tests and 55 external-C groups per debug/release profile**. All 34 runner
  WAV interchange cases and two all-feature CLI cases pass; the latter now include
  computed SHA-256 and META. JSON is parsed with Python's JSON reader. Formatting,
  Clippy, no-default-features and isolated allocation instrumentation pass.
- `continuation-deep-asan-runtime-20261004T154451Z.log`: **59 runtime tests**,
  including **13 external-C groups**, pass AddressSanitizer/leak checks.
- `continuation-deep-asan-core-20261004T154451Z.log`: **34 focused core tests**
  (library numerical tests, musical publication and reference contracts) pass
  AddressSanitizer/leak checks. Rust uses `RUSTC_BOOTSTRAP=1`,
  `RUSTFLAGS=-Zsanitizer=address`, x86_64 target and `ASAN_OPTIONS=detect_leaks=1`.
  These logs do not imply that the external C oracle archive or std was instrumented.
- `continuation-deep-{aarch64,ilp32-compile,msvc-compile,riscv-compile}-20261004T154451Z.log`:
  required AArch64/no-default-features and i686/MSVC all-target compilation, plus
  portable RV32IMAF-C compilation. None substitutes for platform execution.
- `continuation-deep-ilp32-final-20261004T154451Z.log`: final-source **271 ordinary
  Rust tests and 55 external-C groups in both debug/release**, linked/executed as
  i686 binaries against the separately compiled SSE2 C oracle archive. Its C suite
  passes **123/123** (`continuation-deep-ilp32-sse2-tests-20261004T154451Z.log`).
  The default x87 failure remains in `continuation-deep-ilp32-parity-20261004T154451Z.log`;
  SSE2 success is not default-x87 parity.
- `continuation-deep-detail-retry2-20261004T154451Z.log`: all ten focused musical
  tests pass, including the original 91 lifecycle profiles and four new retry
  traces. Final combined verification reruns 72 source/seed combinations and the
  exact 4096-fragment/524288-frame capacity case in both profiles.
- `continuation-deep-c-oracle-sanitizers2-20261004T154451Z.log`: both new C oracles
  pass with the GCC ASan/UBSan reference archive, including all 49 injected
  failures, concurrent custom allocation and both unknown sparse cases.
- `continuation-deep-riscv-stack2-20261004T154451Z.log`: final release-object
  layout and LLVM stack-size diagnostic described above. The first diagnostic's
  section-name extraction warning is preserved; the corrected script extracts
  the actual `SNAPSHOT_BYTES` section and reproduces the measurement.
- `continuation-deep-workflow-pins-20261004T154451Z.log`: the five previously
  documented unpinned actions in three unchanged research workflows remain; no
  new action dependency was added. Notebook validation and `git diff --check` pass.

Continue native unknown-duration sparse ownership only after extending the new
public reference contract through growth, scheduling, seeding and music. Extend
custom allocation coverage into memory budgets, reallocation callbacks, static
workspace processing and source failure ordering before implementing a separately
reviewed unsafe C boundary. The reference-only allocation tests do not establish
native custom allocator, C ownership or ABI parity. Complete C ABI/static/shared
packaging and frozen consumers remain open; keep the installed C product intact.
Further ensemble/profile coverage, automatic capacity policy/request-table growth,
live owning metadata, full JSON/corpus interfaces, platform execution and embedded
stack work remain independent engineering tasks. Original DSP accuracy gates and
physical hardware qualification remain separate from rewrite parity.

## Native Rust consumer integration — 2026-10-04

Native consumers now take priority over legacy ABI emulation. Pajoniiir M1 uses a
pinned direct Cargo dependency on the allocator-free, unsafe-free `no_std` core;
its adapter and product policies remain in Pajoniiir. The desktop `std` runtime,
C allocator/layout/handle emulation, C packaging and ESP-IDF are not prerequisites
for this consumer. C compatibility remains a separate unfinished workstream;
no C implementation, public header, ABI or container format changed.

### Portable API and consumer behavior

`GridSegment::beat_at` offers bounded, checked authoritative beat lookup without
an expanded allocation. It skips phase-continuity anchors before applicability,
checks ordinal/coordinate overflow and rejects inconsistent declared coverage.
It delegates position arithmetic to the existing reference-Q32 helper and does
not resolve hybrid authority. `FractionalFrame::rounded_milliseconds` rounds the
full Q32 coordinate once with the actual sample rate, nearest/ties upward, into a
checked u64. Product-specific u32 coordinates remain the consumer's responsibility.
Five exact integer tests cover rates, half ties, large widths, overflow, context
anchors and inconsistent counts. No DSP coefficients/backend/selection changed.

The isolated `pajoniiir-apta-adapter` uses the existing Libapta session engine and
caller-provided conversion storage. A deterministic 8 kHz/320000-frame fixture
runs in 256-frame/one-step processing budgets, produces 80 beats and BPM x100
12000, then exercises the existing Deck Beat Jump and Sync controls. Phase comes
from the actual meter downbeat, not ordinal-zero or assumed 4/4. Preflight errors
preserve caller output. Retained conversions survive destruction of the original
session; two distinct banks allow staging without mutating a pinned generation.
Generation narrowing rejects overflow/zero. Stopped-boundary upgrades require
matching source identity/geometry, provider and lineage and a newer generation.

Further integrated portions include exact live/local-cache beat comparison,
required cache source identity and caller parse limits, whole-track waveform
conversion into the existing RGB565 renderer, and a host WAV-to-analysis/PPM
example. Missing/non-final grid or meter exposes tempo only; missing tempo stays
zero. Unsupported sparse coverage, explicit ordinal gaps, hybrid authority and
multiple segments fail explicitly. Cache/native provider changes establish a new
pin rather than treating cache-assigned generations as native session identity.
Key and confidence are not added to a neutral model with no demonstrated consumer.

Pajoniiir's existing ADR-006/M1R-P1-001 production gates are preserved. Neither its
firmware nor Slint simulator selects this experimental provider. Host tests call
the actual domain/raster consumers; the host example is not the production media
worker. See the consumer's adapter README and architecture for its acceptance.

### Preserved contracts and remaining engineering

The combined reference suite continues to cover stage ordering, failed publication
retry, reservation rollback/partial nodes, source release/recovery, cancellation,
immutable retention and source/seed identity. Integration required no changes to
those processing contracts. Unknown-duration sparse ownership still needs its
own growth/scheduling/seeding/music extension against the existing C EOF probes;
known-duration acceptance does not close that work.

Remaining native work includes production worker/media integration and generation
ownership, global/hybrid/cache authority and sparse/detail viewport contracts,
fuller key/confidence consumers where needed, caller storage budgeting/placement,
and complete embedded stack/timing qualification. Explicit core/raster storage is
not proof of internal SRAM/PSRAM placement or DMA/cache safety. Cross-compilation
is not target execution; no hardware was operated or firmware deployed.

Deferred C work remains exact custom allocator classes/order/reallocation,
workspace/handle layout and ownership, separately reviewed unsafe ABI exports,
static/shared/CMake/pkg-config packaging and frozen C consumers. Windows execution
still requires a suitable SDK/import-library and execution environment. Default
i686 x87 remains a distinct numerical profile; SSE2 acceptance does not qualify it.

The portable key 55734 versus host C 55735 boundary remains unchanged. No rounding
patch, hidden math-backend switch, fixture/tolerance change or accuracy claim.
Original musical/DSP accuracy gates and physical P4 release qualification remain
independent of all native integration evidence.

### Libapta verification for this continuation

Fresh evidence lives in `/home/shome/.local/share/libapta-audio/rust-rewrite/`.
No earlier logs were overwritten. `CARGO_BUILD_JOBS=2` and `RUST_TEST_THREADS=2`
were used with the shared external target directory and one implementation agent.

- `native-consumer-baseline-20261004T165758Z.log`: required combined baseline
  before behavior/API work, including 123 C tests and the prior 271/55 Rust matrix.
- `native-consumer-final2-20261004T172109Z.log`: final source passes **123 C tests,
  276 ordinary Rust tests and 55 external-C groups in each debug/release profile**;
  **34 WAV interchange cases and two all-feature CLI cases**, allocation
  instrumentation, formatting, Clippy and no-default-features pass. This rerun
  includes the context-anchor correction after the first integration checkpoint.
  Musical lifecycle/retry, 72 identity/seed combinations and the exact
  4096-fragment/524288-frame capacity comparison are rerun, not assumed.
- `native-consumer-asan-runtime-20261004T171249Z.log`: **59 runtime tests**,
  including **13 external-C groups**, pass ASan/leak checking.
- `native-consumer-asan-core-20261004T171249Z.log`: **39 focused core tests** pass
  ASan/leak checking, including all five final coordinate tests. External C
  binaries in these Rust runs are separately compiled reference binaries;
  instrumenting Rust does not imply instrumented C/std.
- `native-consumer-ilp32-final-20261004T171249Z.log`: actual i686 SSE2 execution
  passes the same **276 ordinary / 55 external-C** debug/release matrix.
  `native-consumer-ilp32-c-*` reruns **123 C tests** for that explicit SSE2 profile.
- `native-consumer-c-oracle-sanitizers2-20261004T171249Z.log`: allocation/failure/
  concurrent-retention and both unknown sparse EOF probes pass against the
  ASan/UBSan C archive. These remain reference contracts, not implemented C ABI.
- `native-consumer-portable-final-*` reruns AArch64/core, i686/MSVC all-target and
  RV32/core compilation after the final helper changes.
- `native-consumer-riscv-stack2-20261004T171249Z.log` reruns the release snapshot
  probe: **3768-byte object / 3456-byte function frame**, still excluding callers,
  nested calls and interrupt/runtime costs. It is not a task-stack recommendation.

The reproducible extra/platform scripts are retained alongside these logs with
stamp `20261004T171249Z`. Consumer evidence is separately owned under
`/home/shome/.local/share/Pajoniiir-M1/apta-evidence/`; its verification script,
fixture, rendered artifact and adapter README carry that scope's exact acceptance.


## Native consumer worker and extended views — 2026-10-04

This continuation extends the owning Pajoniiir adapter without changing Libapta
algorithms, core APIs, C source/headers/ABI or container bytes. The consumer keeps
its immutable Cargo pin to the previously published portable source revision;
this library checkpoint changes documentation only. Portable Libapta remains
independent of product media identities, Deck controls, rendering and hardware.
The original collaborator checkout is preserved; the existing isolated consumer
contribution incorporates inspected foundation advances and stays open for review.

### Newly integrated consumer behavior

- Continuous multiple-segment conversion uses `GridSegment::beat_at`, including
  context anchors. Full-source segment coverage, final phase, consecutive ordinals
  across boundaries, exact meter whole-frame/ordinal correspondence and applied
  revision identity are checked before output mutation. Global precedence remains
  explicit; unsupported global data never falls back to local interpretation.
- Identity-checked global segment/explicit caches use caller-owned native segment
  and beat scratch plus independent conversion banks. Scratch may change on error;
  converted banks remain transactional and outlive source bytes/scratch. Cache
  generations remain host-assigned; container v1 has no native generation lineage.
- An experimental `no_std` worker owns a configured portable Session. Requests
  carry the originating catalog/media lease, track ID and present source identity.
  A persistent single lane assigns checked request generations; cancellation,
  replacement, unmount/reinsert and stale delivery fail closed. Each processing
  call uses 256 frames/one core step outside audio. Reads/decoding remain caller
  owned; failures prevent publication and incomplete sessions cannot complete.
- Retained deck pins hold caller banks and inspect actual Deck state. Both playing
  and pending transport requests block replacement. Upgrades require the same
  media/track/source geometry/provider/lineage and a newer generation. A new track
  requires a new pin. The neutral view itself still cannot enforce ownership if
  product callers bypass the pin or retain copied views across invalidation.
- Explicit aligned whole-column sparse overview/detail windows use actual source
  coordinates and the existing RGB565 renderer. Adjacent detail tiles work; holes,
  unaligned crops and partial EOF columns are rejected transactionally. No inferred
  silence, hybrid authority, automatic layer fallback or pixel interpolation.
- The host WAV example uses the worker/pin path and existing product SHA-256 over
  exact input bytes. It renders an independent waveform after worker destruction.
  Its mounted media is a host fixture, not the production USB/filesystem worker.

### Evidence boundaries exposed by real processing

The unchanged real S6 uniform-impulse fixture (8 kHz, 320000 frames, 120 BPM,
amplitude 0.75) publishes a final global segment ending at frame 262144, with
timing that does not bind to its local meter. The consumer correctly rejects it.
Positive single/multiple-segment and explicit global-cache fixtures transport
actual PCM-derived local timing under an explicitly constructed global profile;
they do not qualify the rejected S6 result or improve its DSP accuracy. Hybrid
remains unsupported because the consumed model does not identify override ranges.
No algorithm correction, output normalization, tolerance change or backend switch.

Real worker PCM exercises Deck Sync/Beat Jump, two retained generations, stale
requests/catalog leases, cancellation, source failure, pending transport and
capacity rollback. Test-only allocation counting verifies no allocations during
worker creation, processing and conversion. Real sparse PCM crosses a detail tile
boundary and reaches the rasterizer; output survives session/cache destruction.
See the consumer adapter README for exact acceptance logs and counts.

### Remaining work and independent gates

Production filesystem/Embassy scheduling, Slint selection, storage placement,
complete nested-call stack/timing, DMA/cache safety and physical P4 execution are
still required. Caller buffers and portable compilation do not qualify SRAM/PSRAM
placement or provide a task-stack recommendation. Neither production provider nor
ADR-006/M1R-P1-001 gates were enabled or weakened.

Unknown-duration sparse ownership still requires expanded compiled-C growth,
scheduling, seeding and music contracts before implementation. No new acceptance
is claimed for those profiles, Windows execution, default i686 x87, full platform
or frozen-tool compatibility. Portable key 55734 versus C 55735 remains the known
backend parity boundary. Original musical accuracy and physical hardware gates
remain independent and open.

Legacy allocator classes/order/reallocation, C binary workspace/handle ownership,
reviewed unsafe ABI exports, C packaging and frozen consumers remain deferred as a
separate unfinished workstream, not prerequisites for native Rust integration.

### Verification for this continuation

- Fresh unchanged-core combined baseline:
  `/home/shome/.local/share/libapta-audio/rust-rewrite/native-extended-baseline-20261004T174953Z.log`.
  **123 C tests; 276 ordinary Rust tests and 55 external-C groups in each
  debug/release profile; 34 WAV interchange cases and two all-feature CLI cases**.
  Formatting, Clippy, no-default-features and allocation instrumentation pass.
  No library source changed after this run; this checkpoint changes documentation.
  Prior i686/Windows/AArch64/native-runtime ASan matrices were not rerun here.
- Consumer final-source script `verify-consumer.py`, external evidence root
  `/home/shome/.local/share/Pajoniiir-M1/apta-evidence/`, stamp `20261004T181337Z`:
  **382 workspace tests; 78 focused release tests; 8 adapter ASan integration
  tests plus 1 lifetime compile-fail doctest**. Formatting/Clippy, actual pinned
  Rust 1.95 P4 library compilation, dependency inspection and optimized object
  generation pass. The desktop runtime/IDF/RTOS remain outside normal dependencies.
- `verify-extended-artifacts.py`, stamp `20261004T181426Z`: both host WAV examples
  reproduce exact PPM bytes after worker optimization (800x128, fixture 120 BPM /
  80 beats / 4120 green pixels; silence no tempo/grid and an 800-pixel centerline).
- P4 individual function frames: worker completion **1104 B** and waveform
  **112 B**, down from **7712 B / 7600 B** when they constructed a full snapshot.
  Existing typed getters remove unused detail copying without a new core API.
  Worker push is 1776 B, step 352 B; cache conversion with scratch 2608 B;
  overview/detail windows 128 B / 48 B. These exclude nested calls, caller
  placement, interrupts and runtime overhead: **not task-stack recommendations**.
- The consumer contribution incorporated inspected foundation exFAT advances and
  the collaborator's own subsequent host-check fix before final verification.
  The original collaborator checkout was neither switched nor edited. Production
  firmware/Slint provider selection, C source/ABI/wire and accuracy gates remain
  unchanged. Shared knowledge validation and both repositories' diff checks pass.

## Native catalog filesystem consumer — 2026-10-04

The next integrated consumer portion lives entirely in Pajoniiir. Its immutable
Cargo pin remains `03b0643aaedc7b362a2556d8bcc563b78ac526ec`; Libapta's core API,
DSP, math backend, C implementation/ABI and wire format are unchanged. The existing
borrowed WAV decoder and Session APIs suffice; no second engine or C-shaped layer
was introduced.

### Newly verified path

Pajoniiir selects from both fixed and storage-backed immutable catalog snapshots,
reserves its persistent lane generation before I/O, and retains the originating
lease/path/track ID. Its native `AsyncFileSystem` reads at most 4096 bytes per call
into explicitly bounded caller whole-object storage. Existing product SHA-256
hashes every captured byte, including headers/trailing bytes. Full coverage,
successful file close and WAV validation precede verified job creation. Short
reads work; invalid counts, premature EOF, handle geometry changes, I/O errors,
cancellation, replacement and stale media cannot authorize a completion.

The same portable WAV decoder feeds a geometry-matched Session in at most
256-frame/one-core-step calls. A 17-frame queue deliberately forces partial PCM
acceptance without losing/repeating samples. Generated native FAT32 fragmented
and exFAT contiguous/fragmented files reach 80 beats / BPM x100 12000, actual Deck
Beat Jump/Sync, and exactly 4120 green raster pixels. These are software transport
and consumer tests of the unchanged deterministic local-analysis fixture, not DSP
accuracy acceptance. Retained beat/waveform banks survive source/worker/filesystem
destruction. Allocation instrumentation covers acquisition, hashing, decoding,
processing and conversion with zero observed allocations.

Pajoniiir also adds explicit EOF-aware overview/detail windows. The only permitted
short column ends at the trusted source's declared EOF; starts remain aligned,
coverage must exist, and each source column occupies one display column. This is
not time-proportional resampling. Strict existing APIs, transactional output,
unsupported hybrid authority and global-precedence rejection remain intact.

### Acceptance and limits

- Unchanged-library combined run:
  `native-source-baseline-20261004T185459Z.log`, under the existing Libapta evidence
  directory, passes **123 C tests, 276 ordinary Rust tests and 55 external-C
  groups per debug/release**, **34 WAV interchange and two all-feature CLI cases**,
  formatting, Clippy, allocation instrumentation and no-default-features.
- Consumer `verify-consumer.py`, stamp **20261004T190329Z**, passes **393 workspace
  tests including doctests**, **89 focused release tests including doctests**,
  **19 adapter tests plus one compile-fail doctest under ASan**, formatting,
  Clippy, pinned Rust 1.95 P4 compilation, dependency inspection and object emission.
  No new runtime, std/alloc, IDF or RTOS dependency enters the adapter.
- Consumer external artifact/probe scripts reproduce exact impulse/silence PPM
  output and instantiate source acquisition with actual FAT32/exFAT generic
  backends on RV32. Canonical exact final stamps and object/frame measurements
  are recorded in the consumer README and new external handoff.

This remains an experimental whole-object profile. Caller capacity bounds memory;
large tracks need a future streaming decoder contract before production use in
32 MiB PSRAM. Open/close futures must run to completion; backend in-flight I/O
recovery is not solved by poisoning a cancelled analysis reader. The existing
USB broker's channel ownership and production Embassy scheduling are unchanged.
No production firmware/Slint provider selection or ADR-006 gate is enabled.
Object budgets and individual diagnostic frames do not establish storage placement,
DMA/cache safety, whole nested-call/task stack, timing or physical P4 acceptance.

The persistent single-lane Requests lifetime and separate retained Deck pins remain
required. Generation exhaustion is tested to fail without wrapping or replacing
the preceding request. Unknown-duration sparse ownership still needs expanded
compiled-C growth/scheduling/seeding/music coverage. Hybrid authority, arbitrary
waveform resampling and broader production codec/cache policy remain open.
Legacy custom allocators/layout, reviewed unsafe C ABI exports, packaging and
frozen C consumers remain a separate unfinished compatibility workstream.
The portable key 55734/C 55735 boundary and original musical accuracy gates are
unchanged. i686, Windows/MSVC, AArch64 and native-runtime sanitizer matrices were
not rerun in this consumer-only continuation; earlier evidence remains dated.

## Bounded streaming WAV consumer — 2026-10-04

The portable core now exposes `wav::WavScanner` and `WavLayout` for consumers that
cannot retain full source objects. Incremental framing accepts the same RIFF,
format, padding, data-before-format and trailer profiles as borrowed `Wav`.
It retains constant framing storage (compile-time <=192 B), reports PCM byte
coordinates only after complete input, and fails terminally on invalid framing.
Block decode reuses the existing PCM normalization/invalid-float atomicity path;
no algorithms, C source, public headers, ABI or container format changed.

The Pajoniiir adapter's `streaming` path selects the original catalog identity,
scans/hashes every byte with <=4096-byte reads, closes, then reopens the copied
path with matching geometry. It decodes through 4096-byte caller scratch and
512 f32 samples, retaining partial bytes and partial accepted PCM. A second
full-object SHA-256 must match and the second file must close successfully before
any worker/result escapes. Ancillary/trailing changes fail just like PCM changes.
Each analysis call reads at most once, decodes <=256 frames and runs one core
step; this bounds work units, not backend latency. No filesystem seek is needed.

The host example and native FAT32/exFAT fixtures exercise this path through actual
Deck Sync/Beat Jump and RGB565 rendering, including caller-retained output after
source destruction. The first-pass identity is captured bytes, not proof of
immutable external storage. Two passes cost extra I/O; caller duration/work and
core/output capacity limits still apply. This removes whole-object retention,
not the need for bounded output banks, memory placement and measured scheduling.

A dropped pending read poisons analysis; callers still consume with finish/abort,
drive open/close to completion and recover backend pending operations/handles/DMA.
No additional USB client lane or production provider selection is introduced.
ADR-006 remains gated. C allocator/layout/ABI/packaging, unknown-duration sparse
ownership, hardware qualification and original DSP accuracy remain separate work.
The diagnosed portable key boundary and rejected real S6 fixture are unchanged.

Acceptance evidence and final consumer pin are recorded in the continuation
handoff and consumer README after final-source checks.

Final core checks: `streaming-final-20261004T202709Z.log` passes **123 C tests,
279 ordinary Rust tests and 55 external-C groups per debug/release profile**,
34 WAV interchange cases and two all-feature CLI cases, formatting, Clippy,
no-default-features and allocation instrumentation. The unchanged baseline is
`streaming-baseline-20261004T201749Z.log` (276 ordinary Rust tests). New scanner
checks compare format/geometry/decoded samples exactly with borrowed WAV across
chunk boundaries, mutations, truncation, reordered/duplicate/extensible chunks,
odd padding and trailers. Scanner/block decoding is included in the isolated
allocation counter. `streaming-asan-20261004T203034Z.log` passes **8 focused tests**
including scanner/decoder cases and the existing full session allocation path.
Logs are under `/home/shome/.local/share/libapta-audio/rust-rewrite/`.
Broader i686/Windows/AArch64/runtime sanitizer matrices were not rerun here.

Final consumer continuation passes **400 workspace / 96 focused release tests**,
**26 adapter tests plus one lifetime doctest under ASan**, formatting/Clippy,
pinned Rust 1.95 P4 checks, exact prior impulse/silence PPM output and zero measured
allocations. Consumer source pin remains `a242c4ab1ada6ac2547f63332ef7f7c0f36276fa`.
External consumer evidence: `verify-consumer.py` stamp `20261004T203840Z`, artifact
stamp `20261004T203948Z`, native-source probe `20261004T203949Z`, streaming-source
probe `20261004T204041Z`, under the existing Pajoniiir evidence directory.

P4 diagnostics drove a consumer ownership improvement: synchronous construction,
borrowed asynchronous open/finish and synchronous extraction avoid moving Session
through I/O futures. FAT32 open/finish futures fell from 14592/8464 to 2936/504 B;
corresponding one-poll frames from 18672/12656 to 3136/224 B. Final streaming
workers including handles are 4256/4280 B (FAT32/exFAT), with 1408/1440 B step
futures and 1488/1520 B one-poll frames. Nested sector loading separately uses
1584 B. The adapter README gives explicit caller-bank formulas, object ceilings,
additional nested frames and exclusions. These remain software diagnostics, not
complete task-stack/placement/timing or physical P4 acceptance.

## Consumer policy and full-task storage — 2026-10-04

Pajoniiir's experimental native consumer now enforces explicit per-job object,
exact duration and per-pass work ceilings. Opened object length is checked before
reading; complete first-pass close/framing precedes the exact frames/rate duration
check and source verification. Step exhaustion fails terminally with the handle
still available for finish/abort. Pending reads resume without recharging a step;
dropping a pending read remains terminal. Existing unbounded-policy entrypoints
remain available for callers governing limits externally. Product policy stays
in Pajoniiir; portable source, the immutable Cargo pin, C implementation, headers,
ABI, wire format and numerical behavior are unchanged.

A checked target-sized local-tempo storage plan accounts for existing onset/flux,
queue, native/rendered waveform, TWO beat banks and fixed source/PCM scratch before
allocating caller banks. The 320000-frame / 17-frame-queue / 1250-column /
two-128-beat-bank profile is exactly 108772 bytes. Additional features, other
tracks, filesystem, control, catalog, framebuffers and stack remain additional.
The host example and actual native FAT32/exFAT fixtures use finite work policies;
retained beats/columns and consumer output remain exact. The host example applies
its policy to the opened file, avoiding a separate metadata snapshot.

The adapter can borrow a verified Worker in place after the second close/hash
acceptance. This preserves all delivery gates while avoiding large Session moves
through async extraction/error temporaries. Existing consuming extraction still
returns the original worker on premature use for handle recovery.

Full coroutine/Embassy diagnostics show why one-step frames are insufficient:
with Rust 1.95 and Embassy executor 0.10.0, the initial FAT32/exFAT complete task
pools were 13336/13640 bytes and polling frames 33344/33280 bytes before callees.
Borrowed verified access and separate synchronous construction reduce these to
9792/10096-byte pools and 21008/21424-byte polling frames. Preparation itself uses
6976 bytes, so even that identified nested path requires 27984/28400 bytes before
its own callees/executor/interrupts. This is not a qualified stack size. The future
lives inside the pool; do not double-count Session/worker/child futures. Arrays +
pool + diagnostic native backend total 119156/119468 bytes, still excluding other
owner state and stack. The adapter README owns detailed limits and reproduction.

The complete job also passes an allocation-instrumented host test with every read
suspending once and cooperative yields between steps, ending at 120 BPM, 80 beats
and 4120 green pixels. Actual Embassy task pools are compiled, not enabled or
executed on firmware. External evidence source/lock/object/disassembly and test
are retained by `verify-task-storage-probe.py`, stamp 20261004T210659Z, in the
existing Pajoniiir evidence directory. Production executor features, SRAM/PSRAM,
DMA/cache, full nested stack and timing/coexistence remain unqualified.

The later lease/construction continuation below supersedes the exFAT-handle
blocker. Foundation still needs a uniquely owned analysis client with pending
completion/resource recovery. No Library/Deck channel is
borrowed; firmware/Slint selection and ADR-006 remain gated. Unknown-duration
sparse ownership, broader codec/view profiles, C allocator/layout/unsafe ABI/
packaging and original DSP acceptance remain independent unfinished work. The
known S6 rejection and key coefficient boundary are unchanged.

Fresh unchanged-core combined acceptance:
`consumer-policy-baseline-20261004T211016Z.log` passes **123 C tests**, **279 ordinary
Rust tests plus 55 external-C groups per debug/release profile**, **34 WAV
interchange cases** and **two all-feature CLI cases**, formatting, Clippy,
no-default-features and allocation instrumentation. Core/runtime sanitizer and
broader i686/Windows/AArch64 matrices were not rerun in this consumer-only change.
Consumer final-source acceptance **20261004T210742Z** passes **405 workspace / 101
focused release tests**, **31 adapter tests + one lifetime doctest under ASan**,
formatting/Clippy and pinned P4 checks. Artifacts **20261004T210932Z** are byte
identical; native/streaming probes **20261004T210933Z / 20261004T210938Z** pass.
Shared knowledge validation passes (18 projects, 89 notes, zero broken links).


## Consumer lease and construction integration — 2026-10-05

Portable source/test pin remains a242c4ab1ada6ac2547f63332ef7f7c0f36276fa.
Pajoniiir rechecks the original media lease after async open/read/close and
prepares its existing local-tempo/meter Session directly into an empty caller
control slot. Product policy stays in the adapter; no C or portable algorithm,
format, API or numerical change is required. Native FAT32/exFAT tests retain exact
beats/columns and drive Deck Sync/Beat Jump and RGB565 consumers without measured
allocations. Foundation exFAT broker handles are integrated; the old
ExFatUnavailable blocker is superseded. Analysis-client lifetime, completion
draining and exclusive handle/buffer recovery still precede production selection.

Complete-task polling frames fall from 21008/21424 to 13328/13776 bytes, while
pools grow 16 bytes to 9808/10112 (FAT32/exFAT). Preparation is 7136 bytes, so the
identified poll+preparation subtotal is 20464/20912 before further callees. A
separate panic-abort/forced-frame-pointer inventory records all diagnostic
library frames, matching inspected firmware code-generation settings. This is
not a safe stack size or an executed firmware/executor acceptance test. The
consumer README owns measurements, exclusions and exact reproduction; new
external handoff: `pajoniiir-libapta-native-rust-continuation-2026-10-05-0505.md`
under `/home/shome/.local/share/libapta-audio/handoffs/`.

Fresh unchanged-core combined baseline `consumer-construction-baseline-20261005.log`
passes 123 C tests, 279 ordinary Rust + 55 external-C groups per debug/release,
34 WAV interchange and two all-feature CLI cases. Consumer 20261005T050057Z passes
430 workspace / 104 focused release tests, 34 adapter tests plus one compile-fail
lifetime doctest under ASan, fmt/Clippy, allocation and P4 checks. Exact raster
artifacts and source/task probes pass; final task evidence is 20261005T050332Z.
The probe now emits explicit dated objects rather than choosing newest cache files.
No broader core/runtime/C sanitizers, i686/Windows/AArch64 or physical P4 gates
were rerun. Unknown-duration sparse ownership, production codec/view policies,
C allocator/layout/unsafe ABI/packaging and original DSP accuracy remain open.
The real S6 rejection and portable key coefficient boundary remain unchanged.

## Native constructor and cooperative consumer — 2026-10-05

The borrowed `Session::new` wrapper now permits cross-crate inlining. The same
`with_storage` implementation, validation order, caller buffers and algorithms
remain authoritative; there is no API, allocator, unsafe, C or wire-format change.
A concrete pinned Rust 1.95 RV32 complete-job consumer exposed a large temporary
Result at this boundary. Making the wrapper visible to optimization reduced its
consumer local-tempo preparation frame from 7136 to 128 bytes. This is a measured
compiler result, not a portable stack-size guarantee. The external local-patch
experiment is `task-inline-probe-20261005` / `task-inline-20261005.o` under the
Pajoniiir evidence root; final immutable-pin acceptance is recorded by the consumer.

Pajoniiir's second-pass `StreamWorker::run` borrows caller control, yields after
each incomplete bounded step, and uses the existing open/hash/close and original
lease checks. Processing errors close the handle; cleanup errors take precedence.
Cancellation during reads or cooperative yields poisons delivery while keeping
control available for explicit abort. Backend pending-operation recovery and
non-cancellable open/close obligations still apply. These are consumer policies,
not portable-core executor or filesystem dependencies. Real native FAT32/exFAT
fixtures and the host WAV example use the driver and retain exact neutral output.
Neither firmware nor Slint enables APTA; unique broker client/recovery, placement,
timing, hardware and original DSP accuracy gates remain independent.

Fresh pre-change `consumer-cooperative-baseline-20261005.log` and post-change
`consumer-inline-final-20261005.log`, under the Libapta rewrite evidence root,
both pass 123 C tests, 279 ordinary Rust plus 55 external-C groups in each debug/
release profile, 34 WAV interchange cases and two all-feature CLI cases, with
formatting, Clippy, no-default-features and allocation instrumentation. C remains
unchanged. Expanded unknown-duration sparse ownership and C allocator/layout/ABI/
packaging compatibility are still unfinished separate workstreams. The diagnosed
portable key boundary and real S6 rejection are unchanged.

Optimized AddressSanitizer checks (`consumer-inline-asan-20261005.log`) pass
38 ordinary core/session/allocation/WAV tests; ignored external-C tests are not
counted in that sanitizer run. Portable AArch64 no-default-features compilation
passes (`consumer-inline-aarch64-20261005.log`). Broader runtime/C sanitizers,
i686 and Windows execution were not rerun; consumer ASan/P4 acceptance follows
the immutable pin separately.

Final immutable-pin consumer acceptance: `20261005T064308Z` passes 446 workspace /
110 focused release tests, 36 adapter tests plus one lifetime compile-fail doctest
under ASan, formatting/Clippy/allocation and pinned Rust 1.95 P4 checks. Native
FAT32/exFAT retained outputs now also exercise foundation Deck transport-lane
backpressure: refusal preserves Deck state, and retry emits the exact seek once.
Neither provider selection nor broker-client ownership is enabled.

The complete job keeps both scan and worker control in caller slots. Standard
RV32 future sizes are 4040/4288 B, pools 4416/4664 B, external scan control
672/696 B and worker control 4264/4288 B (FAT32/exFAT). Polling is 8432/8800 B;
preparation is 128 B, making the identified subtotal 8560/8928 B before further
callees. Arrays + pool + external control + diagnostic backend are 118716/119020 B;
no control is counted twice. `write_in_place` frames are separately 4416/4672 B.
Three host complete-task tests include actual Embassy pool initialization/executor
execution without measured allocations and first-pass cancellation recovery.
This does not execute target esp-rtos or P4 hardware. A separate actual combined
firmware compile/link inventory records entry/executor/interrupt disassembly and
4211 frames, with APTA absent; it cannot be added blindly to the task diagnostic
as a complete stack maximum. Large foundation owner frames, indirect callees,
interrupt nesting, simultaneous workloads, placement and physical timing remain.

Reproduction, exact artifacts, revised source-resolved diagnostic scripts,
exclusions and publication are recorded in the consumer README and complete
handoff `pajoniiir-libapta-native-rust-continuation-2026-10-05-0645.md` under
`/home/shome/.local/share/libapta-audio/handoffs/`. Core source remains the published
consumer pin `e93e667ba010d7256e06582f05cf5da0b94846e5`; this final addition is docs only.

## Cooperative scan and consumer effect acknowledgement — 2026-10-05

Pajoniiir's pinned native consumer now drives both WAV passes cooperatively. The
new borrowed `ScanRead::run` yields after each incomplete bounded scan step and
poisons a scan cancelled at either a read or its own yield. Caller control retains
the handle for explicit abort/recovery; successful scanning still requires the
existing framing/hash/duration verification and first close. The driver never
resets limits or selects an executor. Manual scan APIs retain their existing
contract. No portable source, C algorithm/header/ABI/container or Cargo pin changes
were needed; source revision remains `e93e667ba010d7256e06582f05cf5da0b94846e5`.

The isolated consumer integrates the foundation's retry-safe Deck effect contract.
Native FAT32/exFAT output reaches actual Sync, Beat Jump and RGB565 consumers and
exercises transport queue refusal plus downstream failed acknowledgement. The
exact Seek remains queued until successful acknowledgement. This is a neutral
consumer transaction, not enabled firmware playback or device-command execution.
The foundation's existing exFAT dispatch works; a unique analysis broker lifetime,
pending/unexpected completion draining and exclusive handle/buffer recovery remain
owner contracts to establish before adding a production lane.

A remaining diagnostic copy came from explicitly forbidding inlining on the
complete-job async constructor, rather than a Libapta API gap. Allowing constructor
inlining reduces standard RV32 task polling from 8432/8800 B to 4432/4560 B for
FAT32/exFAT. Preparation remains 128 B; identified subtotals are 4560/4688 B before
further callees. Job futures remain 4040/4288 B, pools 4416/4664 B and initialization
frames 4416/4672 B. Arrays + pool + both external control slots + diagnostic backend
remain 118716/119020 B. A separate forced-frame-pointer/panic-abort profile measures
4432/4544 B polling; never mix the two profiles. No complete maximum, placement,
interrupt nesting, DMA/cache or timing acceptance follows from these inventories.

Consumer final-source `20261005T073545Z` passes 454 ordinary workspace tests plus
one lifetime compile-fail doctest, 114 focused release tests plus that doctest, and
39 adapter ASan tests plus that doctest; fmt/Clippy/allocation/P4 checks pass.
Exact raster artifacts `20261005T073645Z`, native/streaming probes
`20261005T073647Z` / `20261005T073652Z`, and complete-task `20261005T073653Z` pass.
The three host task tests include actual Embassy execution with another runnable
task progressing during analysis, zero measured allocations after setup and
first-pass read/yield cancellation recovery. Nested inventory
`task-nested-20261005T073656Z` has 1491 frames; actual combined firmware inventory
`firmware-frames-20261005T073659Z` has 4213 frames, with APTA absent.

Canonical consumer usage, commands and detailed limitations are in its adapter
README. Neither firmware nor Slint selects APTA. Broader native ownership evidence,
C allocator/layout/ABI/packaging and platform matrices, original DSP accuracy, and
physical P4 memory/timing coexistence remain independent unfinished workstreams.

Fresh unchanged-core combined acceptance
`consumer-scan-final-20261005T0738.log` passes 123 C tests, 279 ordinary Rust tests
and 55 external-C groups per debug/release, 34 WAV interchange and two all-feature
CLI cases, formatting, Clippy, no-default-features and allocation checks. Consumer
ASan/P4 checks above were rerun; library/runtime/C sanitizers, AArch64, i686 and
Windows/MSVC were not rerun in this docs-only library continuation. Earlier
acceptance remains dated evidence, not a new execution claim.

## Complete scan ownership and unknown sparse growth evidence — 2026-10-05

Pajoniiir now shares a complete first-pass API between its host WAV example,
actual native FAT32/exFAT fixtures and the complete Embassy job diagnostic.
`SourceRequest::scan` requires an empty caller scanner slot, closes successful
scans, aborts scan errors and preserves poisoned control on cancelled reads or
cooperative yields. Cleanup errors take precedence; open/close completion and
backend recovery obligations remain explicit. The verified geometry boundary
still permits host allocation before synchronous Session preparation. Four new
complete-job tests cover cleanup/cancellation, occupied control, limits and
persistent generation ownership through repeated array reuse. Neutral Deck,
Sync/Beat Jump, retained views and raster behavior remain exact. No Libapta
production source, API, DSP, C product/header/ABI/container or consumer pin changes
were needed. Consumer source remains pinned to `e93e667ba010d7256e06582f05cf5da0b94846e5`.

The existing compiled `sparse_capacity_oracle.c`, already registered in
`rust/check.py`, now tests initially unknown duration as well as known duration.
For 63 and 4096 separated 64-frame fragments it retains the first-pass result,
fills every intervening hole, rejects a conflicting short EOF and finalizes at
8064/524288 frames. Complete wire output compares exactly among unknown-origin
C, known-origin C and native **known-duration** owning sparse processing. The
retained result preserves its original source duration, all span coordinates and
column bytes after merging, EOF and session destruction, and keeps its context
busy until released. This expands growth/retention evidence without introducing
native unknown-duration sparse ownership or claiming its scheduling, seeding,
musical or allocation-failure contracts. Those remain required before that change.

Consumer final-source `20261005T083100Z` passes 458 ordinary workspace tests, 118
focused release tests and 43 adapter ASan tests, each suite also passing its
lifetime compile-fail doctest. Exact artifact/native-source/streaming/task stamps
are `20261005T083148Z`, `20261005T083150Z`, `20261005T083155Z` and `20261005T083156Z`.
Standard RV32 polling decreases to 4256/4336 B, while job futures increase to
4120/4392 B and pools to 4496/4768 B. Preparation remains 128 B; initialization
is 4496/4768 B. Arrays + pool + both external controls + diagnostic backend total
118796/119124 B. This is an explicit resident-storage/polling-temporary tradeoff,
not a maximum stack or placement claim. The separate nested inventory has 1491
frames; actual combined firmware has 4213 frames with APTA absent. Detailed
profile-specific evidence/exclusions remain in the consumer adapter README.

Library final-source acceptance `unknown-growth-final-20261005T0833.log`, explicitly
using `RUSTUP_TOOLCHAIN=1.95.0`, passes 123 C tests, 279 ordinary Rust tests plus
55 enabled external-C groups per debug/release, 34 WAV interchange cases and two
all-feature CLI cases, fmt/Clippy/no-default-features/allocation checks. Counts
stay unchanged because the registered capacity group now tests both duration
origins. The pre-edit unchanged-core runner passed in
`consumer-composed-final-20261005T0826.log`. Focused native/C growth comparison is
`unknown-growth-focused-20261005T0829.log`.

`unknown-growth-asan-20261005T083627Z` passes four ASan/UBSan/leak-checked capacity
oracle executions (known/unknown origin at 63/4096 fragments) with exact paired
wire bytes. Only the oracle is sanitizer-instrumented; its unchanged linked C
archive is not. Reproduction is `verify-unknown-growth-asan-20261005.py` in the
external rewrite evidence root. Broader core/runtime/C sanitizers, AArch64, i686
and Windows/MSVC were not rerun. No physical hardware was operated. Original DSP
accuracy and all C allocator/layout/ABI/packaging gates remain separate.


## Native request reservation and PCM cursor consumer — 2026-10-05

`Scheduler::replace_request_storage` grows borrowed or owned storage without
allocation in core. It preserves all request slots, including terminal records,
IDs, enqueue order, priority aging and focus. Oversized replacement (>16) or
shrinking storage fails before mutation. `request_capacity` exposes the logical
slot capacity. The existing borrowed constructor and default ceiling are unchanged.

`OwnedSparseSession::reserve_requests` and the scheduled pull owner can now grow
an initially small (including zero-slot) request table while Created/Running.
Minimum and actual Vec-capacity bytes must fit the aggregate working-byte limit;
allocation/quota failure leaves work, results, scheduler and accounting unchanged.
Dirty mirrors still block mutation. No implicit growth, terminal-slot recycling,
new generation or source callback occurs. The limit bounds committed storage,
not transient coexistence of old and replacement arrays. C remains unchanged:
its fixed sixteen-slot table is the policy oracle, not an allocation model.

New acceptance covers exact public-C request/demand/terminal traces across sixteen
incremental reservations, preserved priority aging/focus/IDs, quota and allocator
failure/retry, retained generations, live PCM and scheduled-source ownership.
Existing allocation instrumentation also covers borrowed core replacement.
Initially unknown sparse ownership and its scheduling/seeding/musical/failure
contracts remain separate; this change closes only explicit growth of a smaller
native request table within the existing ceiling.

The isolated Pajoniiir streaming worker now advances a cursor over retained PCM
bytes. It compacts only the incomplete frame before another read, moving at most
seven retained bytes for the supported mono/stereo formats instead of moving the
whole unread suffix after each short queue acceptance. Decode/validation order,
one-read/256-frame/one-core-step bounds, both hashes/closes and cancellation
ownership are preserved. This is a byte-copy reduction, not measured throughput
or target timing acceptance. Its new format matrix covers all four PCM formats,
mono/stereo, ordinary/extensible reordered framing, one-byte/odd/full reads and
one-/seventeen-frame queues, including a partial final waveform column. Actual
fragmented FAT32 and contiguous/fragmented exFAT also feed every format to the
existing retained Deck/Sync/Beat Jump/renderer path. Detailed consumer contracts,
measurements and evidence remain in the adapter README.


Library acceptance: `request-growth-combined-final.log` passes 123 C tests,
282 ordinary Rust tests and 56 enabled external-C groups per debug/release,
34 WAV interchange and two all-feature CLI cases, fmt/Clippy/no-default-features
and allocation instrumentation. The pre-edit baseline is
`continuation-20261005-baseline.log`. `request-growth-asan-runtime.log` covers the
complete owning runtime including enabled external-C comparisons; focused portable
scheduler/session allocation checks are in `request-growth-asan-core.log`.
ASan/leak instrumentation applies to Rust, not the unchanged linked C oracle
archive. No AArch64, i686 or Windows matrix or physical hardware was rerun here.
Consumer final-source diagnostics and the external handoff follow separately.


Final owning-runtime ASan/leak acceptance is 62 cases, including enabled external
C comparisons; focused portable ASan is eight tests. Consumer acceptance on the
new immutable library revision `25d09de364e9507532c0508c838a8fecf0772d13`, stamp
`20261005T091217Z`, passes 459 workspace / 119 release / 44 adapter ASan tests plus
one lifetime compile-fail doctest in each suite. Original artifact bytes remain
exact (`20261005T091507Z`). Native/streaming/task probes are `20261005T091511Z`,
`20261005T091516Z`, `20261005T091517Z`; the last passes three complete-job host tests.

The cursor adds eight resident bytes to each measured RV32 worker control
(4272/4296 B FAT32/exFAT). Standard futures/pools/poll/preparation remain
4120/4392, 4496/4768, 4256/4336 and 128 B respectively. Arrays + pool + both controls
+ diagnostic backend total 118804/119132 B. The separate forced-frame-pointer
inventory (`task-nested-20261005T091525Z`, 1491 records) has 4256/4352 B polling:
exFAT grows 16 B in that profile only. Actual combined firmware inventory
`firmware-frames-20261005T091530Z` remains 4213 records with APTA absent. These are
software diagnostics, not complete call-path maxima or hardware budgets. Unique
foundation-owned analysis-client/recovery, production retained storage/provider
selection, original DSP accuracy and C compatibility gates remain unchanged.

## Pajoniiir owned analysis-client contribution — 2026-10-05

The isolated consumer now supplies the missing analysis ownership bridge in its
foundation `pajoniiir-media-fs` crate and USB0 broker wiring. A dedicated once-only
endpoint reuses the existing request queue and native FAT32/exFAT engines. The
client retains original lease, pending operation/ticket, actual handle and owned
buffer across cancelled futures; explicit recovery drains and closes before
reuse. Unexpected completions are retained rather than discarded, stale reads
cannot write consumer scratch, and logical file/ticket generations do not wrap.
Short read limits preserve whole-buffer ownership at WAV framing/EOF boundaries.

Existing SourceRequest/StreamWorker APIs consume this AsyncFileSystem bridge
without a portable-core change. Both full-object hashes and both closes still
gate delivery. Existing retained banks, Deck pins, neutral transport and waveform
consumers are reused. The actual FAT32/exFAT fixtures traverse the bridge; host
recovery/repeated-job tests and complete Embassy task diagnostics cover its
software ownership path. Consumer README owns the exact contracts and acceptance.

Libapta source, C oracle, public API/ABI, container, numerical behavior and pinned
portable revision remain unchanged. No product broker/executor policy enters
Libapta and desktop libapta-runtime remains excluded from firmware. The contribution
is reviewable without enabling the production provider. Owner adoption of the
lane/lifetime contract, persistent task and SRAM/PSRAM bank placement remain
separate decisions; hardware DMA/cache/interrupt/coexistence/timing, original DSP
accuracy and deferred C compatibility remain independent gates.

Final unchanged-library acceptance `analysis-owner-combined-20261005.log` passes
123 C tests, 282 ordinary native Rust tests and 56 enabled external-C groups per
debug/release, 34 WAV interchange cases, two all-feature CLI cases, formatting,
Clippy, no-default-features and allocation instrumentation. Consumer acceptance
`20261005T094935Z` passes 463 workspace / 122 focused release / 47 adapter ASan
tests plus the lifetime doctest in each suite. Complete Embassy host and P4
probe `20261005T095219Z`, nested `20261005T095224Z` and actual combined firmware
`20261005T095227Z` pass. The consumer README records the separate broker profile,
resource exclusions and reproduction; these do not qualify physical P4 behavior.

## Pajoniiir persistent consumer owner — 2026-10-05

The isolated consumer now keeps the actual owned broker client, request generation
owner, catalog selection and reusable working-array borrows outside cancellable
jobs. It recovers the same endpoint before replacement/repeated work, drives the
existing two-pass native consumer, and exposes accepted output through separately
retained banks and the existing neutral Pin boundary. Logical job controls may be
dropped only because this concrete client preserves actual pending resources.
Both complete hashes and both closes still gate delivery; no timeout or USB/DMA
cancellation is implied by dropping a job.

The optional firmware compile path uses the actual USB0 Analysis endpoint,
foundation retained Library catalog, lifecycle snapshots and existing PSRAM
allocator/StaticCell storage mechanisms. It does not claim an endpoint at boot,
spawn analysis, or select the production provider. Repeated native FAT32/exFAT
fixtures and real host Embassy execution cover cancellation/recovery, generations,
retained pins and neutral consumers. Host delayed completions are not USB evidence.
The consumer README owns the exact contracts, diagnostics and reproduction.

Libapta code, numerical behavior, C/ABI/container and portable pin are unchanged;
no product dependency or desktop runtime enters the portable firmware path.
Owner adoption still decides triggers/scheduling, capacity/limits, retained-bank
retirement and quarantine/hung-backend handling. Physical memory/DMA/cache/interrupt/
audio-coexistence/timing, original DSP accuracy and deferred C compatibility remain
independent gates. Compiler inventories are not complete stack or hardware budgets.

Consumer final acceptance `20261005T102601Z` passes 465 workspace, 124 focused
release and 49 adapter ASan tests plus its lifetime doctest; original artifacts
remain byte-identical (`20261005T103149Z`). Complete host Embassy/storage
`20261005T103158Z` passes three tests; nested and ordinary firmware inventories are
`20261005T103203Z` / `20261005T103206Z`. Actual USB0 owner compile/link inventory
`owner-firmware-frames-20261005T103211Z` explicitly emits unselected code with
link-dead-code/forced frame pointers. The consumer README records its 5696-B future,
5752-B pool, retained-array/owner sizes and precise component exclusions. These
are different compiler profiles, not hardware budgets or total stack maxima.

Unchanged-library combined verification `persistent-owner-combined-20261005.log`
passes 123 C tests, 282 ordinary Rust tests plus 56 enabled external-C groups in
each debug/release profile, 34 WAV interchange and two all-feature CLI cases,
formatting/Clippy/no-default-features and allocation checks. No broader C/runtime
sanitizer or Windows/i686/AArch64/hardware qualification was rerun. The ordinary
combined owner-feature firmware link and both firmware dependency exclusions also
pass; all verification commands ran serially with at most two build/test workers.

## Pajoniiir long-lived owner handoff — 2026-10-05

The isolated native consumer now provides a real unspawned analysis service loop.
The retained Library catalog transfers only original lease/path/track identity;
its receiving persistent owner alone issues request generations. Bounded commands
apply latest-received replacement, explicit cancellation and recovery retry. The
same concrete Client preserves pending I/O and quarantine across job or recovery
cancellation, and drains/closes before replacement. Both full hashes and both
successful closes still gate output.

Retained banks move to the product boundary without self-reference or allocation.
Fresh media, read-only current delivery authority and actual Deck state gate
acceptance. Short neutral views borrow the bank; stopped/non-pending Deck state
and released views gate explicit retirement. No free bank rejects analysis rather
than reclaiming a playing/pending pin. Actual USB0 Embassy channels, Library enqueue,
ProductRuntime acceptance, PSRAM/StaticCell storage and infinite task compile under
`apta-owner-compile`; main does not claim/spawn/select this provider. See the
consumer README's **Long-lived owner handoff** for precise policy and diagnostics.

Native FAT32/exFAT fixtures run the loop through neutral consumers, and the host
Embassy executor exercises loop cancellation/replacement with a live heartbeat.
Host delayed completions are not USB/DMA execution. Libapta implementation, C,
ABI/container, numerical behavior and portable pin are unchanged; no product
policy or executor dependency enters Libapta. Production scheduling/triggers,
approved capacities and quarantine/hung-backend decisions require adoption review.
Physical placement/DMA/cache/interrupt/timing/audio coexistence, original DSP
accuracy and deferred C compatibility remain independent gates. Final acceptance
is recorded below and in the complete external handoff.

Consumer acceptance `20261005T141054Z` passes 468 workspace / 127 focused release /
52 adapter ASan tests plus two lifetime doctests per suite, formatting/Clippy/
allocation/P4 checks. Original raster bytes remain identical (`20261005T142018Z`).
Complete real Embassy loop/task probe `20261005T142027Z` passes all three tests.
Concrete actual-USB0 loop diagnostics `owner-firmware-frames-20261005T142108Z`
observe a 6344-B future, 6392-B pool and 1000-B Service (including Owner), with
7465 frame records under the distinct link-dead-code/forced-frame-pointer profile.
The consumer README owns exact channel/retained-array/component sizes and
exclusions; the 126500-B selected-component subtotal is not a total memory budget.
Ordinary firmware and nested inventories remain separate compiler profiles.

Unchanged-library `service-owner-combined-20261005.log` passes 123 C tests,
282 ordinary Rust tests plus 56 enabled external-C groups in each debug/release
profile, 34 WAV interchange and two all-feature CLI cases, formatting/Clippy/
no-default-features/allocation checks. Ordinary combined owner-feature firmware
link and both firmware dependency exclusions pass. Checks ran serially with at
most two build/test workers; no hardware or broader compatibility run occurred.

## Pajoniiir actual product-owner lifecycle — 2026-10-05

The isolated consumer now executes its existing firmware ProductRuntime together
with the native Service. The product owns accepted banks, uses their borrowed
neutral views in Deck controls/waveforms, and handles bounded catalog selection,
delivery refusal, media invalidation and recycling. Product intent revisions
prevent delayed same-track/superseded delivery without touching the service's
Requests counter. Retirement waits for stopped/non-pending transport, an empty effect lane and
completion of its one ticketed in-flight operation at the audio/timeline boundary.
Downstream queue acceptance alone cannot release Cue's pending Pause/Seek pin. Recycling
backpressure keeps ownership in bounded product slots.

Host tests source-include the actual owner; native FAT32/exFAT fixtures and the
real host Embassy probe use it. The optional P4 compile path wakes the existing
controller/product owner for catalog, service and media events while retaining
its pending USB wait future. No analysis endpoint is claimed, no analysis task
is spawned, and no production provider is selected. The consumer README's
**Actual product-owner lifecycle** defines policy and remaining gates.

Libapta implementation, portable pin, DSP, C/API/ABI and container are unchanged.
Software evidence does not qualify physical placement, DMA/cache, interrupt/FP
nesting, timing/audio coexistence or original DSP accuracy. Compiler inventories
remain profile-specific, not total stack or hardware budgets. Production adoption
still approves capacities, triggers and backend quarantine/hung-operation policy.

Final consumer acceptance `20261005T155814Z` passes 470 workspace / 129 focused
release / 54 adapter ASan tests, plus two lifetime doctests per suite and
formatting/Clippy/allocation/portable-P4 checks. Native filesystem fixtures execute
the actual product owner; the real host Embassy probe passes at `20261005T160654Z`.
Concrete USB0/product inventory `owner-firmware-frames-20261005T160723Z` records
6352/6400-B Service future/pool and 13400/13448-B actual USB1 owner future/pool,
including its 6728-B ProductRuntime. The consumer README gives exact individual
frames, storage, profile configuration and exclusions; these are not system budgets.

Unchanged-library `product-completion-combined-20261005.log` passes its complete
combined Rust/C, interchange, formatting/Clippy/no-default/allocation checks.
The serial consumer remainder also passes raster identity, native/streaming/task
probes, ordinary firmware link and default/owner dependency exclusions. No hardware,
production activation or broader legacy compatibility qualification is implied.


## S6 coverage and independent DSP evaluation — 2026-10-05

The [source-linked evaluation](DSP-COVERAGE.md) reproduces the original consumer
rejection and portable key boundary, then establishes exact C/Rust payload parity
across 30 known/unknown/dynamic S6 window/EOF cases in debug and release. Complete
source input and Final/declared coverage can coexist with missing segment timing;
full-length segment cases still disagree with local meter. The original 320000-
frame fixture ends segment timing at 262144. This is inherited C behavior, not a
justified Rust-only correction. No production DSP, C/API/ABI, portable backend,
consumer source or immutable dependency pin changes.

The new `rust/tests/s6_coverage.rs` runs automatically in both external-C phases
of the combined runner. Optional external evidence output preserves original PCM,
paired containers and CSV measurements without overwriting a previous run. The
unchanged consumer one-step rejection, eight-backend key audit and high-precision
coefficient diagnostic were rerun separately. A bounded one-step C drain exhaustion
at the 64-bin minimum is retained as a separate observation, not normalized into
parity or hidden by changing its guard. Existing native termination distinctions,
clock/mask/retained-result tests and allocation instrumentation remain authoritative.

Evidence root:
`/home/shome/.local/share/libapta-audio/rust-rewrite/dsp-coverage-20261005/`.
The dated external handoff owns publication/CI and exact source hashes. This is a
bounded reproducible evaluation and regression addition, not renewed musical
accuracy acceptance. Original DJ corpus failures, interior/ring coverage, consumer
owner adoption/production activation, hardware and deferred C compatibility gates
remain independent. Keep rejection explicit; extending the tail does not repair
meter binding or prove timing accuracy.
