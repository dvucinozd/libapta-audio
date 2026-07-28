# APTA static session workspace status 0.1

**Status:** Phase-one implementation candidate  
**Scope:** caller-owned storage for `apta_session_t` and an internal coalescing arena

## Purpose

The phase-one workspace path activates the existing `static_workspace` and `static_workspace_size` fields in `apta_session_config_t` without changing the public structure layout.

When both fields are zero, session creation follows the existing context-allocator path.

When both fields are supplied, the session object is constructed directly at the beginning of the caller-provided storage. The storage must remain valid and exclusively owned by the session until `apta_session_destroy()` returns.

## Validation

The implementation requires:

- `static_workspace` and `static_workspace_size` to be supplied together;
- the workspace base address to satisfy `alignof(max_align_t)`;
- enough storage for the internal session object, arena header and one aligned payload block;
- the normal source, feature-mask, channel-layout and detail-dependency rules.

A missing pointer/size pair or misaligned pointer returns `APTA_ERROR_INVALID_ARGUMENT`. An aligned but undersized workspace returns `APTA_ERROR_OUT_OF_MEMORY`.

## Ownership and lifetime

The library does not free caller workspace memory.

`apta_session_destroy()` releases the current immutable result and all context-owned runtime objects, decrements the context session count and clears the in-workspace session object. An independently acquired immutable result may still outlive the destroyed session and workspace.

## Allocation boundary

This phase intentionally moves only the session object itself into caller storage.

The following remain context-owned:

- immutable result objects;
- overview/detail snapshot arrays;
- session metadata copies;
- queued PCM nodes;
- accepted-range arrays;
- overview accumulator arrays.

Therefore this phase does **not** claim `APTA-R0-STATIC-128K` or the stronger guarantee "no heap allocation after successful session creation".

## Arena foundation

The unused portion of the workspace is initialized as a max-aligned first-fit arena with block splitting, deallocation and adjacent-block coalescing. The arena is internal and is not yet used by waveform runtime allocations in this phase.

Keeping arena activation separate from runtime migration allows each ownership class to receive dedicated backpressure, growth, retry and lifetime tests before it stops using the context allocator.

## Verification

The phase-one test verifies:

- exact session placement at the caller workspace base;
- one fewer context allocator call than heap-backed session creation;
- immutable result validity after workspace session destruction;
- pointer/size pairing;
- alignment rejection;
- undersized-workspace rejection;
- preservation of the overview/detail feature dependency.

## Next bounded phase

The next workspace phase will migrate recyclable queued PCM nodes and fixed-growth session analysis state to the arena. Immutable results will remain context-owned so they can continue to outlive the session.
