# APTA bounded immutable result slots 0.1

**Status:** Implemented and CI-verified implementation candidate  
**Purpose:** provide post-create bounded publication without weakening immutable result lifetime  
**Verified implementation merge:** `b1c9100b2acee13188c650e71e6364bacbae7e7c`  
**Primary verification:** GitHub Actions PR CI run `#188`

## Problem statement

Caller-owned session workspace storage is appropriate for mutable state, but not for immutable results. An acquired `apta_result_t` may remain valid after `apta_session_destroy()` returns, at which point the caller may immediately reuse the session workspace.

Version 0.1 therefore uses:

- caller-owned storage for the session object, queued PCM, accepted ranges, overview accumulators and session metadata;
- one context-owned, preallocated result pool for immutable generations that may outlive the session.

## Activation

The mode is opt-in through:

```text
APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS
```

The public API also defines:

```text
APTA_ERROR_RESULT_SLOTS_EXHAUSTED
APTA_MEMORY_REQUIREMENTS_INCLUDE_RESULT_POOL
```

A valid bounded configuration requires:

- a caller-provided static session workspace;
- known `total_frames`;
- waveform overview support;
- only features whose maximum snapshot geometry is derivable at creation time;
- successful preallocation of the complete pool before session creation returns.

Unknown-duration input is rejected because overview span and column capacity would otherwise have no finite creation-time upper bound.

Sessions without the flag retain the original heap-backed result path.

## Fixed slot count

Version 0.1 uses exactly two result slots.

Two slots implement double-buffered publication:

1. one slot contains the current immutable generation;
2. the other slot is constructed as the replacement;
3. publication atomically swaps the current pointer;
4. the session releases its old result reference;
5. the old slot becomes reusable only after all application references are released.

The fixed count keeps the ABI and memory query deterministic. A configurable slot count is not part of version 0.1.

## Capacity derivation

The query and create paths share one checked layout calculator.

Each slot reserves:

- one `apta_result_t`;
- `ceil(total_frames / 1024)` overview columns;
- the same count of overview spans, covering the worst case of one sparse span per logical column;
- four `apta_waveform_tile_view_t` detail descriptors;
- 256 packed detail columns;
- `APTA_METADATA_MAX_TOTAL_BYTES` metadata storage;
- all alignment padding required by the contained types.

The layout rejects arithmetic overflow, unsupported bounded geometry and a nonzero session memory budget smaller than the complete pool.

Although each slot reserves 8,192 metadata bytes, the current public per-field limits allow at most 5,884 input bytes across all metadata fields. That complete public maximum is covered by runtime tests.

## Pool ownership

The pool is one context-owned allocation created before the bounded session is committed.

It contains:

- an atomic pool reference count;
- one session-owner reference;
- two atomic slot descriptors;
- the checked fixed layout;
- all result, metadata, overview and detail storage.

Each active result holds one pool reference. Session destruction releases the current result and the session-owner reference. If an application still retains any result, the pool and context remain alive. The last pooled result release frees the pool.

This preserves the rule that `apta_context_destroy()` returns `APTA_ERROR_BUSY` while acquired results remain.

## Transactional publication

Bounded publication performs these steps:

1. reserve a free slot with atomic compare-and-exchange;
2. zero the complete slot storage;
3. initialize result identity, source geometry, lineage, generation and session state;
4. copy session metadata into fixed slot storage;
5. build sparse WOVR spans and columns in fixed regions;
6. build the bounded WDTL tile cache and packed columns in fixed regions;
7. validate every count and pointer range;
8. atomically replace `session->current_result`;
9. release the previous session-owned result reference.

If any construction step fails, the new result is never published and the reserved slot is returned to the pool.

The dispatcher is internal and mode-specific:

- bounded sessions use fixed-slot publication;
- ordinary sessions use the unchanged allocation-backed publication path.

## Exhaustion and retry

When both slots are active, no replacement can be safely constructed. The operation returns:

```text
APTA_ERROR_RESULT_SLOTS_EXHAUSTED
```

The error is transient and distinct from out-of-memory failure.

Retry behaviour is feature-aware:

- metadata replacement preserves the previous metadata, generation and current result;
- session-state transitions roll back if their replacement generation cannot be published;
- overview samples remain accumulated, while completion markers are reset so a later process call reconstructs completion and retries publication;
- detail mutation serials remain pending until a later successful publication.

No PCM data, request state or published result is silently discarded. Releasing an older retained result allows the same operation to progress.

## Result release

A pooled result is not individually deallocated.

When its public reference count reaches zero:

- the slot is atomically marked free;
- the slot releases its pool reference;
- the complete pool is deallocated only after the session-owner reference and every result reference are gone.

Non-pooled results retain their existing cleanup and deallocation behaviour.

## No-allocation guarantee

After successful bounded session creation, these operations have verified zero context-allocator callbacks:

- metadata replacement within public limits;
- PCM push and backpressure;
- cooperative processing;
- partial and final overview publication;
- detail publication up to the full four-tile capacity;
- result acquire and release;
- serialized-size query and serialization into caller storage;
- session destruction.

Successful creation itself makes exactly one pool allocation in addition to the context object. Container parsing is a separate context-owned operation and is not covered by this guarantee.

## Memory requirement reporting

When the bounded flag is present, `apta_query_memory_requirements()` reports the complete two-slot pool and sets:

```text
APTA_MEMORY_REQUIREMENTS_INCLUDE_RESULT_POOL
```

The minimum and recommended byte counts are equal for the current fixed layout. Query and create use the same calculator, preventing reported and allocated sizes from diverging.

## Verification matrix

The following contract gates are implemented and tested:

| Gate | Evidence |
|---|---|
| Exact query/create pool-size agreement | `apta.memory.result_pool_layout`, `apta.memory.result_pool_storage` |
| Pool allocation and memory-limit failure | `apta.memory.result_pool_storage`, `apta.session.bounded_initial_result` |
| Zero allocator calls after successful create | bounded initial, waveform, capacity and concurrency tests |
| Partial and final WOVR | `apta.result.bounded_waveform_publication` |
| Maximum fixed WDTL cache | `apta.memory.bounded_pool_capacity` |
| Maximum public metadata input | `apta.memory.bounded_pool_capacity` |
| Retained old plus current generation | bounded waveform and concurrency tests |
| Deterministic third-publication exhaustion | pooled empty-result and bounded waveform tests |
| Retry after retained-result release | bounded waveform and concurrency tests |
| Result validity after session/workspace destruction | initial, waveform, capacity and concurrency tests |
| Context busy until final result release | all bounded lifetime tests |
| Concurrent acquire/release during pooled publication | `apta.result.bounded_concurrency` |
| Serialization after workspace destruction | `apta.result.bounded_waveform_publication` |
| 32-bit execution | repository CI matrix |
| ASan/UBSan | CI run `#188` |
| Ordinary result-path regressions | complete 43-test suite |

The registered runtime suite contains 43 tests. GitHub Actions PR CI run `#188` completed successfully for merge `b1c9100b2acee13188c650e71e6364bacbae7e7c`.

## Conformance position

The bounded result-slot implementation is complete for its version 0.1 scope. It does not itself establish an `APTA-R0-STATIC-128K` or other resource-class claim.

A resource-class claim still requires:

- a declared workload and source limit;
- measured total context, pool, workspace and stack consumption;
- measured process-call latency;
- proof that the complete configuration fits the class ceiling;
- independent target integration evidence.

Tempo, beatgrid, unknown-duration bounded sessions and other variable-size feature snapshots remain outside the implemented scope.
