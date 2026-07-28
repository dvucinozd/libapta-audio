# APTA static session workspace status 0.1

**Status:** Phase-two implementation candidate  
**Scope:** caller-owned session, accepted-range storage and recyclable queued PCM

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

Phase two places the following in caller storage:

- the `apta_session_t` object;
- the sorted accepted-range array;
- queued normalized-mono PCM nodes.

Accepted-range growth remains transactional. A larger replacement is reserved before the old array is returned to the arena. If the workspace cannot temporarily hold the replacement, PCM acceptance reports bounded backpressure rather than falling back to the context allocator.

PCM nodes are allocated from the arena and returned after processing. Adjacent free blocks are coalesced, allowing the same workspace capacity to serve a long sequence of push/process cycles instead of being consumed cumulatively.

## Context-owned objects

The following remain context-owned:

- immutable result objects;
- overview/detail snapshot arrays;
- session and result metadata copies;
- overview accumulator arrays;
- parser-owned objects.

Therefore this phase does **not** claim `APTA-R0-STATIC-128K` or the stronger guarantee "no heap allocation after successful session creation".

## Arena and cleanup compatibility

The workspace uses a max-aligned first-fit arena with block splitting, deallocation and adjacent-block coalescing.

Each arena payload carries an internal allocation tag immediately before the returned pointer. Existing cleanup paths may continue calling the generic context deallocator: tagged session-owned blocks are recognized and routed back to their owning arena, while normal context allocations retain their existing accounting and allocator callbacks.

The tag is private implementation state and is not part of the public ABI or serialized format.

## Verification

The workspace test verifies:

- exact session placement at the caller workspace base;
- one fewer context allocator call than heap-backed session creation;
- seven contiguous 128-frame push/process cycles;
- no context allocation for accepted ranges or queued PCM;
- PCM block recycling after every processing call;
- immutable result validity after workspace session destruction;
- pointer/size pairing;
- alignment rejection;
- undersized-workspace rejection;
- preservation of the overview/detail feature dependency.

The controlled workload stops at 896 analysed frames, before the first 1024-frame overview column completes. This prevents result publication from obscuring the allocator-call evidence.

## Next bounded phase

The next workspace phase will migrate overview accumulator growth to the arena. Immutable results will remain context-owned so they can continue to outlive the session.
