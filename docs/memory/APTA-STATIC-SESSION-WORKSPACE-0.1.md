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
