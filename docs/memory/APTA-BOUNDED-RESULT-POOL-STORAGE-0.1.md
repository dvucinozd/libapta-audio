# APTA bounded result pool storage status 0.1

**Status:** Implemented and CI-verified implementation candidate  
**Public activation:** enabled through `APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS`  
**Verified implementation merge:** `b1c9100b2acee13188c650e71e6364bacbae7e7c`  
**Primary verification:** GitHub Actions PR CI run `#188`  
**Registered runtime tests:** 43  
**Resource-class claim:** none

## Implemented scope

A bounded-result session combines two deliberately different lifetime domains:

- mutable session state is stored in the caller-owned static workspace;
- immutable generations are stored in one context-owned result-pool allocation so acquired results can outlive the session and workspace.

Pool creation uses the same checked layout calculator as `apta_query_memory_requirements()`. One allocation contains:

- an atomic pool owner reference;
- two atomic slot descriptors;
- two distinct max-aligned result regions;
- one `apta_result_t` location per slot;
- worst-case sparse overview span and column capacity derived from known `total_frames`;
- four detail tile descriptors and 256 packed detail columns per slot;
- `APTA_METADATA_MAX_TOTAL_BYTES` of fixed metadata storage per slot;
- all required alignment padding.

The successful custom-allocator create path performs exactly two callback allocations: the context object and the complete result pool. It does not allocate a temporary heap result.

## Activation requirements

Version 0.1 bounded-result mode requires:

- a caller-provided, max-aligned static session workspace;
- known `total_frames`;
- waveform overview in the requested feature set;
- only features with creation-time bounded snapshot geometry;
- sufficient context and session memory budgets for the exact calculated pool.

Unknown-duration input is rejected because the worst-case overview span and column counts are not finite without another declared source limit.

The flag is opt-in. Sessions without it retain the existing heap-backed publication path.

## Fixed-slot publication

Every replacement generation is constructed transactionally in a free non-current slot.

The bounded snapshot builder can populate, in one immutable result:

- owned metadata, including explicitly present empty values;
- sparse `WOVR` spans and quantized min/max/RMS columns;
- the bounded four-tile `WDTL` cache and packed detail columns;
- source geometry, lineage, generation, session state, changed-feature and available-feature fields.

Only after the complete slot validates successfully does publication atomically replace `session->current_result`. The previous session-owned result reference is then released. A partially constructed slot is never visible to readers.

Ordinary sessions continue through the original allocation and cleanup path.

## No-allocation operation

After successful bounded session creation, the following tested operations do not call the session context allocator:

- metadata replacement within public limits;
- PCM push and workspace backpressure;
- cooperative processing;
- overview and detail publication within calculated capacities;
- result acquire and release;
- serialized-size query and serialization into caller storage;
- session destruction.

Container parsing remains a separate context-owned operation and is outside this guarantee.

## Slot exhaustion and retry

When the current generation and the other slot are both retained, a replacement cannot be constructed. The operation returns:

```text
APTA_ERROR_RESULT_SLOTS_EXHAUSTED
```

This is a transient ownership condition, not allocator failure.

The implementation preserves retry state:

- metadata replacement leaves the previous metadata, generation and current result unchanged;
- lifecycle transitions roll back when their result publication cannot reserve a slot;
- overview publication preserves accumulated samples and resets completion markers so a later `process()` reconstructs completion and republishes without rereading PCM;
- detail publication retains the existing mutation-serial retry state.

After an older application result is released, the same operation can succeed and reuse the freed fixed address.

## Result and workspace lifetime

Each active pooled result holds one pool reference. The session holds a separate pool-owner reference until destruction.

`apta_session_destroy()`:

- releases the session-owned current-result reference;
- cleans mutable workspace state;
- decrements context session accounting;
- releases the pool-owner reference;
- clears the caller workspace.

An application-acquired pooled result remains valid after session destruction and after the caller overwrites or reuses the complete session workspace. The context remains busy until the final result releases the last pool reference.

Pooled results are never individually deallocated. Their final public release marks the slot free and releases its pool reference. Non-pooled results retain ordinary metadata/waveform cleanup and individual context deallocation.

## Verified boundaries

The verified suite includes:

- exact query/create pool-size agreement;
- pool allocation failure and context memory-limit rollback;
- atomic two-slot reservation, exhaustion and fixed-address reuse;
- initial public bounded generation and post-session lifetime;
- zero-allocation metadata, state-transition, WOVR and WDTL publication;
- partial and final overview/detail states;
- retry after retained-result exhaustion;
- canonical META/WOVR/WDTL serialization and independent parse after workspace destruction;
- maximum public metadata fields, totaling 5,884 owned bytes;
- all four full detail tiles and all 256 packed detail columns;
- four concurrent readers during 200 pooled metadata publications;
- unchanged ordinary-session tests;
- GCC/Clang builds, ASan/UBSan runtime execution and bounded fuzz smoke.

The current registered suite contains 43 runtime tests. CI run `#188` completed successfully for source merge `b1c9100b2acee13188c650e71e6364bacbae7e7c`.

## Deliberate limitations

This implementation does not establish an `APTA-R0-STATIC-128K` or other resource-class claim. Such a claim still requires a declared workload and measured total context, pool, workspace, stack and process-call latency evidence.

Version 0.1 also retains these deliberate limits:

- exactly two immutable result slots;
- known-duration bounded sessions only;
- fixed four-tile detail cache;
- no tempo, beatgrid or other variable-size feature snapshots;
- no claim that container parsing is allocation-free;
- no certified conformance or stable ABI claim.
