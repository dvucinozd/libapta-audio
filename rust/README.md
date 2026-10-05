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
example cases plus eight waveform and nine musical native CLI cases with the strict C reader. `--c-build PATH`
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
No `unsafe` or allocation occurs in core modules. Core storage can also be owned by the separately allocating std runtime. `libm` is the only portable-core dependency,
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
full C allocation/context contracts, C ABI/packaging and
platform gates remain open.

### Owning sessions, context lifetime and native desktop commands

`libapta-runtime::GrowingSession` owns the existing portable session's queue,
overview, S4/S6 rings and beat arrays. Unknown-duration output grows on demand;
known duration reserves its overview at creation. `GrowingLimits` bounds queue
frames, output columns, actual mutable Vec capacities and each retained graph.
`enable_default_music()` allocates all five musical arrays before attaching any
stage. Native fixed ring/beat/segment caps retain the default reference policy.
Owning band/detail and sequential pull are supported below; owning sparse
scheduling remains pending.

Core `Session::new` remains caller-backed. `Session::with_storage` also accepts
owning array storage; `replace_queue` preserves a wrapped FIFO and
`replace_output` preserves complete columns, a partial accumulator and analysis
history. These operations allocate nothing. Attached bands must already cover
the replacement output; detail retains its coordinate ceiling.
`Session::snapshot(generation)` exposes an opaque trusted overview/detail/musical graph;
`copy_to` and `result::from_session_snapshot` preserve session validation. Metadata
still uses separate copy interfaces. Arbitrary external
builders and `HeapResult::view()` keep strict external conversion rules.

`RuntimeContext::create_session` tracks writers and retained heap graphs.
`close()` returns `Error::Busy` until all writers, channels and acquired graphs
release their resources. Context quotas count graph headers/actual array
capacities; mutable workspaces, transient copies and Arc/lock control allocation
are excluded. A context quota or snapshot allocation failure preserves the old
published graph. Inspect accepted/processed frames and retry only `refresh()`;
mutations are blocked until that mirror succeeds. Context ownership is native std
behavior, not C allocator callbacks/classes, workspace layout or ABI acceptance.

The portable key default still uses `libm`. `KeyMath` allows an explicit fixed
host backend; owning std sessions use platform `f32` cosine/log/square root.
A retained diagnostic exposes a one-unit portable MKEY score discrepancy against
this host's C libm; see the newest migration section. No tolerance hides it.

For S6, `Final` and full declared coverage do not guarantee full segment timing
or local-meter binding. The [coverage evaluation](../docs/rust/DSP-COVERAGE.md)
reproduces the inherited C limitation across EOF/window boundaries and separates
it from the portable key boundary and independent musical accuracy.

```bash
cargo run -p libapta-runtime --bin apta-native -- analyze INPUT.wav OUTPUT.apta --music
cargo run -p libapta-runtime --bin apta-native -- inspect OUTPUT.apta
cargo run -p libapta-runtime --bin apta-native -- validate OUTPUT.apta
cargo run -p libapta-runtime --bin apta-native -- version
cargo run -p libapta-runtime --bin apta-native -- corpus INPUT_DIRECTORY NEW_OUTPUT_DIRECTORY --music
```

The native CLI supports waveform/default music, full known-section inspection,
strict validation (or `--permissive`) and deterministic local WAV batch conversion.
Inputs are bounded at 256 MiB. Output files/directories must not exist; batch
failures retain successful outputs and return failure. These additive commands do
not replace C tool names/options, all feature-selection modes or frozen privacy/qualification corpus interfaces. Source hashing, typed metadata input and a native JSON summary are described below.
Final verification and precise remaining gates are in the migration document.

### Owning waveform features and sequential sources

`GrowingSession::enable_three_band()` and `enable_detail()` attach owning
workspaces before input. Band sums grow with unknown-duration overview storage;
queue, output and band replacements all allocate before committing growth.
Working limits account for actual capacities of bands, the four-tile detail cache
and musical arrays as well as queue/output arrays. Detail retains the reference
four-tile eviction policy: it does not preserve the entire track's detail in the
latest result. Previously acquired heap graphs retain their independent copies.

Actual `SessionSnapshot` graphs now include detail in fixed inline scratch
(four descriptors and at most 256 columns), alongside borrowed overview/music.
They remain allocator-free and trusted only through the actual-session path.
Metadata still uses separate interfaces. Generic externally supplied graphs
retain external validation; the portable key discrepancy described above remains.

`GrowingPullSession::new(writer, source)` accepts a Created owning writer with
features already attached and an existing `PullSource`. It reads at most 256
frames and performs one processing step per call. Known lengths finish without
an extra source read; unknown lengths resolve on EOF. WouldBlock is retryable;
source errors, malformed blocks and cancellation are terminal. Blocks release
exactly once before processing. Result-limit failures retain committed PCM and
old generations; `refresh()` or the next `process()` retries the mirror before
reading more data. Queued accepted PCM drains before another read. A failed
working allocation can rerequest the same absolute offset, since no PCM committed.
This native retry policy does not emulate C custom-allocator failure ordering.

Both owning push and pull expose `process_with_clock`; the pull deadline starts
after source release. Heap-copy/allocation and source callback time are outside
that cooperative processing budget. Owning sparse scheduling and capability
projection/mutations are described below; the C source callback ABI remains open.

The additive desktop commands accept independently combinable `--music`,
`--bands` and `--detail` flags for `analyze` and `corpus`. Detail exports the
resident cache only. Duplicate/unknown flags fail before output creation.
These options do not establish full C command-line or frozen corpus parity.

### Owning sparse sessions, replay and musical mutations

`libapta-runtime::OwnedSparseSession` owns the existing sparse engine and
scheduler. Configure a known source with `SparseLimits`, then attach bands,
detail and/or default music before input or seeding. Overview storage covers the
configured source. Queue nodes and accepted-range slots start at the configured
capacities; `reserve_pending(queue_nodes, range_capacity)` explicitly grows them
while Created/Running. Push keeps fixed-capacity backpressure until reservation.
`reserve_requests(capacity)` also grows an initially smaller request table, up to
the unchanged C-compatible ceiling of 16. Cancelled/satisfied records retain their
slots and IDs. Growth preserves aging, priority/deadline/FIFO order and focus;
it never reclaims terminal records or resets ID sequencing. The scheduled pull
owner exposes the same reservation without reading or releasing source blocks.
Construction, attachment and growth preflight all
fallible arrays and actual Vec-capacity bytes under `maximum_working_bytes`.
Growth preserves partially processed nodes, PCM, ranges and scheduler state;
it publishes no generation. The byte limit excludes transient replacement copies.

Use `push_at`, `request_region`, `cancel_region_request`, `request_progress`,
`set_focus` and `next_pcm_request`. Processing shares its budget across waveform,
S4, S6, meter and key. Detail replay changes neither overview evidence nor band
filter history or processed counts. `snapshot()` exposes a trusted actual sparse
graph; heap results retain independent overview/detail/music arrays. Failed
mirrors block further mutations until `refresh()` succeeds. A RuntimeContext can
create this writer with the same committed-result quotas and lifetime rules.

`seed_from_result` accepts a retained HeapResult while Created, after feature
attachment. It copies overview evidence only, imports no musical state or detail,
and leaves band filters fresh. It publishes no generation itself. Supplied source
identities must match when both sides have one; required identity rejects either
missing side. Geometry and source-length checks remain independent.

`OwnedScheduledPullSession` owns a configured Created sparse writer and source.
It drains accepted PCM and retries pending mirrors before reading. Each call
reads at most one 4096-frame demand, limited by the input budget, then processes
within the shared budget. Every acquired block releases once before processing
and clock initialization. WouldBlock retries; malformed blocks, source errors
and premature source EOF are terminal. Automatic reads use overview demand;
public `next_pcm_request` can instead expose detail replay. Reaching a selected
range with no missing PCM may finish with unrelated holes still visible.
Range-table exhaustion returns retryable `BufferTooSmall` after releasing the
unaccepted block; call `reserve_pending` and retry. No source failure is recorded.

Both owning writers and their pull adapters expose `set_tempo_focus`,
`lock_grid_range` and `apply_grid_revision`. Failed lock publication restores the
working lock. Revision acceptance persists after a failed mirror; retry
`refresh`, not the revision ID. Repeated successful locking is idempotent.

Call `set_requested_features` before attaching features. Its output rules match
the existing explicit requested-capability profile: bounded C for known input,
nonbounded C for initially unknown input. Default musical workspaces may be a
superset, but unrequested payloads are projected out and dynamic S6 follows the
requested bit. Initially unknown BPM keeps its derived local grid after EOF.
C's **nonbounded known-duration** BPM-only path also retains that grid; it is
not the profile emulated by this projection API. Ordinary native publication
remains content-derived. Snapshot projection shares the existing core rules.

See the [sparse continuation acceptance](../docs/rust/MIGRATION.md#owning-sparse-scheduling-replay-and-mutation-continuation--2026-10-04)
for exact C content/source comparisons, failure profiles and current totals.
Native heap publication still does not emulate C intermediate generations,
custom allocators or ABI ownership. Portable key score 55734 versus host-C 55735
remains unresolved. AArch64 core and i686/MSVC workspace cross-compilation are
compile evidence only; platform execution, linking/packaging and hardware gates
remain separate.

### Source identity and explicit sparse growth acceptance

`session::SourceIdentity::new(kind, bytes)` validates an optional 32-byte identity:
0 requires zero bytes, 1 is application opaque, and 2 denotes SHA-256 of exact
source-object bytes. The host supplies the identity; the library does not compute
or verify a hash. Core sessions accept it before input/seeding. Owning writers
use `new_with_identity`; RuntimeContext provides `create_session_with_identity`
and `create_sparse_session_with_identity`. Identity is fixed before the initial
immutable generation, persists through unknown EOF, and is preserved on wire.
Configure writers before wrapping either owning pull adapter.

For a single file, `apta-native analyze` accepts
`--source-identity=opaque:HEX` or `--source-identity=sha256:HEX`, with exactly 64
hexadecimal digits. `inspect` displays it. Duplicate/malformed identity flags fail
before creating output. `corpus` rejects a shared supplied identity; assign
per-file identities through individual analyze calls. This is identity transport,
not hash verification. Use `--hash-source` and `verify-source` below for actual source-object hashing; full C tool parity remains open.

See [capacity and identity acceptance](../docs/rust/MIGRATION.md#sparse-capacity-source-identity-and-consumer-continuation--2026-10-04)
for exact C comparisons, allocation/concurrency evidence and remaining gates.


### Source hashing, metadata and JSON inspection

```bash
apta-native analyze input.wav output.apta --music --bands --detail --hash-source
apta-native verify-source output.apta input.wav
apta-native inspect output.apta --json
apta-native corpus input-directory output-directory --hash-source
apta-native analyze input.wav output.apta --metadata-cbor=metadata.cbor
```

`--hash-source` uses SHA-256 of the exact bytes read for analysis, including WAV
headers and metadata, without rereading the source. It is opt-in and conflicts
with supplied identity. Corpus hashes each file independently. `verify-source`
requires SHA-256 identity and compares the full source object; absent, opaque or
mismatching identities fail. The 256 MiB input limit still applies. The std runtime
adds RustCrypto `sha2`; the portable core remains unchanged and allocator-free.

Metadata input is a canonical META CBOR payload, at most 8192 bytes, containing
only the seven typed fields supported by `libapta::Metadata`. Unknown fields,
noncanonical input, duplicate options and malformed payloads fail before output
creation. Metadata is attached to the exported container, not to live owning
session generations. This per-file option is rejected by corpus. `inspect` shows
its typed fields. `inspect --json` produces schema version 1: source/identity,
feature mask, partial flag, numeric FourCC bytes, overview/detail counts, selected
tempo and key/candidates. It is a summary, not a full graph or frozen corpus export.

### Publication retry and platform boundaries

Failed musical publications retry in waveform → S4 → S6 → meter → key order.
A pending key cannot consume a newly free bounded slot before an accepted grid
revision. Exact public-C failure traces cover both sequential and sparse wrappers;
owning mirror-only retry behavior is unchanged.

The numerical audit now records coefficients and 1250 intermediate Goertzel/chroma
snapshots for all eight cos/log/sqrt backend combinations. Portable coefficient
27 is `bf3fd897`, versus this host C's `bf3fd898`; the resulting selected score
remains 55734 versus 55735. No correction or tolerance change is applied.
Run `python3 rust/tests/fixtures/key_rounding.py` for the independent rounding
check and consult the latest migration handoff for compiled-C audit commands.

User-local i686 linking/execution is now available; default GCC x87 arithmetic
and the separately tested SSE2 profile are distinct. MSVC still needs Windows
SDK import libraries and actual execution; the Windows CI job now includes native
Rust release tests. No Rust C ABI, custom allocator, ESP-IDF or embedded stack
qualification follows from these desktop checks.


### Direct native Rust consumers

Depend on package `libapta` at an immutable Git revision for portable firmware;
`libapta-runtime` is the separate desktop `std` ownership layer. Native consumers
need neither C handles nor a C allocator or ESP-IDF runtime. Keep consumer model
conversion in the consumer repository; the existing C product stays intact.

`GridSegment::beat_at(index)` expands one declared beat with checked ordinal/Q32
arithmetic, skips a context anchor before applicability, and returns `None` past
`beat_count`. It rejects inconsistent coverage. It does not resolve hybrid grid
authority or validate a complete graph. `FractionalFrame::rounded_milliseconds`
uses the source sample rate, rounds once (nearest, ties upward), and returns a
checked `u64`; check again when a consumer has a narrower coordinate type.

The experimental Pajoniiir adapter uses caller-owned conversion banks, immutable
neutral analysis, explicit source/generation checks and the existing Deck and
waveform consumers. It is not production P4 enablement. See the latest native
consumer handoff in [MIGRATION.md](../docs/rust/MIGRATION.md).


### Experimental media-worker consumer continuation

Pajoniiir's separate native adapter now exercises the existing portable APIs
through a lease-checked worker, retained Deck generations, global segment/explicit
cache scratch and aligned sparse/detail waveform windows. This adds no product
coupling or desktop runtime to Libapta. Full contracts, real S6 rejection evidence
and remaining boundaries are in the [latest migration handoff](../docs/rust/MIGRATION.md#native-consumer-worker-and-extended-views--2026-10-04)
and the consumer's `firmware-rust/crates/pajoniiir-apta-adapter/README.md`.
Production worker enablement, hybrid override authority and physical P4/DSP
qualification remain separate. This continuation changes no portable DSP behavior.

The subsequent [catalog filesystem continuation](../docs/rust/MIGRATION.md#native-catalog-filesystem-consumer--2026-10-04)
uses Pajoniiir's existing native async filesystem and identity APIs to acquire a
bounded whole WAV object, then feeds the existing borrowed decoder/Session into
retained neutral consumers. Actual FAT32/exFAT host fixtures are covered. This
requires no portable-core change; whole-file capacity and production scheduling,
streaming codecs, hardware memory/timing and original DSP gates remain explicit.

### Constant-storage WAV framing and block decode

`wav::WavScanner::new(object_bytes)` consumes all object bytes sequentially via
`push`, retaining only framing and up to 40 format bytes. `finish` returns an
immutable `WavLayout` with the PCM byte range, geometry and encoding. Data-before-
format, extensible format, odd padding and opaque trailers follow borrowed `Wav`.
Framing errors poison the scanner; incomplete or excess input cannot finish.
Scanner storage is compile-time capped at 192 bytes. Work is linear in each
supplied slice; choose bounded slices for cooperative scheduling.

`WavLayout::decode` decodes complete frame-aligned byte blocks through the existing
borrowed decoder. It returns frames fitting output; nonfinite F32 in that prefix
fails before output changes. Caller owns byte coordinates, partial-frame assembly,
source identity and I/O. This adds no filesystem, hashing, allocator or DSP engine.
Pajoniiir's experimental two-pass reader hashes the full object on each pass and
withholds output until equal hashes and successful close. It uses 4096 byte and
2048 PCM-byte scratch instead of whole-track retention; core/output banks remain
additional. No production memory placement or latency qualification is implied.

Pajoniiir now applies explicit streaming object/duration/work limits, checked
local-tempo caller-array sizing and in-place verified output access. These remain
consumer policy; no portable API or numerical change is required. Full coroutine
and Embassy diagnostics expose substantially larger task/nested-stack needs than
individual step futures. See the [consumer policy continuation](../docs/rust/MIGRATION.md#consumer-policy-and-full-task-storage--2026-10-04)
for measured storage and remaining ownership/hardware gates.


The Pajoniiir consumer now prepares its local-tempo/meter profile directly into
an empty caller control slot and rechecks original media leases after async I/O.
Its native FAT32/exFAT consumers remain exact. Complete-task and nested-library
stack diagnostics are recorded in the migration section **Consumer lease and
construction integration — 2026-10-05**; they do not qualify firmware stack sizes.
Portable source and the immutable consumer pin are unchanged.


## Native construction and cooperative consumer — 2026-10-05

`Session::new` permits cross-crate inlining of its existing storage constructor.
This lets optimized native consumers eliminate large temporary Session results;
no API, storage lifetime, algorithm or C behavior changes. The measured effect
and exact verification belong in the [migration record](../docs/rust/MIGRATION.md).
Inlining is a compiler optimization opportunity, not a portable stack guarantee.
Pajoniiir owns cooperative I/O driving, cancellation and caller control placement;
those policies do not enter this portable core.

The native Pajoniiir consumer now uses cooperative borrowed drivers for both WAV
passes and the existing acknowledged Deck effect lane. No portable API or pinned
core revision changed in this continuation. See
[the migration record](../docs/rust/MIGRATION.md#cooperative-scan-and-consumer-effect-acknowledgement--2026-10-05)
for exact software evidence and the separate production, hardware and C compatibility
gates. Filesystem policy, executor composition and product ownership stay outside
this portable core.


The [complete scan and sparse-growth continuation](../docs/rust/MIGRATION.md#complete-scan-ownership-and-unknown-sparse-growth-evidence--2026-10-05)
adds shared consumer first-pass cleanup with caller-retained cancellation control.
The compiled C capacity oracle now covers initially unknown duration through
4096 disjoint fragments, hole merging, final exact native-known-duration wire
comparison and retained pre-EOF snapshots. Native unknown-duration sparse
ownership still needs scheduling/seeding/music evidence; this is not its implementation.

Pajoniiir now has a persistent native consumer owner and a compile-only concrete
USB0/PSRAM integration path, preserving request generations and retained banks
across cancellation and repeated jobs. This changes no portable API or pinned
core source. See [the migration record](../docs/rust/MIGRATION.md#pajoniiir-persistent-consumer-owner--2026-10-05)
for evidence and the remaining owner-adoption/hardware gates.

The isolated Pajoniiir consumer now has a bounded, long-lived owner handoff loop
and explicit retained-bank delivery/retirement. See the
[migration record](../docs/rust/MIGRATION.md#pajoniiir-long-lived-owner-handoff--2026-10-05).
It remains unspawned with the production provider unselected; Libapta's portable
implementation and dependency pin are unchanged.

Pajoniiir's optional compile path now retains native output in its actual firmware
product owner and tests catalog-to-Deck/waveform delivery and acknowledged
retirement on host. See [the migration record](../docs/rust/MIGRATION.md#pajoniiir-actual-product-owner-lifecycle--2026-10-05).
This introduces no portable-library or dependency-pin change and does not activate
the production provider.
