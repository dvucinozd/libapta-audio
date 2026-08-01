# M2 waveform processing status

**Milestone status:** Complete  
**Profile status:** Self-tested implementation candidate  
**API version:** 0.1.0 draft  
**Original M2 verification merge:** `b1c9100b2acee13188c650e71e6364bacbae7e7c`  
**Latest integrated verification merge:** `8fe19cfda514151880d658520912722db7edb99a`  
**Latest verification evidence:** GitHub Actions PR CI run `#224` completed successfully  
**Registered runtime tests:** 53

## Advertised capabilities

A context may advertise:

```text
APTA_FEATURE_WAVEFORM_OVERVIEW
APTA_FEATURE_WAVEFORM_DETAIL
APTA_FEATURE_BPM
APTA_FEATURE_LOCAL_BEATGRID
APTA_FEATURE_CONFIDENCE
APTA_FEATURE_GRID_LOCKING
```

The context exposes only explicitly requested supported capabilities. Detail and Stage S4 processing currently require overview because the overview path remains the authoritative PCM ownership and accepted-range layer.

## Implemented waveform behaviour

The current implementation provides:

- push-mode and pull-mode PCM input for mono and stereo sources;
- S16, packed S24 little-endian, S32 and F32 conversion;
- deterministic stereo reduction using `(L + R) / 2`;
- copied bounded PCM queue nodes;
- partial push acceptance and backpressure;
- non-blocking pull callback integration and `WOULD_BLOCK` propagation;
- overlap rejection plus feature-specific detail replay;
- out-of-order non-overlapping PCM delivery;
- explicit sparse accepted ranges and gap-preserving demand;
- playback focus and explicit priority-region requests;
- soft-deadline ordering within one effective-priority class;
- FIFO tie-breaking;
- deterministic bounded starvation aging;
- bounded cooperative processing by frame, step and optional soft-time budget;
- overview geometry of 1024 source frames per column;
- detail geometry of 256 source frames per column and 64 columns per tile;
- bounded four-tile detail cache with focus/request protection and deterministic eviction;
- sparse min/max/RMS accumulation;
- ties-to-even quantization and clipping flags;
- immutable overview spans and detail tile snapshots;
- bounded owned metadata snapshots;
- partial, stable and final waveform lifecycle states;
- progressive request status;
- final partial-column handling;
- publication retry after transient allocation failure or bounded slot exhaustion;
- concurrent immutable result readers during ordinary and pooled publication;
- cross-thread cooperative cancellation;
- measurable minimum and recommended memory requirements;
- context memory-limit enforcement.

## Stage S4 integration

The same normalized PCM pipeline now also provides:

- fixed-capacity 256-frame onset-energy bins;
- deterministic 40–300 BPM reference search;
- up to three ordered tempo candidates;
- independent confidence and half/double-time ambiguity flags;
- one local constant-period beatgrid segment;
- explicit requested, evidence, applicability and coverage ranges;
- focus-driven local-grid applicability revisions;
- stable grid-range locking;
- `PROVISIONAL → STABLE → FINAL` publication;
- identical push and pull behaviour;
- immutable heap and bounded-pool tempo/grid snapshots.

The complete Stage S4 evidence is documented in [`S4-TEMPO-LOCAL-GRID-STATUS.md`](S4-TEMPO-LOCAL-GRID-STATUS.md).

## Static workspace and bounded immutable results

Caller-provided workspace sessions keep mutable session state outside the context allocator:

- `apta_session_t`;
- accepted-range arrays;
- recyclable normalized PCM nodes;
- overview accumulator arrays, including 16-to-32 growth;
- session-owned metadata bytes;
- fixed-capacity Stage S4 onset bins.

The internal arena is max-aligned, first-fit, split/coalescing and compatible with existing generic cleanup paths through private allocation tags.

Known-duration sessions may additionally enable:

```text
APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS
```

This mode creates one context-owned, fixed two-slot immutable result pool before session creation returns. Each slot has checked capacity for:

- worst-case sparse overview spans and columns;
- four detail tile descriptors and 256 packed detail columns;
- three tempo candidates;
- one local-grid coverage range and segment;
- all public metadata fields;
- one immutable result object and alignment padding.

The pool is context-owned because acquired results may outlive the session and caller workspace. The session holds one owner reference and each active result holds one pool reference.

After successful bounded creation, metadata replacement, PCM push/pull, cooperative processing, WOVR/WDTL/TEMP/LGRD publication, acquire/release, serialization into caller storage and session destruction do not call the context allocator.

When application retention occupies both slots, publication returns `APTA_ERROR_RESULT_SLOTS_EXHAUSTED`. Releasing an older result permits deterministic retry without loss of metadata, PCM accumulation, tempo/grid state or request progress.

## Container implementation

The version-1 `.apta` implementation includes:

- canonical little-endian fixed header and section directory;
- CRC32C Castagnoli;
- required `WOVR` writer and hardened reader;
- optional `WDTL` writer and hardened reader;
- optional deterministic-CBOR `META` writer and hardened reader;
- optional `TEMP` tempo/candidate writer and hardened reader;
- optional `LGRD` local-grid writer and hardened reader;
- canonical writer → reader → writer byte identity for emitted section combinations;
- sparse and partial result preservation;
- source and application metadata retained after session destruction;
- strict range, offset, overlap, geometry, metadata, tempo, grid and reserved-field validation;
- configurable file, section, span, column and allocation limits;
- complete cleanup across injected WOVR, WDTL, META and Stage S4 allocation failures;
- exhaustive byte-prefix truncation and trailing-byte rejection;
- independently produced WOVR+META fixture with SHA-256 manifest and byte-identical library reserialization;
- sanitizer-backed bounded fuzz smoke with final, sparse-partial, WDTL, META and S4 seeds;
- canonical serialization and independent parsing of retained immutable results after session/workspace destruction.

## Runtime verification

The verified suite registers 53 runtime tests covering:

- core lifecycle, ownership and configuration;
- allocator failure cleanup;
- cancellation and cross-thread cancellation visibility;
- public initializers, ABI prefixes and version rejection;
- push and pull PCM ownership;
- metadata ownership, presence, validation and result lifetime;
- overview waveform semantics and block-boundary determinism;
- focus priority;
- deadline ordering for PCM demand and queued PCM processing;
- bounded starvation aging;
- detail geometry, request status, eviction and replay;
- memory requirements and memory limits;
- caller-owned session workspace placement;
- workspace PCM/range recycling;
- workspace overview accumulator growth;
- workspace session metadata ownership;
- result-pool layout, storage and slot lifetime;
- bounded initial, metadata, state-transition, WOVR and WDTL publication;
- deterministic slot exhaustion and retry;
- maximum public metadata payload and all four full detail tiles;
- concurrent pooled readers across 200 publications;
- pooled serialization after session/workspace destruction;
- ordinary publication retry;
- WOVR/WDTL/META canonical writing, parsing, malformed input and round-trip;
- WOVR/WDTL/META allocation-failure sweeps;
- exhaustive canonical container truncation;
- concurrent immutable result access;
- Stage S4 golden BPM and local-grid geometry;
- broad 40–250 BPM and 44.1 kHz tempo vectors;
- silence and insufficient-evidence rejection;
- explicit provisional, stable and final lifecycle;
- focus-driven applicability revision;
- stable grid-range locking;
- S4 push/pull parity;
- bounded zero-allocation S4 publication;
- TEMP/LGRD canonical round trip, malformed input and truncation;
- S4 parser allocation-failure sweep.

The evidence also includes AddressSanitizer/UndefinedBehaviorSanitizer execution, five canonical seed generations, bounded libFuzzer smoke, actual GCC/G++ `-m32` execution from the earlier foundation suite and the independent producer fixture workflow.

## Threading contract

The public prototype threading rules are documented in [`../api/APTA-THREADING-0.1.md`](../api/APTA-THREADING-0.1.md).

Result acquire/access/release operations may run concurrently with publication. Mutating session calls remain host-serialized except for explicitly thread-safe cancellation request/query operations.

Session destruction must not race with any operation receiving the same session pointer. Acquired immutable results may outlive their session, while the context remains busy until those results are released.

## Deliberate limitations

The implementation does not yet provide:

- waveform sources with more than two channels;
- three-band waveform values;
- multiple detail levels or dynamic tile-cache sizing;
- unknown-duration bounded-result sessions;
- configurable result-slot count;
- arbitrary application-defined or losslessly preserved unknown META keys;
- global beatgrid refinement;
- dynamic-tempo segments;
- explicit full-track beat arrays;
- stable API or ABI guarantees;
- measured resource-class or responsiveness claims.

## Conformance position

The bounded M2 waveform-processing milestone, S1 PCM push/pull scope, version 0.1 bounded immutable-result publication and functional Stage S4 scope are complete.

The implementation satisfies the currently listed functional requirements for `APTA-WAVEFORM-0.1` and is a self-tested candidate for the tempo/local-grid portion of the draft Core Profile. It does not make a formal profile or resource-class claim because independent implementation, broader fixture, cross-endian, shared-library, long-running fuzz, embedded-integration and measured resource evidence remain incomplete.

See [`../conformance/APTA-WAVEFORM-READINESS-0.1.md`](../conformance/APTA-WAVEFORM-READINESS-0.1.md) for the waveform requirement matrix and [`S4-TEMPO-LOCAL-GRID-STATUS.md`](S4-TEMPO-LOCAL-GRID-STATUS.md) for Stage S4 evidence.

## Next work

The next architecture and evidence sequence is:

1. Stage S5 POSIX source adapter and reference desktop tools;
2. measured embedded memory, stack and process-call report;
3. independent ESP-IDF integration validation;
4. cross-endian and shared-library export evidence;
5. maintained long-running fuzz campaign and retained corpus;
6. optional three-band waveform processing;
7. Stage S6 global-grid and dynamic-tempo refinement.

The completed result-slot contract is documented in [`../memory/APTA-BOUNDED-RESULT-SLOTS-0.1.md`](../memory/APTA-BOUNDED-RESULT-SLOTS-0.1.md).

## Types: double removed from the per-sample waveform path

The first hardware target is RV32IMAFC, which has no hardware double. Two
per-sample paths in the waveform code used it.

`apta_internal_waveform_accumulator_t.sum_squares` is now `uint64_t`, a sum of
squared sample magnitudes scaled by `APTA_INTERNAL_SQUARE_MAGNITUDE_SCALE`
(2^23). An overview column holds 1024 samples, bounding the sum at 2^56, a 256x
margin. `apta_quantize_rms()`, `apta_detail_quantize_rms()` and
`apta_pool_quantize_rms()` divide out the count and the scale; their arithmetic
stays `double` because it runs once per column rather than once per sample, and
the round-half-to-even step feeds canonical serialization.

The scale is 2^23 rather than the 2^15 used for onset magnitudes, and that
choice matters. A sample decoded from 16-bit PCM is exactly k/32768, so k * 2^8
is representable in `uint64_t` without loss and the conversion introduces no
error at all for such sources. Measured against the previous implementation,
published `minimum`, `maximum`, `rms` and `flags` are bit-identical. A 2^15
scale, which is what a naive reading of the requirement suggests, moved some
published `rms` values by one count out of 65535 -- small, but a silent change
to published data, and no test in the repository detects it because there are no
stored golden column values. `apta.waveform.determinism` compares two chunkings
of the same input against each other, not against a reference.

The per-sample format conversions `apta_normalize_s16()`, `_s24()` and their
counterparts in `apta_waveform_detail_replay.c` computed
`(float)((double)value / 32768.0)`. These run once per source sample per
channel, ahead of every other per-sample path. They are now single `float`
divisions. This is bit-identical rather than merely close: for 16- and 24-bit
input both the value and the divisor are exactly representable in `float`, so
IEEE 754 gives the correctly rounded result either way.

`apta_normalize_s32()` keeps a `double` division on its positive branch. A
32-bit magnitude exceeds float's 24-bit mantissa, so converting first would
discard bits. Its negative branch divides by 2^31, where scaling is exact, and
is now `float`. This is the only per-sample `double` remaining, reachable only
for `APTA_SAMPLE_S32` input, and it is commented as such.

Conversions clamp with `fminf(fabsf(sample), 1.0f)`. `apta_read_channel_sample()
` already guarantees a finite value in [-1, 1], but the float-to-integer
conversion must not depend on a caller invariant for its defined behaviour. The
branchless form is also safer than an `if`: for a NaN input the comparison form
would leave NaN in place and the subsequent integer conversion would be
undefined, whereas `fminf` returns the non-NaN operand.

Verified with AddressSanitizer and UndefinedBehaviorSanitizer over the full
suite, with no `float-cast-overflow` or other diagnostics.

## Configurable overview resolution

`apta_result_get_waveform_overview()` has always taken a `level_id` and
`apta_waveform_level_info_t` has always carried `frames_per_column`, but there
was exactly one resolution: 1024 frames per column, about 43 columns per second
at 44.1 kHz. A consumer whose zoom window spans one to thirty seconds across
800 pixels needs 27 to 800 columns per second.

`apta_session_config_t.overview_frames_per_column` now selects it. Zero keeps
the library default, so a config from `apta_session_config_init()` behaves
exactly as before. A non-zero value must be a power of two between 64 and
65536; anything else is rejected at `apta_session_create()` with
`APTA_ERROR_INVALID_ARGUMENT` rather than clamped, because the column index is
a division by this value and the result-pool layout is sized from it.

The field comes from the struct's reserved space, so `sizeof` and the ABI are
unchanged and no API version bump is needed.

This is the work order's option B rather than a level pyramid. C3 had already
made the constant overridable at build time, which left the remaining work as
moving it from compile time to the session config; a pyramid would have cost
memory linearly per level for resolutions most hosts do not need at the same
time.

Everything derived from the resolution follows it: the session's column
geometry, the bounded result-pool layout, and
`apta_query_workspace_requirements()`. The queried figure rises as the
resolution gets finer, which is the honest answer -- the accumulator array
holds one entry per column across the whole track and is the dominant workspace
term.

The value reached is reported back in
`apta_waveform_level_info_t.frames_per_column`, so a host that passes zero can
still discover what it got.

### Validation lives in three places

A new session-config field has to be validated in
`apta_session_config_is_valid()` for heap sessions,
`apta_workspace_config_is_valid()` for static-workspace sessions, and
`apta_query_memory_requirements_base()` for the query. Adding it to only one
leaves the others accepting values the rest of the library cannot honour. The
check is a shared `apta_internal_overview_resolution_is_valid()` called from
all three rather than a fourth copy.

### Not changed

The detail tile mechanism keeps its own fixed geometry, per the work order.
Whether the WOVR container header carries `frames_per_column` was not
investigated; a serialize and parse round trip at a non-default resolution is
not covered by `apta.waveform.resolution`, which checks the live result only.
That is the remaining gap for this task.
