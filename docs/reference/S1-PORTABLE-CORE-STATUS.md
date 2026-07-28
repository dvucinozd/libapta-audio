# S1 portable core API status

**Architecture stage:** S1 — Portable core API  
**Stage status:** Functionally complete implementation candidate  
**API version:** 0.1.0 draft  
**Verified merge commit:** `bb818bbffba75044dc33bace9bfad452e82a32de`  
**Verification evidence:** GitHub Actions PR CI run `#195` completed successfully  
**Registered runtime tests:** 45

## Roadmap mapping

The architecture draft defines Stage S1 as:

- opaque handles;
- allocator abstraction;
- PCM push and pull;
- bounded process API;
- cancellation;
- immutable result snapshots.

All six functional items now have reference implementation and runtime coverage.

## Opaque handles and versioned API

The public API exposes opaque context, session and result handles. Public structures use `struct_size`, `api_version` and reserved fields so the 0.1 prototype can reject incompatible prefixes and append future fields deliberately.

C11 and C++11 public-header compilation and ABI-layout checks run in CI. Stable API/ABI status is not yet claimed.

## Allocator and bounded memory

Contexts accept a caller allocator and enforce configured memory limits.

Sessions support:

- the ordinary context allocator path;
- caller-owned mutable session workspace;
- a known-duration, preallocated two-slot immutable result pool;
- deterministic `APTA_ERROR_RESULT_SLOTS_EXHAUSTED` retry semantics.

The bounded path performs no context allocator callback after successful creation for metadata, PCM input, cooperative processing, WOVR/WDTL publication, result acquire/release, serialization into caller storage or session destruction.

## PCM push

Push sessions accept caller-supplied PCM blocks through `apta_session_push_pcm()`.

The implementation provides:

- mono and stereo input;
- S16, packed S24 little-endian, S32, interleaved F32 and planar F32 conversion;
- copied queue ownership;
- partial acceptance and backpressure;
- sparse and out-of-order non-overlapping delivery;
- explicit end-of-input signalling;
- feature-specific detail replay.

## PCM pull

Pull sessions attach `apta_pcm_source_t` through `apta_session_set_source()` while still in `APTA_SESSION_CREATED`.

`apta_session_process()` then:

1. asks the existing scheduler for the highest-ranked missing source-frame range;
2. invokes at most one synchronous `read_frames()` callback;
3. validates the returned block and exact requested origin;
4. copies accepted PCM into the existing owned queue;
5. calls `release_frames()` exactly once after every successful read;
6. runs the unchanged waveform/detail processing and publication pipeline.

The source may return:

- `APTA_STATUS_OK`;
- `APTA_STATUS_WOULD_BLOCK`; or
- `APTA_STATUS_END_OF_INPUT`.

Known length may come from session configuration or optional `get_total_frames()`. Unknown-length sources terminate through the end-of-input callback result. Malformed callback output becomes `APTA_ERROR_SOURCE` and publishes a failed lifecycle generation.

Push and pull ownership are mutually exclusive. Pull mode also works with static session workspace and bounded immutable result slots.

See [`../api/APTA-PCM-PULL-0.1.md`](../api/APTA-PCM-PULL-0.1.md) for the detailed callback contract.

## Bounded processing

`apta_session_process()` accepts maximum input-frame, maximum step and optional soft-time budgets.

The caller controls when processing occurs. The core does not own a worker thread, event loop, decoder, filesystem, USB host or playback scheduler.

A pull callback must itself be nonblocking or cooperatively bounded because the core cannot preempt an external callback.

## Cancellation

Cancellation uses an atomic request flag and is observed at bounded API boundaries.

A cancellation visible at pull-process entry is handled before the source callback. A cancellation requested during a callback is observed after that callback returns.

## Immutable results

Each publication produces a monotonically numbered immutable generation.

Results may be acquired and read concurrently with processing and may outlive their session. Context destruction remains busy until every session and independently acquired result is released.

Ordinary heap-backed results and fixed-slot pooled results share the same public lifetime and accessor contract.

## Verification

The two PCM pull tests cover:

- successful pull session creation and memory queries;
- missing-source rejection;
- known-length and unknown-length input;
- `WOULD_BLOCK` and retry;
- automatic known-length completion;
- callback-driven unknown-length completion;
- empty known sources;
- requested-frame budget limits;
- exact successful-read/release pairing;
- malformed block origin and failed session transition;
- push/pull API exclusion;
- static workspace and bounded result-pool operation;
- exact two callback allocations for context plus pool;
- no later allocator calls;
- retained final-result lifetime after session and workspace destruction.

CI run `#195` completed:

- GCC build and all 45 runtime tests;
- Clang AddressSanitizer and UndefinedBehaviorSanitizer execution;
- canonical seed generation; and
- bounded parser fuzz smoke.

## Remaining project work outside S1

Completing S1 does not establish APTA 1.0 or a certified resource profile.

Open work includes:

- three-band waveform processing;
- onset, BPM and beatgrid analysis;
- desktop tools and decoder adapters;
- measured embedded memory, stack and latency reports;
- independent ESP-IDF and second-platform integrations;
- cross-endian and shared-library export evidence;
- long-running fuzz campaign evidence;
- stable API/ABI and specification governance.
