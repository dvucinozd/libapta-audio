# APTA bounded immutable result slots 0.1

**Status:** Proposed implementation contract  
**Purpose:** close the remaining post-create allocation gap without weakening immutable result lifetime

## Problem statement

The static session workspace now owns mutable session state: the session object, queued PCM, accepted ranges, overview accumulators and session metadata.

Immutable results cannot simply move into that workspace. The public lifetime contract allows an acquired `apta_result_t` to remain valid after `apta_session_destroy()` returns. The caller is then free to reuse or release the session workspace, so any result object, snapshot array or metadata pointer stored inside it would become invalid.

The remaining bounded-memory solution therefore requires a context-owned result pool allocated before successful session creation returns.

## Activation

The first implementation will add an explicit session flag:

```text
APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS
```

The mode is opt-in. Existing heap-backed and current static-workspace behaviour remains unchanged when the flag is absent.

Version 0.1 bounded-result mode requires:

- a caller-provided static session workspace;
- known `total_frames`;
- waveform-overview support;
- only section/features whose maximum snapshot capacities are derivable at creation time;
- successful preallocation of the complete pool before the session is returned.

Unknown-duration input is rejected for the first bounded-result implementation because the worst-case overview column and span counts are not finite without an additional declared source limit.

## Fixed slot count

Version 0.1 uses two result slots.

Two slots permit normal double-buffered publication:

1. one slot contains the current immutable generation;
2. the other slot is constructed as the next generation;
3. after atomic publication, the session releases its reference to the old generation;
4. the old slot becomes reusable when no application references remain.

The slot count is deliberately fixed in 0.1 so ABI and memory-accounting work can be completed before exposing a configurable count.

## Capacity derivation

At session creation, the pool layout is calculated with checked arithmetic.

Each slot reserves capacity for:

- one `apta_result_t`;
- worst-case overview columns:
  `ceil(total_frames / overview_frames_per_column)`;
- worst-case overview spans equal to the overview column count;
- all fixed detail tile descriptors;
- all fixed detail packed columns;
- up to `APTA_METADATA_MAX_TOTAL_BYTES` of result-owned metadata;
- required alignment padding.

A sparse source can produce one span per logical column, so using the logical column count as span capacity is required for a strict upper bound.

Tempo, beatgrid and future variable-size feature snapshots are unsupported in bounded-result mode until their creation-time capacity rules are normative.

## Pool ownership

The result pool is one context-owned allocation created during `apta_session_create()`.

The pool carries:

- an atomic pool reference count;
- a session-owner reference;
- two slot descriptors;
- slot state and generation identity;
- fixed pointer/capacity assignments for every snapshot region.

An active result slot holds one pool reference. The session holds a separate owner reference until destruction.

`apta_session_destroy()` releases the current result and then the session-owner reference. If an application still retains a result, the pool and context remain alive. The final retained result release frees the pool only after all slot references and the session-owner reference are gone.

This preserves the existing rule that context destruction returns busy while acquired results remain.

## Slot publication

Publication is transactional:

1. select a free non-current slot;
2. clear its previous logical contents without releasing the pool allocation;
3. construct metadata and waveform snapshots within fixed slot capacities;
4. validate every resulting count and pointer range;
5. initialize the result reference count to one session-owned reference;
6. atomically replace `session->current_result`;
7. release the previous current result.

A partially constructed slot is never visible through `apta_session_acquire_result()`.

Publication must not mutate the previous current generation or session publication bookkeeping until the replacement is complete.

## Exhaustion and retry

When both slots are retained — one as current and one by an application — no replacement slot is available.

The implementation will return a dedicated transient error:

```text
APTA_ERROR_RESULT_SLOTS_EXHAUSTED
```

No PCM, request-state or published-generation data may be lost. The host can release an older result and retry the same process or metadata operation.

This condition is distinct from allocator failure: the complete pool already exists, but application retention prevents safe slot reuse.

Existing publication-retry logic must treat slot exhaustion as retryable in the same places where transient snapshot allocation failure is currently retried.

## Result release

A pooled result is not individually deallocated.

When its public reference count reaches zero:

- owned logical pointers/counts are reset;
- the slot becomes free;
- the slot releases its pool reference;
- the pool is freed only if the session-owner reference is also gone and no other slot is active.

Non-pooled results retain the current allocation and cleanup path.

## No-allocation guarantee

After successful creation of a bounded-result workspace session, the following operations must not call the context allocator:

- metadata replacement within fixed metadata limits;
- PCM push and backpressure;
- cooperative processing;
- overview/detail publication within fixed capacities;
- result acquire/release;
- serialization size query and serialization into caller storage;
- session destruction.

Container parsing remains a separate context-owned operation and is not covered by the session guarantee.

## Memory requirement reporting

`apta_query_memory_requirements()` must include the complete two-slot pool when the bounded-result flag is present.

The query and creation layout calculators must share one implementation so the reported required bytes cannot diverge from allocation behaviour.

The report must include checked alignment and must reject configurations whose pool size cannot be represented by `size_t` or exceeds a nonzero configured memory budget.

## Required tests

Activation cannot merge until tests cover:

- exact query/create pool-size agreement;
- Nth-allocation failure during pool creation;
- zero allocator calls after successful creation;
- final and partial WOVR publication;
- WDTL publication at maximum fixed tile capacity;
- maximum metadata copy;
- retained old generation plus current generation;
- deterministic slot-exhaustion error on third publication;
- successful retry after releasing the retained generation;
- acquired pooled result validity after session/workspace destruction;
- context busy state until the last pooled result is released;
- concurrent result acquire/release during publication;
- 32-bit build and execution;
- ASan/UBSan execution;
- unchanged heap-session result lifetime and allocation-failure behaviour.

## Conformance position

This design is necessary for a future `APTA-R0-STATIC-128K` claim, but it does not itself establish that claim.

A resource-class claim additionally requires a declared workload, measured total context/workspace/stack use, fixed source limits and proof that the complete pool plus session workspace fits the class ceiling.
