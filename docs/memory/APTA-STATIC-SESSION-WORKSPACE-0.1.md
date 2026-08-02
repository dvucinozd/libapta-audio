# APTA static session workspace status 0.1

**Status:** Phase-four implementation candidate  
**Scope:** caller-owned session, mutable overview state and session metadata

> Historical phase snapshot: the immutable-publication gap described here was
> closed by the two-slot result-pool implementation. See
> [`APTA-BOUNDED-RESULT-SLOTS-0.1.md`](APTA-BOUNDED-RESULT-SLOTS-0.1.md) and
> [`APTA-BOUNDED-RESULT-POOL-STORAGE-0.1.md`](APTA-BOUNDED-RESULT-POOL-STORAGE-0.1.md).

## Purpose

The workspace path activates the existing `static_workspace` and `static_workspace_size` fields in `apta_session_config_t` without changing the public structure layout.

When both fields are zero, session creation follows the existing context-allocator path.

When both fields are supplied, the session object is constructed directly at the beginning of caller-provided storage. The storage must remain valid and exclusively owned by the session until `apta_session_destroy()` returns.

## Validation

The implementation requires:

- `static_workspace` and `static_workspace_size` to be supplied together;
- the workspace base address to satisfy `alignof(max_align_t)`;
- enough storage for the internal session object, arena header and one aligned payload block;
- the normal source, feature-mask, channel-layout and detail-dependency rules.

A missing pointer/size pair or misaligned pointer returns `APTA_ERROR_INVALID_ARGUMENT`. An aligned but undersized workspace returns `APTA_ERROR_OUT_OF_MEMORY`.

## Ownership and lifetime

The library does not free caller workspace memory.

`apta_session_destroy()` releases the current immutable result and context-owned runtime objects, returns session-owned arena blocks and clears the in-workspace session object. An independently acquired immutable result may still outlive the destroyed session and workspace.

## Workspace-owned objects

Phase four places the following in caller storage:

- the `apta_session_t` object;
- the sorted accepted-range array;
- queued normalized-mono PCM nodes;
- overview waveform accumulator arrays;
- the session-owned metadata byte block.

Accepted-range and accumulator growth remain transactional. A larger replacement is reserved before the old array is returned to the arena. When the workspace cannot temporarily hold the replacement, the operation returns bounded backpressure or an allocation error rather than falling back to the context allocator.

PCM nodes are allocated from the arena and returned after processing. Adjacent free blocks are coalesced, allowing the same workspace capacity to serve a long sequence of push/process cycles instead of being consumed cumulatively.

Before the overview analyzer consumes queued PCM, the process wrapper counts the distinct overview columns represented by that queue and reserves the required accumulator capacity from the workspace. The existing DSP analyzer then runs unchanged and cannot trigger context allocation for accumulator growth.

`apta_session_set_metadata()` validates and copies caller text/byte fields into the session arena while the session remains `CREATED`. The replacement is installed transactionally: result publication must succeed before the previous session metadata block is returned to the arena.

## Context-owned objects

The following remain context-owned:

- immutable result objects;
- overview/detail snapshot arrays;
- metadata copies owned by immutable results;
- parser-owned objects.

Result-owned metadata intentionally remains outside the session workspace because a result may outlive both the session and the caller-provided storage.

Therefore this phase does **not** claim `APTA-R0-STATIC-128K` or the stronger guarantee "no heap allocation after successful session creation". Initial/result publication and immutable snapshot construction still allocate through the context allocator.

## Arena and cleanup compatibility

The workspace uses a max-aligned first-fit arena with block splitting, deallocation and adjacent-block coalescing.

Each arena payload carries an internal allocation tag immediately before the returned pointer. Existing cleanup paths may continue calling the generic context deallocator: tagged session-owned blocks are recognized and routed back to their owning arena, while normal context allocations retain their existing accounting and allocator callbacks.

The tag is private implementation state and is not part of the public ABI or serialized format.

## Verification

The workspace tests verify:

- exact session placement at the caller workspace base;
- one fewer context allocator call than heap-backed session creation;
- seven contiguous 128-frame push/process cycles;
- no context allocation for accepted ranges, queued PCM or overview accumulators;
- PCM block recycling after every processing call;
- seventeen additional sparse one-frame blocks on separate 1024-frame column boundaries;
- accepted-range growth through capacities 8, 16 and 32;
- overview accumulator growth from 16 to 32 entries;
- invalid metadata rejection without allocation or generation change;
- exactly two context allocations for successful metadata publication: result object plus result-owned metadata copy;
- caller metadata buffer independence;
- immutable result metadata validity after workspace session destruction;
- pointer/size pairing;
- alignment rejection;
- undersized-workspace rejection;
- preservation of the overview/detail feature dependency.

The controlled PCM workload never completes an overview column. This prevents immutable result publication from obscuring the allocator-call evidence. Across all push/process operations, the context allocator remains at the three expected calls: context creation, initial result publication and the `CREATED` to `ACTIVE` result transition.

## Subsequent bounded phase

The gap at this snapshot was immutable result publication. It was subsequently
closed with a context-owned, preallocated two-slot result pool that preserves
result lifetime beyond session/workspace destruction and reports deterministic
slot-exhaustion backpressure.

## Querying the required size

`apta_query_workspace_requirements()` reports how large
`apta_session_config_t.static_workspace` must be for a given configuration:

```c
apta_session_config_t config;
apta_memory_requirements_t required;

apta_session_config_init(&config);
config.source_sample_rate = 44100u;
config.channel_count = 2u;
config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
config.channel_layout = APTA_CHANNEL_LAYOUT_STEREO;
config.total_frames = total_frames;          /* required: the figure scales */
config.requested_features = features;

apta_memory_requirements_init(&required);    /* output structs too */
status = apta_query_workspace_requirements(&config, &required);
```

`minimum_bytes` is what `apta_session_create()` enforces; a buffer at least that
large, aligned to `required_alignment`, completes the analysis.
`recommended_bytes` adds headroom. `static_workspace` and
`static_workspace_size` are ignored by the query, so it can be called before a
buffer exists.

`config.total_frames` must be set. The overview accumulator array holds one
entry per column across the track and is the largest contributor for anything
longer than a few seconds, so a query with `total_frames == 0` reports only the
fixed part and will understate a real session.

### What changed

`apta_session_create()` previously validated the workspace against
`apta_internal_session_workspace_minimum_size()`, which is
`align(sizeof(apta_session_t))` plus one block header and payload prefix and is
independent of both `total_frames` and `requested_features`. A 12 KiB buffer was
accepted for a multi-minute full-feature session and the insufficiency surfaced
much later as an `APTA_ERROR_OUT_OF_MEMORY` from `apta_session_process()`.

Creation now also enforces the computed figure, so an undersized buffer is a
diagnosable configuration error at the point where the host can still act on it.
The old constant remains as a cheap first check.

Hosts that previously supplied a workspace smaller than their configuration
needs, and happened not to hit the failure, will now be rejected at creation.
That is the intended behaviour: those sessions could not have completed.

## Overridable capacities

The geometry and capacity constants in `src/core/apta_internal.h` are
`#ifndef`-guarded, so a host can override them from its build system without
patching the header:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_FLAGS=-DAPTA_INTERNAL_OVERVIEW_FRAMES_PER_COLUMN=256u
```

The ESP-IDF component exposes the four most useful ones through `Kconfig`; see
`ports/espidf/README.md`.

Derived constants stay derived and are not overridable:
`APTA_INTERNAL_DETAIL_TILE_FRAMES`, `APTA_INTERNAL_GLOBAL_MAX_SEGMENTS` and
`APTA_INTERNAL_GLOBAL_MAX_BEATS`.

Every invariant the code depends on is a `_Static_assert` in the same header,
so an incoherent combination fails to compile instead of corrupting a ring or
bounded workspace array.

Any override changes what `apta_query_workspace_requirements()` reports,
because that function is computed from the same constants. Call it rather than
scaling a published figure.

### Ring capacities and division cost

The onset and global bin stores are rings addressed with
`bin_index % capacity`, not a mask, so a capacity that is not a power of two is
correct. It costs an integer division per lookup; a power of two lets the
compiler strength-reduce it. This is worth respecting: the same division in an
inner loop was measured to dominate the tempo autocorrelation.

### Verifying a non-default build

```bash
cmake -S . -B build-ov256 -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_FLAGS=-DAPTA_INTERNAL_OVERVIEW_FRAMES_PER_COLUMN=256u \
      -DCMAKE_CXX_FLAGS=-DAPTA_INTERNAL_OVERVIEW_FRAMES_PER_COLUMN=256u
cmake --build build-ov256 --parallel
ctest --test-dir build-ov256 -LE default_geometry --output-on-failure
```

`-LE default_geometry` excludes tests pinned to the documented default
geometry: those asserting exact serialized container sizes, exact fixture byte
counts, or allocator call counts. They are interchange and layout conformance
evidence, and deriving their expectations from the configured geometry would
make them assert that the writer wrote what the writer computed. Everything
outside that label is expected to pass at any supported override.
