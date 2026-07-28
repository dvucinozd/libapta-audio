# APTA bounded result pool storage status 0.1

**Status:** Internal storage implementation candidate  
**Public activation:** intentionally disabled

## Implemented scope

The bounded-result layout is now materialized as one context-owned allocation.

Pool creation:

- calls the same checked layout calculator used by `apta_query_memory_requirements()`;
- reserves exactly `layout.total_bytes` through the context allocator;
- initializes one atomic owner reference;
- stores the complete calculated layout in the control block;
- initializes two inactive slot descriptors;
- assigns each descriptor one distinct max-aligned fixed-size storage region.

The allocation contains no active `apta_result_t` generation yet. Slot storage is zero-filled and remains private until the publication phase assigns result and snapshot views.

## Lifetime

The pool uses an atomic reference count independent of session and result reference counts.

The initial reference represents future session ownership. Internal retain/release operations allow later result slots to keep the pool alive after session destruction.

When the final pool reference is released, the complete block is returned through `apta_internal_context_deallocate()`. While the pool exists, `apta_context_destroy()` returns `APTA_ERROR_BUSY` because the allocation remains in context accounting.

## Failure behaviour

Pool creation is transactional:

- invalid layout/configuration returns the layout error;
- configured context memory limits are enforced before allocator invocation;
- allocator failure returns `APTA_ERROR_OUT_OF_MEMORY`;
- `pool_out` remains `NULL` on every failure;
- failed allocation reservation is removed from context accounting.

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

## Deliberate boundary

`APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS` still returns `APTA_ERROR_UNSUPPORTED` from public session creation.

This phase does not:

- attach a pool to `apta_session_t`;
- construct a pooled result;
- reserve or release an individual slot;
- replace snapshot allocation;
- change result acquire/release;
- change publication retry semantics.

The next phase will construct and release an initial empty pooled result in one slot while preserving ordinary result behaviour for all existing sessions.
