# APTA bounded waveform-detail MVP 0.1

**Status:** Implemented and verified  
**Merged implementation:** `9283599d769517c5b54a6f5cabc9c42147943b4d`  
**Current verified integration:** `e9eef869578656a25a00ed583b8c657e71549f33`  
**Verification evidence:** GitHub Actions PR CI runs `#139`, `#143`, `#145` and `#147`

## Objective

Provide focus-local higher-resolution waveform data without changing PCM ownership, blocking cooperative processing or allowing unbounded memory growth.

## Fixed MVP geometry

The implementation uses one detail level:

- `level_id = 1`;
- `frames_per_column = 256`;
- `columns_per_tile = 64`;
- `frames_per_tile = 16384`;
- maximum resident tiles per session = 4.

Tile identity is the stable tuple `(level_id, tile_index)`. Geometry does not change within one result lineage.

## Feature dependency

A detail-enabled session currently requests:

```text
APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_WAVEFORM_DETAIL
```

Detail-only sessions are rejected because overview remains the authoritative accepted-range and PCM ownership layer. Within an overview-plus-detail session, individual focus and region requests may request detail only.

## PCM acceptance and replay

Normal PCM acceptance is transactional:

1. the overview layer validates, accepts and copies a bounded PCM prefix;
2. the same normalized mono samples are offered to the detail cache;
3. detail cache pressure cannot retroactively change the accepted frame count;
4. normal overlapping pushes remain conflicts.

When overview already owns a range but a detail tile was skipped or evicted, the scheduler may issue a detail-only PCM replay request. Replay is accepted only when it matches the current detail demand and respects 256-frame column alignment, except for a shorter declared final source column.

## Cache and eviction policy

The cache is embedded in the session object and performs no per-tile heap allocation.

Eviction priority:

1. use an empty slot;
2. otherwise evict the least-recently-used unprotected tile;
3. a tile intersecting active focus or an unsatisfied detail request is protected;
4. an unprotected incoming tile is skipped when every resident tile is protected;
5. a protected incoming tile may replace the oldest protected tile when the requested protected working set exceeds capacity.

Moving focus to a skipped or evicted tile causes a detail-only replay request. Eviction is reflected only in a later immutable result generation and never mutates an existing result.

## Publication and coverage

Detail aggregation occurs during PCM acceptance or replay, while publication occurs only through `apta_session_process()`.

A new immutable generation is published when:

- detail columns become complete;
- a resident published tile is evicted;
- lifecycle state changes;
- a transient publication allocation failure is successfully retried.

One public tile view exposes one contiguous analysed run. Missing detail columns are not represented as fabricated valid columns.

## Result ownership

Every result owns copied tile descriptors and copied portable waveform columns. A result may outlive its session, and accessor pointers remain valid until the result reference is released.

## Progressive request status

For a detail request:

- no output is `WAITING_FOR_PCM`;
- partial tile coverage is `PARTIALLY_SATISFIED`;
- complete requested coverage is `SATISFIED`.

For a combined overview-plus-detail request, both requested features must be satisfied.

## Serialization

Version-1 `.apta` detail serialization is implemented through optional `WDTL` sections:

- canonical sorted tile descriptors;
- packed 10-byte waveform columns;
- CRC32C section protection;
- lifecycle and confidence preservation;
- immutable reader ownership;
- writer → reader → writer byte identity;
- malformed-input and allocation-failure tests.

Overview-only output remains byte-identical when no detail tiles are present.

## Verified gates

The merged implementation has verified coverage for:

- transactional PCM acceptance;
- detail-only region progress;
- fixed detail geometry and lifecycle;
- focus-protected bounded eviction;
- feature-specific replay;
- result lifetime after session destruction;
- memory-requirement accounting;
- WDTL round-trip and malformed parsing;
- WDTL allocation-failure cleanup;
- scheduler deadline and starvation policy integration;
- GCC/Clang builds;
- ASan/UBSan runtime tests;
- bounded parser fuzz smoke.

## Deferred work

The MVP does not yet include:

- multiple detail levels;
- dynamic cache sizing;
- persistent on-disk tile cache;
- static-workspace-only allocation;
- three-band energy data;
- measured embedded publication latency and stack usage.
