# Result and snapshot model

**Status:** APTA 1.0 Release Candidate Draft

## 1. Immutable generations

A session publishes analysis through immutable result generations.

Once acquired by an application, the contents of one generation MUST NOT change. Further analysis creates a newer generation with a larger generation number.

## 2. Result handle

```c
typedef struct apta_result apta_result_t;

const apta_result_t *apta_session_acquire_result(
    const apta_session_t *session);

void apta_result_release(
    const apta_result_t *result);
```

A successful acquire operation returns a retained reference. The caller MUST release it exactly once.

Acquiring the current generation multiple times MAY return the same pointer or distinct handles representing identical immutable data.

## 3. Lifetime

An acquired result remains valid until its matching release, even when the originating session is cancelled or destroyed.

The owning context MUST remain alive until every result created under it is released. `apta_destroy()` SHOULD report a busy error or provide a documented deferred-destruction mechanism when results remain outstanding.

## 4. Thread safety

Acquisition, release and read-only access to immutable results MUST be thread-safe.

No accessor may return mutable internal storage.

## 5. Generation identity

Each session maintains a monotonically increasing unsigned 64-bit generation number:

```c
typedef uint64_t apta_generation_t;
```

Generation zero means no published result. The first published result has generation one.

A generation number is unique only within one session lineage. Cross-session equality requires source identity and provenance comparison.

## 6. Result metadata

Every generation exposes at least:

- specification version;
- producer API version;
- container compatibility version when deserialized;
- source identity summary;
- session lineage identifier;
- generation number;
- available feature mask;
- changed feature mask relative to the previous generation when known;
- session state;
- diagnostics summary;
- producer and backend provenance.

## 7. Accessor-based public API

The stable public ABI SHOULD expose result data through accessors rather than one permanently growing structure containing pointers to every possible feature.

Example direction:

```c
apta_generation_t apta_result_get_generation(
    const apta_result_t *result);

apta_feature_mask_t apta_result_get_available_features(
    const apta_result_t *result);

apta_status_t apta_result_get_feature_state(
    const apta_result_t *result,
    apta_feature_mask_t feature,
    apta_frame_range_t range,
    apta_feature_state_t *state_out);
```

Feature-specific accessors return immutable views with explicit count, coverage and lifetime tied to the result handle.

## 8. Feature descriptors

Each feature descriptor includes:

- feature identifier;
- feature state;
- exact coverage, potentially as multiple disjoint ranges;
- confidence or `APTA_CONFIDENCE_UNKNOWN`;
- payload generation or revision identifier;
- flags;
- diagnostic reference when failed or degraded.

A feature with disjoint analysed regions MUST expose all ranges or a query mechanism. It MUST NOT report one continuous range that hides gaps.

## 9. Changes between generations

A newer generation MAY:

- add coverage;
- add a new feature;
- refine provisional values;
- raise confidence;
- transition lifecycle state forward;
- add a pending revision;
- record a failure or diagnostic;
- evict non-persistent implementation cache that is not part of the published result.

A newer generation MUST NOT silently alter stable locked data. Such a change requires explicit revision metadata and host policy.

## 10. Progress

Progress is feature- and scope-dependent. One global percentage is insufficient for all uses.

The API MAY provide a convenience aggregate progress value, but MUST also make available:

- requested scope;
- completed coverage;
- missing PCM coverage;
- per-feature request status;
- finality conditions.

Progress values MUST NOT decrease merely because a higher-priority request is added; the new request has its own progress scope.

## 11. Diagnostics

Diagnostics are immutable records associated with a generation or request. They SHOULD include:

- stable numeric diagnostic code;
- severity;
- affected feature;
- affected source-frame range when applicable;
- implementation message for development tools;
- whether processing can continue.

Human-readable messages are informative and MUST NOT be the sole machine-readable error representation.

## 12. Serialization

Serialization operates on one immutable result generation.

A serializer MUST record explicit feature coverage and state. It MUST NOT infer continuous coverage from array length when gaps are possible.

A partial or provisional generation MAY be serialized when the container profile permits it.

## 13. Import and continuation

`apta_session_seed_from_result()` allows a fresh created session to import compatible parsed waveform coverage and continue missing analysis. The result is not retained by the call.

The portable 1.0 seeding subset MUST validate:

- overview geometry;
- known sample rate and channel count;
- known source length;
- coverage bounds and integrity;
- session state and source compatibility required by the operation.

Tempo and beatgrid publication is not resumable from the portable result because the onset work state is not serialized. The implementation MUST rebuild that evidence from PCM and MUST NOT present a cached estimate as newly derived.

Source identity remains an explicit host policy when no usable fingerprint is present. Private backend work state MUST NOT be required for another conforming implementation to consume portable published payloads.
