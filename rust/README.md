# Native Rust implementation (in progress)

See the [canonical migration checklist](../docs/rust/MIGRATION.md). The C
implementation remains the installed product and comparison reference. This
crate performs its own PCM, waveform, default musical analysis, WAV and container
work; it does not link C.
It is not yet a full API/ABI or conformance replacement.

## Build and verify

From the repository root:

```bash
cargo fmt --all -- --check
cargo clippy --workspace --all-targets --locked -- -D warnings
cargo test --workspace --locked
cargo check --workspace --lib --no-default-features --locked
python3 rust/check.py \
  --build-root /home/shome/.local/share/libapta-audio/rust-rewrite/combined --jobs 2
```

Use an appropriate external build directory on another machine. The combined
runner requires Python 3, CMake, a C/C++ compiler, Cargo, rustfmt and Clippy. It
builds/runs the C suite, compiles a test-only public C oracle, runs normal and
explicit C interoperability tests, tests optimized Rust, then validates eight waveform and nine musical
synthetic WAV-to-container cases with the strict C reader. `--c-build PATH`
reuses a C build directory. It currently targets POSIX static builds; Windows
and embedded verification remain separate migration work.

`cargo test` alone deliberately ignores the external-C tests; it does not
claim C interoperability. To use an existing oracle manually:

```bash
APTA_C_WAVEFORM_ORACLE=/absolute/path/waveform-oracle \
APTA_C_VALIDATOR=/absolute/path/tools/apta-validate \
APTA_C_CONTAINER_ORACLE=/absolute/path/container-oracle \
  cargo test --workspace --locked -- --ignored
```

The `rust-version` field is the intended minimum (1.81); this initial run used
1.97.1. Minimum-toolchain, Windows, ILP32 and ESP32 builds still need validation.
No `unsafe` or allocation occurs in core modules. `libm` is the only dependency,
needed for portable reference quantization. The isolated allocation-counter test
uses an unsafe allocator shim solely to count calls in the test executable.

## Desktop waveform example

```bash
cargo run --example wav_to_apta -- INPUT.wav OUTPUT.apta
```

The output path must not exist. The example accepts at most 256 MiB of input,
uses 32,768 source frames per column, and produces a final WOVR-only container.
It supports mono/stereo PCM S16/S24/S32 and IEEE F32 RIFF/WAVE, including
extensible format headers. It loads the file in desktop memory; the portable
WAV parser itself borrows input and decodes into caller scratch. Add `--music` for the default musical path documented below. Fingerprint
computation, metadata, and C analyzer CLI parity remain outside this example. Empty input has no serializable waveform and is rejected by the example.

## Native interfaces

- `waveform::PcmView`: the five existing PCM representations, validated geometry,
  mono/stereo reduction, rejecting nonfinite floats.
- `session::Session`: known- or unknown-duration sequential push with caller-owned queue and
  output storage. `push_pcm` copies an accepted prefix. Use returned frame count
  to retry the suffix. `process` checks cancellation between steps of at most
  256 frames; zero budget fields are unlimited. `process_with_clock` accepts a
  caller nanosecond clock and cooperative microsecond deadline; it includes the
  four C analysis-stage checks even while those stages are disabled.
- `pull::PullSession` owns one `PullSource` and releases each acquired `PullBlock`
  once through its drop guard. Each call reads/processes at most 256 frames in
  one step within the budget; WouldBlock is retryable, errors terminal. Known
  lengths complete without a final read. Unknown lengths use a one-frame EOF
  probe at capacity; excess data is released and reports `BufferTooSmall`.
  No source block survives the call. `process_with_clock` starts the deadline
  after source read/release, as in C. Callbacks must return promptly; random access
  uses `ScheduledPullSession`. The C callback ABI remains pending.
- `columns()` borrows completed columns. `copy_snapshot_into()` copies to
  separate caller storage so snapshots survive subsequent processing and drop.
  A native push may use a different PCM representation each time; a future C
  adapter must enforce its configured immutable sample format.
  Use `session::TOTAL_FRAMES_UNKNOWN` for unknown duration; `finish_input()`
  resolves it to accepted frames. Output storage fixes the maximum accepted
  duration. Exhausting that capacity returns `BufferTooSmall` for a nonempty
  retry, while a full input queue returns zero and can be drained.
- `container::Container::parse` validates framing, directory/CRCs and registry;
  **it does not validate all section payloads**. `parse_waveform_result` validates
  WOVR, META and all WDTL payloads, including global tile identities and aggregate
  limits; it rejects other recognized analysis sections pending implementation.
  `parse_waveform` performs the same validation and returns just the overview.
- `meta` validates deterministic CBOR, including unknown nested values, UTF-8,
  minimal encoding, ordering and fixed resource limits. `Metadata` borrows
  recognized fields; `copy_to` copies them into caller storage. Unknown keys
  are ignored by the typed view; `copy_canonical` preserves their validated bytes.
- `detail` validates level-1 WDTL (256 frames/column, 64 columns/tile), sparse
  tile coverage, states, confidence and packed ranges. This is interchange;
  native eager detail analysis is also available for sparse sessions; detail
  request scheduling and replay are available through attached sparse publication.
- `write_waveform` writes WOVR only; `write_waveform_result` adds optional WDTL
  and META in canonical order. Both use caller buffers, valid only on success.
  `ParseOptions` bounds input, sections, spans, aggregate columns and detail
  tiles; default 1,024-span/tile limits bound quadratic interval/identity checks.
  Validated detail lookup does not rerun payload validation.
- Native permissive waveform/detail views clear reserved column flags and absent
  band bytes before canonical writing. META unknown values obey stricter
  deterministic validation than the C skip walker. Detail rejects whole columns
  beyond EOF even where C accepts them; only the last column may be truncated.
- `wav::Wav` parses a borrowed byte slice. Its native invalid-magic and
  beyond-EOF errors differ from the C adapter statuses; it is not an ABI adapter.

Quantized waveform fields and golden container bytes must match exactly; tests
do not hide differences behind a floating point epsilon. Later DSP stages need
separate documented tolerances and original algorithm acceptance gates.

## Complete container results and streaming

`result::parse` validates the full current wire section set, including TEMP/LGRD,
GGRD/REVN and MKEY/MTRD/CONF. `result::write` preserves wire-reader acceptance and
canonical bytes. `builder::finalize` adds stricter external-result validation and
writes independent caller-owned bytes; it currently covers the serializable
subset, with one grid coverage range and required waveform overview.

`result::read_from_stream` uses `stream::Input`, caller scratch, caller directory
slots and a retained payload arena. All sections are framed and CRC-checked;
only requested payloads and validation dependencies occupy retained storage.
The result survives source destruction. Unrequested dependencies are hidden and
quality records are filtered. `stream_write::write` emits canonical records
through `stream::Output` without a complete output buffer or heap allocation.
It validates before callbacks and handles partial progress, stalls, seek and
flush errors. The destination must already be empty/truncated; the host owns
transactions and filesystem durability.

These are native Rust APIs. Full C builder metadata/lifecycle, generation pools,
allocator callbacks and ABI parity remain pending. See the migration acceptance
matrix for exact current commands, evidence and deliberate reference differences.
Set `CARGO_TARGET_DIR` outside the checkout for focused Cargo commands too, e.g.
`/home/shome/.local/share/libapta-audio/rust-rewrite/combined/cargo-target`.


### Native result ownership

`native_validation` validates native graphs, including representations outside
the container subset. `owned_result::{requirements, copy}` uses caller-provided
typed storage; `OwnedResult::replace` is transactional, and `view()` borrows an
immutable native graph. Provenance and metadata are deep copied. Only referenced
waveform/detail data is retained, with compacted offsets. Independent owners
support concurrent readers. `result::from_native` validates conversion to the
wire subset using caller-provided tile descriptors; unsupported representations
fail explicitly. These types do not define the C ABI or allocator interface.


### Immutable sequential publication

`publication::plan` checks complete overview capacity and per-slot retained bytes.
`ResultPool` owns two caller-provided typed stores; `PublishedSession` publishes
initial/state/waveform generations. `acquire()` returns immutable cloneable leases
that can survive session destruction. Holding old leases can exhaust the pool;
`process()` may consume more queued PCM before retrying the accumulated snapshot.
Inspect processed-frame counts after such an error. Cancellation and completion leave
session state unchanged when their publication fails. Pool control uses `RefCell`
and is single-threaded; one pool belongs to one session lifetime. This does not
implement the C concurrent acquisition, allocator or workspace-layout contracts.


### Optional waveform analysis storage

`enable_three_band` attaches caller-owned `band::BandSums` before input to
`Session`, `PullSession`, `SparseSession` and their publication wrappers. Allocate
one entry per overview column (sequential sessions require the output capacity).
The filter follows actual processing order continuously across sparse seeks.
Quantized columns match C exactly; native available-feature masks derive the
three-band bit from column flags, unlike the C bounded pool which omits that bit.
Feature-enabled sparse overview seeding is supported: create the published sparse
session, attach band and/or detail storage, then call `seed_from_result` while
Created, before PCM or source processing. For scheduled pull, wrap the seeded
Created session in `ScheduledPullSession::new` before processing. Repeated seeds
are allowed while Created. Attaching features after installing a seed is rejected.
Only overview peaks/RMS/clipping and accepted coverage are reconstructed. Published
checkpoint band bytes/flags and detail tiles are ignored, matching C: seeded
columns have zero band bytes with HAS_3BAND when bands are attached, the filter
starts fresh on new PCM, and detail starts empty. Request/replay can recover detail
in seeded overview ranges without changing overview/bands or processed-frame counts.
Seeding itself publishes no generation and copies no lineage or musical state.

`Session` and `PullSession` expose `enable_detail` and `copy_detail_into` for
eager detail with known or unknown input length. `PublishedSession::enable_detail` and `PublishedSparseSession::enable_detail`
attach four `DetailTile` cache entries,
four native tile descriptors and 256 columns of publication scratch. Both result
slots need four detail descriptors and 256 detail columns in addition to overview
storage. Attachment checks aggregate column and retained-byte limits. Accepted
PCM is accumulated eagerly; processing refreshes completed columns and publishes
immutable copies. Completed sparse sessions may retain Partial detail tiles, as
in C; external completed-result builder validation remains stricter. Detail
requests and focus protect resident tiles; public PCM demands prioritize aligned
detail replay. Replay changes only detail, leaving overview and band history intact.
The built-in scheduled pull loop uses overview gaps, matching C; its public demand
query still exposes replay. C's public replay demand uses
aged/deadline scores while replay acceptance uses raw priority/FIFO; this native
port preserves that distinction, including possible rejection of the publicly
selected replay when those orders disagree.
No implicit allocation occurs in these paths. See the migration checklist for
exact acceptance evidence and the remaining core work.


The feature-enabled seeding milestone is bounded to C-compatible overview resume,
including immutable publication, replay and scheduled overview-gap reads. Native
seed preflight remains atomic, seeded tails stay within EOF, and band availability
comes from column flags despite C's bounded-pool mask omission. Exact C comparisons
run in debug and release through the existing seed and detail-pull oracles;
allocation instrumentation includes feature-enabled seed/resume/replay/retained
results. Evidence: `/home/shome/.local/share/libapta-audio/rust-rewrite/checkpoint-feature-seeding-combined-check.log`.
The full core and outside replacement remain unfinished; the continuation below advances musical lifecycle/request-mask acceptance. Nonbounded
ownership/concurrency/allocation classes, C ABI, tools and platform gates remain.


### Native musical stages

Sequential and sparse sessions and their publication wrappers now expose
`enable_tempo`, `enable_global_grid`, `enable_key`, `enable_meter` and
`enable_calibrated_quality`. Attach before input/seeding. Meter and calibrated
BPM quality require tempo. Default C onset/S4, S6, key, meter and calibration
algorithms process actual accepted/processed PCM; snapshots and wire sections
are produced by the integrated session, with cooperative analysis budgets.

S4 needs caller arrays of 4096 `analysis::OnsetBin` and 4096 f32 values. S6 needs
16384 bin/flux entries and 3072 `Beat` entries. Each enabled immutable slot needs
three tempo/key candidates, one local coverage/segment, one global coverage/eight
segments/3072 beats, one meter segment and one quality record. The core allocates
nothing. `publication::plan_features` now plans all default-profile typed arrays and both
retained slots. Attachment checks aggregate retained-byte/count limits and both
slots before initialization. These native sizes are not C ABI workspace offsets
or allocator classes.

Use `set_tempo_focus` and `lock_grid_range` for direct local-grid control.
`apply_grid_revision` accepts a Pending S6 conflict into the locked local grid;
wrong IDs conflict, repeated acceptance is InvalidState. Acceptance persists when
publication fails and `process` retries it. Serialize retained session results
with `result::from_session_result`; external builder validation remains strict.
Known/unknown sequential pull now drains musical work after EOF without rereading
or releasing blocks again. The portable two-slot pool remains single-threaded. Unknown-duration sequential
publication now accepts an explicit caller column ceiling and resolves source
duration atomically at EOF; retained earlier generations keep unknown duration.

Exact C evidence covers seven wire section types and a complete integrated
container, plus focus/locking and pending/applied revisions. Native lifecycle
checks cover retained results, exhaustion/retry, sparse evidence and one-step
pull completion. Evidence:
`/home/shome/.local/share/libapta-audio/rust-rewrite/continuation-combined-check.log`.
See [MIGRATION.md](../docs/rust/MIGRATION.md#integrated-musical-analysis-continuation--2026-10-04)
for storage, termination differences, exact acceptance boundaries and next gates.
The continuation below advances musical request/mask/generation traces, feature
workspace planning and native concurrent readers. Full ownership/C ABI/tools/
platform replacement remains unfinished. Original algorithm accuracy and hardware gates remain open.

### Musical lifecycle, planning and desktop runtime continuation

Use `ResultPool::new_with_requested_features` for explicit C requested-capability
publication: unrequested musical payloads are omitted; confidence, locking and
dynamic availability follow requested capabilities; calibrated quality appears
at EOF. Ordinary `ResultPool::new` retains native content-derived availability.
Requested locking errors follow C validation order. Attach the stages explicitly;
the mask does not allocate or initialize them.

Sparse musical focus and requests select overview PCM gaps while retaining their
original requested masks. S4 requests participate in overview work/progress;
key/global-only requests remain queued in the default C wrapper policy. Public
musical demands target focus/request gaps, whereas automatic scheduled pull uses
the internal overview selector. This distinction is proved against compiled C.

`plan_features(config, features, limits)` returns optional working-array counts,
per-slot native graph requirements and aggregate retained bytes.
`plan_with_capacity` plans unknown-duration sequential overview ceilings.
Attachment preflight accumulates all previously attached features atomically;
failed attachments preserve caller sentinel buffers and the session.

`OwnedResult::copy_to` preserves trusted session data and capability masks in
independent caller storage. The separate safe `libapta-runtime` workspace crate
provides `ConcurrentResults`: short locked acquisition of `Arc` generations,
actual sequential/sparse processing followed by copies, mirror-only retry after
short destination storage, and independent retained lifetimes. Core sessions
remain single-writer and allocator-free. The standard allocator owns Arc control
blocks; graph arrays remain caller-owned in that API. `HeapResult::copy_from` and
`HeapResults` additionally own every native array/text field on the standard heap,
with fallible copies, aggregate retained-capacity limits and independent concurrent
lifetimes. Their sequential/sparse process mirrors retry allocation/limit failures
without reprocessing accepted PCM. These APIs do not implement C custom allocators,
dynamically growing session workspaces or C concurrent acquire/release.

The desktop example now accepts `--music`:

```sh
cargo run --example wav_to_apta -- INPUT.wav OUTPUT.apta --music
```

It uses feature planning, actual PCM analysis, two immutable publication slots
and trusted serialization for default tempo/local/global/key/meter/quality.
Output must not exist. Its existing waveform mode is unchanged. This is a native
consumer example, not the full C analyzer/inspect/validate/corpus CLI replacement.

Exact lifecycle tests cover 53 PCM profiles, intermediate generation/masks,
cooperative clock samples, cancellation, exhausted initial slots and retry,
known/unknown EOF and scheduled sources. Long ring replacement, changing grids
and beat/segment caps compare quantized payload/wire bytes exactly. The final
combined runner includes eight waveform and nine musical WAV checks, with one
complete desktop musical container compared byte-for-byte to unchanged C.
Evidence and current totals are recorded in
[MIGRATION.md](../docs/rust/MIGRATION.md#musical-lifecycle-and-runtime-continuation--2026-10-04).
Full request-mask combinations, all-stage publication-failure traces, integrated
musical detail replay, near-limit coordinates, C allocation/workspace layout,
dynamically growing session storage/context contracts, C ABI/packaging and
platform gates remain open.
