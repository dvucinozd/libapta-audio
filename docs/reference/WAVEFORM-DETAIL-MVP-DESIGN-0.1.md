# APTA bounded waveform-detail MVP design 0.1

**Status:** Feature-branch implementation contract  
**Branch:** `feature/waveform-detail-mvp`  
**Main-build status:** Disabled until one complete PR CI run passes

## Objective

Provide focus-local higher-resolution waveform data without changing PCM ownership, blocking cooperative processing or allowing unbounded memory growth.

## Fixed geometry

The initial implementation uses one detail level:

- `level_id = 1`;
- `frames_per_column = 256`;
- `columns_per_tile = 64`;
- `frames_per_tile = 16384`;
- maximum resident tiles per session = 4.

Tile identity is the stable tuple `(level_id, tile_index)`. Geometry cannot change within one result lineage.

## Feature dependency

The MVP requires both:

```text
APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_WAVEFORM_DETAIL
```

Detail-only sessions are rejected until feature-specific PCM replay is implemented. One accepted PCM block feeds both overview and detail aggregation from the same copied normalized mono samples.

## PCM acceptance contract

Overview acceptance remains authoritative:

1. input validation and representable detail geometry are checked before acceptance;
2. the overview layer accepts and copies a bounded PCM prefix;
3. the accepted copied mono samples are offered to the detail cache;
4. detail cache pressure or degradation cannot retroactively change the accepted frame count or return an error for already accepted PCM.

A later detail replay model may request PCM already consumed by overview, but this MVP does not do so.

## Cache and eviction policy

The cache is embedded in the session object and performs no per-tile heap allocation.

Eviction priority:

1. use an empty slot;
2. otherwise evict the least-recently-used unprotected tile;
3. a tile intersecting active focus or an unsatisfied detail region request is protected;
4. an unprotected incoming tile must be skipped when every resident tile is protected;
5. a protected incoming tile may replace the oldest protected tile only when the requested protected working set exceeds the fixed capacity.

Eviction of published stable detail data must be visible in a later immutable result generation. It must never silently mutate an existing result object.

## Publication contract

Detail aggregation occurs during PCM acceptance, but publication occurs only through the cooperative `apta_session_process()` path.

A new immutable result generation is published when:

- one or more detail columns become complete;
- a resident tile containing published columns is evicted;
- lifecycle state changes alter a tile from stable to final.

Publication failure preserves mutation state so a later process call can retry.

## Coverage and gaps

A column never claims PCM that was not analysed. The current public tile view has no independent coverage-range array, so one tile view can expose only one contiguous analysed column run.

Before activation, the implementation must ensure that a stable published tile identity never switches between unrelated disjoint runs. The preferred MVP rule is to publish a tile only when its complete coverage is contiguous from the first exposed column through the last exposed column. Fragmented internal work remains unpublished until the gap is filled or is discarded by cache eviction.

## Result ownership

Every immutable result owns copied tile descriptors and copied portable waveform columns. A result may outlive its session. Result accessors return pointers valid until the result reference is released.

## Request progress

For a request containing overview and detail, satisfaction requires both requested features. For a detail-only request:

- no detail output means `WAITING_FOR_PCM`;
- some detail output means `PARTIALLY_SATISFIED`;
- complete requested detail coverage means `SATISFIED`.

Absence of an overview requirement is vacuously satisfied for final completion but must not be counted as partial output.

## Activation gates

The detail modules remain excluded from `apta_core` until all gates pass:

- transactional PCM acceptance regression test;
- detail-only request progress regression test;
- deterministic output across block partitioning;
- out-of-order non-overlapping input test;
- focus-protected LRU eviction test;
- publication retry after detail snapshot allocation failure;
- result lifetime after session destruction;
- memory-requirement accounting;
- ASan/UBSan clean run;
- one bounded fuzz/parser run remains green with detail disabled in `.apta` serialization;
- one PR run passes before merge to `main`.

## Deferred work

The MVP does not yet include:

- feature-specific PCM replay;
- `WDTL` serialization;
- multiple detail levels;
- dynamic cache sizing;
- persistent on-disk tile cache;
- static-workspace allocation;
- three-band energy data.
