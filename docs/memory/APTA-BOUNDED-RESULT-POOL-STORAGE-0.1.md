# APTA bounded result pool storage status 0.1

**Status:** Initial public bounded-session implementation candidate  
**Publication scope:** initial empty generation only

## Implemented scope

The bounded-result layout is materialized as one context-owned allocation.

Pool creation:

- calls the same checked layout calculator used by `apta_query_memory_requirements()`;
- reserves exactly `layout.total_bytes` through the context allocator;
- initializes one atomic owner reference;
- stores the complete calculated layout in the control block;
- initializes two atomic slot-state descriptors;
- assigns each descriptor one distinct max-aligned fixed-size storage region.

The pool can construct an immutable empty `apta_result_t` inside either slot. The result contains valid source geometry, generation, session state, changed-feature and lineage fields. It intentionally contains no metadata or waveform snapshot data in this phase.

## Public bounded-session creation

`APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS` is now accepted when:

- a caller-owned static workspace is supplied;
- source duration is known;
- overview waveform is requested;
- the context advertises all requested features;
- the exact result-pool allocation fits configured context and session budgets.

Creation executes transactionally:

1. validate and prepare the caller workspace session without publishing a heap result;
2. allocate the exact context-owned result pool;
3. reserve one slot and construct generation 1 in `APTA_SESSION_CREATED` state;
4. attach the pool owner and current result to the session;
5. increment the context session count only after every prior step succeeds.

The successful create path performs no temporary result allocation. With a custom allocator, the only callback allocations are the context object and the complete result pool.

## Temporary mutation guard

The initial result is read-only infrastructure. Until fixed-slot metadata and waveform snapshot builders are implemented, operations that would require a replacement generation return `APTA_ERROR_UNSUPPORTED`:

- `apta_session_set_metadata()`;
- `apta_session_set_source()`;
- `apta_session_push_pcm()`;
- `apta_session_signal_end_of_input()`;
- `apta_session_process()`.

The initial result remains available through the normal acquire/info/generation/release API. Session state remains `APTA_SESSION_CREATED`.

## Slot reservation and exhaustion

Slot reservation uses an atomic compare-and-exchange from inactive to active.

When both slots are active, a third construction attempt returns:

```text
APTA_ERROR_RESULT_SLOTS_EXHAUSTED
```

The output pointer remains `NULL`. Releasing one result returns its slot to the pool, and the next construction reuses that fixed address after zero-filling the complete slot storage.

## Result and workspace lifetime

Pooled results use the normal public `apta_result_release()` entry point and existing atomic result reference count.

Each active result holds one pool reference. The session holds the pool owner reference until destruction. The complete allocation remains alive until the final pooled result reaches a zero public reference count.

`apta_session_destroy()`:

- removes the session-owned current-result reference;
- cleans mutable workspace state;
- decrements context session accounting;
- releases the pool owner reference;
- clears the caller workspace.

An application-acquired initial result remains valid after session destruction and after the caller reuses or overwrites the workspace. While the retained result exists, `apta_context_destroy()` returns `APTA_ERROR_BUSY`.

The result release path distinguishes pooled and ordinary results through private internal state:

- ordinary results retain metadata/waveform cleanup plus individual context deallocation;
- pooled empty results decrement context result accounting, atomically free the slot and release the slot's pool reference;
- pooled results are never individually deallocated.

## Failure behaviour

Public bounded create and internal pool creation remain transactional:

- invalid layout/configuration returns the layout error;
- missing caller workspace returns `APTA_ERROR_INVALID_ARGUMENT`;
- configured context memory limits are enforced before allocator invocation;
- allocator failure returns `APTA_ERROR_OUT_OF_MEMORY`;
- `session_out` and `pool_out` remain `NULL` on every failure;
- prepared workspace state is abandoned and cleared after pool failure;
- failed allocation reservation is removed from context accounting;
- context session count is not incremented before the initial result is ready.

Empty-result construction performs no allocator call after pool creation. Its only transient failure is deterministic slot exhaustion.

## Verification

`apta.memory.result_pool_storage` verifies:

- exact agreement between queried bytes and pool allocation size;
- two distinct max-aligned slot regions;
- expected overview, detail and metadata capacities;
- invalid slot-index rejection;
- context busy state while the pool exists;
- retain/release lifetime;
- final pool deallocation;
- failure of the pool allocator call;
- context memory-limit rejection before allocator invocation;
- clean context destruction after every failure path.

`apta.result.pool_empty_result` verifies:

- public result info and generation accessors on a pooled object;
- ordinary result retain/release semantics on the slot object;
- simultaneous use of both slots;
- deterministic exhaustion on a third result;
- slot reuse at the same fixed address;
- pool-owner release before result release;
- context busy state until the final pooled result is released.

`apta.session.bounded_initial_result` verifies:

- exact two-allocation create behaviour: context object plus pool;
- generation 1 and `CREATED` result semantics;
- mutation guards with no additional allocation;
- allocator and context-memory-limit failure rollback;
- missing-workspace, unknown-duration and missing-overview rejection;
- retained result validity after session/workspace destruction;
- context busy state until the retained result is released.

## Deliberate boundary

This phase does not:

- populate result metadata from fixed slot storage;
- populate overview/detail snapshots from fixed slot storage;
- enable a second public session generation;
- change publication retry semantics to handle slot exhaustion.

The next phase will populate fixed-slot metadata and overview snapshots and route bounded publication through the second slot transactionally.
