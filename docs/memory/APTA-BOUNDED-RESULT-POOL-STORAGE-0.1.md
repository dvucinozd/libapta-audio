# APTA bounded result pool storage status 0.1

**Status:** Internal pooled-result lifetime implementation candidate  
**Public activation:** intentionally disabled

## Implemented scope

The bounded-result layout is materialized as one context-owned allocation.

Pool creation:

- calls the same checked layout calculator used by `apta_query_memory_requirements()`;
- reserves exactly `layout.total_bytes` through the context allocator;
- initializes one atomic owner reference;
- stores the complete calculated layout in the control block;
- initializes two atomic slot-state descriptors;
- assigns each descriptor one distinct max-aligned fixed-size storage region.

The pool can now construct an immutable empty `apta_result_t` inside either slot. The result contains valid source geometry, generation, session state, changed-feature and lineage fields. It intentionally contains no metadata or waveform snapshot data in this phase.

## Slot reservation and exhaustion

Slot reservation uses an atomic compare-and-exchange from inactive to active.

When both slots are active, a third construction attempt returns:

```text
APTA_ERROR_RESULT_SLOTS_EXHAUSTED
```

The output pointer remains `NULL`. Releasing one result returns its slot to the pool, and the next construction reuses that fixed address after zero-filling the complete slot storage.

## Result lifetime

Pooled results use the normal public `apta_result_release()` entry point and existing atomic result reference count.

Each active result holds one pool reference. The pool owner reference can be released before active results. The complete allocation remains alive until the final pooled result reaches a zero public reference count.

The result release path distinguishes pooled and ordinary results through private internal state:

- ordinary results retain metadata/waveform cleanup plus individual context deallocation;
- pooled empty results decrement context result accounting, atomically free the slot and release the slot's pool reference;
- pooled results are never individually deallocated.

While the owner or any pooled result exists, `apta_context_destroy()` returns `APTA_ERROR_BUSY`.

## Failure behaviour

Pool creation remains transactional:

- invalid layout/configuration returns the layout error;
- configured context memory limits are enforced before allocator invocation;
- allocator failure returns `APTA_ERROR_OUT_OF_MEMORY`;
- `pool_out` remains `NULL` on every failure;
- failed allocation reservation is removed from context accounting.

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

## Deliberate boundary

`APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS` still returns `APTA_ERROR_UNSUPPORTED` from public session creation.

This phase does not:

- attach a pool to `apta_session_t`;
- make a pooled result current on a session;
- populate result metadata from fixed slot storage;
- populate overview/detail snapshots from fixed slot storage;
- change publication retry semantics.

The next phase will attach the pool to a workspace session and replace its initial empty heap result with one pooled generation without yet enabling subsequent waveform publication.
